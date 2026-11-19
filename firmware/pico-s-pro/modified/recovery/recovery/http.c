#include "http.h"
#include "recovery.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* This protocol deliberately accepts only flat objects containing bounded
 * unsigned integers, ASCII strings and booleans. No escaping is needed for
 * hashes, versions or hexadecimal chunks. Unknown and duplicate keys fail
 * before any core operation. Values remain slices of the request until all
 * validation is complete; the HTTP response may then replace that arena. */
typedef enum JsonType { J_UINT, J_STRING, J_BOOL } JsonType;
typedef struct Field {
    const char *key, *text;
    size_t key_length, length;
    uint32_t number;
    JsonType type;
    bool boolean;
} Field;
typedef struct Object { Field field[16]; size_t count; } Object;

static void whitespace(const char **p, const char *end)
{
    while (*p < end && (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')) ++*p;
}
static bool string_slice(const char **p, const char *end, const char **start, size_t *length)
{
    if (*p == end || *(*p)++ != '"') return false;
    *start = *p;
    while (*p < end && **p != '"') {
        unsigned char byte = (unsigned char)**p;
        if (byte < 32 || byte >= 127 || byte == '\\') return false;
        ++*p;
    }
    if (*p == end) return false;
    *length = (size_t)(*p - *start); ++*p; return true;
}
static bool key_is(const Field *f, const char *key)
{
    return f->key_length == strlen(key) && !memcmp(f->key, key, f->key_length);
}
static bool parse_object(const char *body, size_t length, Object *object)
{
    const char *p = body, *end = body + length;
    memset(object, 0, sizeof *object);
    whitespace(&p, end);
    if (p == end || *p++ != '{') return false;
    whitespace(&p, end);
    if (p < end && *p == '}') { ++p; whitespace(&p, end); return p == end; }
    while (p < end && object->count < 16) {
        Field *f = &object->field[object->count];
        if (!string_slice(&p, end, &f->key, &f->key_length) || !f->key_length) return false;
        for (size_t i = 0; i < object->count; ++i)
            if (f->key_length == object->field[i].key_length &&
                !memcmp(f->key, object->field[i].key, f->key_length)) return false;
        whitespace(&p, end);
        if (p == end || *p++ != ':') return false;
        whitespace(&p, end);
        if (p == end) return false;
        if (*p == '"') {
            f->type = J_STRING;
            if (!string_slice(&p, end, &f->text, &f->length)) return false;
        } else if (*p >= '0' && *p <= '9') {
            f->type = J_UINT;
            bool zero = *p == '0'; size_t digits = 0;
            while (p < end && *p >= '0' && *p <= '9') {
                uint32_t value = (uint32_t)(*p - '0');
                if (f->number > (UINT32_MAX - value) / 10 || (zero && digits)) return false;
                f->number = f->number * 10 + value; ++p; ++digits;
            }
        } else if ((size_t)(end - p) >= 4 && !memcmp(p, "true", 4)) {
            f->type = J_BOOL; f->boolean = true; p += 4;
        } else if ((size_t)(end - p) >= 5 && !memcmp(p, "false", 5)) {
            f->type = J_BOOL; p += 5;
        } else return false;
        ++object->count; whitespace(&p, end);
        if (p < end && *p == '}') { ++p; whitespace(&p, end); return p == end; }
        if (p == end || *p++ != ',') return false;
        whitespace(&p, end);
    }
    return false;
}
static const Field *field(const Object *object, const char *name, JsonType type)
{
    for (size_t i = 0; i < object->count; ++i)
        if (key_is(&object->field[i], name))
            return object->field[i].type == type ? &object->field[i] : NULL;
    return NULL;
}
static bool uint_field(const Object *object, const char *name, uint32_t *out)
{
    const Field *f = field(object, name, J_UINT);
    if (!f) return false;
    *out = f->number; return true;
}
static int nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static bool hex_bytes(const Field *f, uint8_t *out, size_t capacity, size_t *length)
{
    if (!f || f->type != J_STRING || f->length % 2 || f->length / 2 > capacity) return false;
    for (size_t i = 0; i < f->length; i += 2) {
        int a = nibble(f->text[i]), b = nibble(f->text[i + 1]);
        if (a < 0 || b < 0) return false;
        out[i / 2] = (uint8_t)((a << 4) | b);
    }
    *length = f->length / 2; return true;
}
static bool digest_field(const Object *object, const char *name, uint8_t digest[32])
{
    size_t length;
    return hex_bytes(field(object, name, J_STRING), digest, 32, &length) && length == 32;
}
static void digest_text(const uint8_t digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) { out[2*i] = hex[digest[i] >> 4]; out[2*i+1] = hex[digest[i] & 15]; }
    out[64] = 0;
}

typedef struct Writer { char *data; size_t capacity, length; bool failed; } Writer;
static void append(Writer *writer, const char *format, ...)
{
    if (writer->failed) return;
    va_list args; va_start(args, format);
    int n = vsnprintf(writer->data + writer->length, writer->capacity - writer->length, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= writer->capacity - writer->length) writer->failed = true;
    else writer->length += (size_t)n;
}
static bool respond(PicoHttpResponse *response, char *arena, size_t capacity,
                    unsigned status, const char *message)
{
    size_t length = strlen(message);
    if (length >= capacity) return false;
    memcpy(arena, message, length + 1);
    *response = (PicoHttpResponse){status, "application/json; charset=utf-8", (uint8_t *)arena, length, NULL};
    return true;
}

static PicoRecoveryFile file_for_target(const PicoRecovery *state, uint32_t target)
{
    return target ? (PicoRecoveryFile)(target + 1) :
        state->plan.slot == 3 ? PICO_RECOVERY_IMAGE3 : PICO_RECOVERY_IMAGE2;
}
static unsigned verified_mask(const PicoRecovery *state)
{
    unsigned mask = 0;
    if (!state->planned) return 0;
    for (unsigned target = 0; target < (state->plan.include_web ? 6u : 1u); ++target)
        if (state->verified[file_for_target(state, target)]) mask |= 1u << target;
    return mask;
}
static bool parse_plan(const Object *object, PicoRecoveryPlan *plan)
{
    static const char *const names[] = {"index", "app", "explorer", "history", "style"};
    uint32_t slot;
    memset(plan, 0, sizeof *plan);
    const Field *web = field(object, "web", J_BOOL), *version = field(object, "version", J_STRING);
    if (!web || !version || !version->length || version->length >= sizeof plan->version ||
        !uint_field(object, "slot", &slot) || (slot != 2 && slot != 3) ||
        !uint_field(object, "size", &plan->image.length) ||
        !digest_field(object, "sha256", plan->image.sha256)) return false;
    plan->slot = slot; plan->include_web = web->boolean;
    if (object->count != (plan->include_web ? 15u : 5u)) return false;
    memcpy(plan->version, version->text, version->length);
    for (unsigned i = 0; plan->include_web && i < 5; ++i) {
        char key[32];
        snprintf(key, sizeof key, "%s_size", names[i]);
        if (!uint_field(object, key, &plan->web[i].length)) return false;
        snprintf(key, sizeof key, "%s_sha256", names[i]);
        if (!digest_field(object, key, plan->web[i].sha256)) return false;
    }
    return true;
}
static bool core_error(PicoHttpResponse *response, char *arena, size_t capacity,
                       PicoRecoveryResult result)
{
    unsigned status = result == PICO_RECOVERY_IO ? 503 :
        result == PICO_RECOVERY_ARGUMENT || result == PICO_RECOVERY_CONTENT ||
        result == PICO_RECOVERY_CHECKSUM || result == PICO_RECOVERY_OFFSET ? 400 : 409;
    Writer writer = {arena, capacity, 0, false};
    const char *name = pico_recovery_result_name(result);
    append(&writer, "{\"error\":\"%s\",\"message\":\"Recovery operation stopped: %s. No automatic retry was attempted.\"}", name, name);
    if (writer.failed) return false;
    *response = (PicoHttpResponse){status, "application/json; charset=utf-8", (uint8_t *)arena, writer.length, NULL};
    return true;
}
static bool operation_response(PicoRecovery *state, PicoHttpResponse *response,
                               char *arena, size_t capacity)
{
    Writer writer = {arena, capacity, 0, false};
    append(&writer, "{\"ok\":true,\"id\":%lu,\"next_offset\":%lu,\"verified_mask\":%u,\"boot_committed\":%s}",
        (unsigned long)state->session_id, (unsigned long)state->written,
        verified_mask(state), state->boot_committed ? "true" : "false");
    if (writer.failed) return false;
    *response = (PicoHttpResponse){200, "application/json; charset=utf-8", (uint8_t *)arena, writer.length, NULL};
    return true;
}
static bool inventory_response(PicoRecovery *state, PicoHttpResponse *response,
                               char *arena, size_t capacity)
{
    PicoRecoveryInventory inventory;
    PicoRecoveryResult result = pico_recovery_inspect(state, &inventory);
    if (result != PICO_RECOVERY_OK) return core_error(response, arena, capacity, result);
    uint32_t magic = (uint32_t)inventory.boot_info[4] | (uint32_t)inventory.boot_info[5] << 8 |
        (uint32_t)inventory.boot_info[6] << 16 | (uint32_t)inventory.boot_info[7] << 24;
    bool valid = inventory.boot_info_readable && inventory.boot_info[0] <= 2 &&
        (magic == PICO_RECOVERY_NORMAL_MAGIC || magic == PICO_RECOVERY_TRIAL_MAGIC || magic == UINT32_C(0x12344321));
    Writer writer = {arena, capacity, 0, false};
    append(&writer, "{\"profile\":\"recovery\",\"version\":\"" PICO_RECOVERY_VERSION "\",\"boot\":{\"valid\":%s,\"selector\":%u,\"magic\":\"%08lx\",\"recovery_selected\":%s},\"slots\":[",
        valid ? "true" : "false", inventory.boot_info[0], (unsigned long)magic,
        inventory.writable ? "true" : "false");
    for (unsigned slot = 2; slot <= 3; ++slot) {
        PicoRecoveryFile f = slot == 2 ? PICO_RECOVERY_IMAGE2 : PICO_RECOVERY_IMAGE3;
        bool verified = state->planned && state->plan.slot == slot && state->verified[f];
        append(&writer, "%s{\"slot\":%u,\"present\":%s,\"size\":%lu,\"allocated\":%lu,\"io_error\":%s,\"vectors_valid\":%s,\"verified\":%s}",
            slot == 2 ? "" : ",", slot, inventory.file_status[f] == 0 ? "true" : "false",
            (unsigned long)inventory.files[f].size, (unsigned long)inventory.files[f].allocated,
            inventory.file_status[f] < 0 ? "true" : "false", verified ? "true" : "null", verified ? "true" : "false");
    }
    append(&writer, "],\"plan\":{\"active\":%s,\"id\":%lu,\"slot\":%u,\"version\":\"%s\",\"required_mask\":%u,\"verified_mask\":%u,\"files\":[",
        state->planned ? "true" : "false", (unsigned long)state->session_id,
        state->planned ? state->plan.slot : 0, state->planned ? state->plan.version : "",
        state->planned ? (state->plan.include_web ? 63 : 1) : 0, verified_mask(state));
    for (unsigned target = 0; state->planned && target < (state->plan.include_web ? 6u : 1u); ++target) {
        const PicoRecoveryArtifact *artifact = target ? &state->plan.web[target - 1] : &state->plan.image;
        char sha[65]; digest_text(artifact->sha256, sha);
        append(&writer, "%s{\"target\":%u,\"size\":%lu,\"sha256\":\"%s\",\"verified\":%s}",
            target ? "," : "", target, (unsigned long)artifact->length, sha,
            state->verified[file_for_target(state, target)] ? "true" : "false");
    }
    append(&writer, "]},\"upload\":{\"active\":%s,\"id\":%lu,\"target\":%u,\"next_offset\":%lu},\"boot_committed\":%s}",
        state->upload_active ? "true" : "false", (unsigned long)state->session_id,
        !state->upload_active || state->upload_file <= PICO_RECOVERY_IMAGE3 ? 0 : (unsigned)state->upload_file - 1,
        (unsigned long)state->written, state->boot_committed ? "true" : "false");
    if (writer.failed) return false;
    *response = (PicoHttpResponse){200, "application/json; charset=utf-8", (uint8_t *)arena, writer.length, NULL};
    return true;
}

bool pico_recovery_http_handler(const PicoHttpRequest *request, PicoHttpResponse *response,
                                char *arena, size_t capacity, void *context)
{
    PicoRecovery *state = context;
    if (!request || !response || !arena || !capacity || !state) return false;
    if (request->method == PICO_HTTP_GET && !strcmp(request->path, "/api/v1/health"))
        return respond(response, arena, capacity, 200, request->auth_configured ?
            "{\"api_version\":1,\"mode\":\"recovery\",\"firmware_version\":\"" PICO_RECOVERY_VERSION "\",\"auth_configured\":true}" :
            "{\"api_version\":1,\"mode\":\"recovery\",\"firmware_version\":\"" PICO_RECOVERY_VERSION "\",\"auth_configured\":false}");
    if (!request->auth_configured)
        return respond(response, arena, capacity, 503, "{\"error\":\"api_key_not_provisioned\"}");
    if (!request->authenticated)
        return respond(response, arena, capacity, 401, "{\"error\":\"unauthorized\"}");
    if (request->method == PICO_HTTP_GET && !strcmp(request->path, "/api/v1/recovery"))
        return inventory_response(state, response, arena, capacity);
    if (strncmp(request->path, "/api/v1/recovery/", 17))
        return respond(response, arena, capacity, 404, "{\"error\":\"not_found\"}");
    if (request->method != PICO_HTTP_POST)
        return respond(response, arena, capacity, 405, "{\"error\":\"method_not_allowed\"}");
    Object object;
    if (!request->body || request->body_length >= PICO_HTTP_ARENA_CAPACITY ||
        !parse_object(request->body, request->body_length, &object))
        return respond(response, arena, capacity, 400, "{\"error\":\"invalid_request\"}");
    const char *operation = request->path + 17;
    PicoRecoveryResult result = PICO_RECOVERY_ARGUMENT;
    uint32_t id, target;
    if (!strcmp(operation, "plan")) {
        PicoRecoveryPlan plan;
        if (parse_plan(&object, &plan)) result = pico_recovery_set_plan(state, &plan, &id);
    } else if (!strcmp(operation, "begin") || !strcmp(operation, "verify")) {
        if (object.count == 2 && uint_field(&object, "id", &id) &&
            uint_field(&object, "target", &target) && target < 6) {
            PicoRecoveryFile file = file_for_target(state, target);
            result = !strcmp(operation, "begin") ? pico_recovery_begin(state, id, file) :
                pico_recovery_verify(state, id, file);
        }
    } else if (!strcmp(operation, "chunk")) {
        uint32_t offset; uint8_t bytes[PICO_RECOVERY_CHUNK_MAX]; size_t length;
        if (object.count == 3 && uint_field(&object, "id", &id) &&
            uint_field(&object, "offset", &offset) &&
            hex_bytes(field(&object, "data", J_STRING), bytes, sizeof bytes, &length) && length)
            result = pico_recovery_chunk(state, id, state->upload_file, offset, bytes, length);
    } else if (!strcmp(operation, "finish") || !strcmp(operation, "abort")) {
        if (object.count == 1 && uint_field(&object, "id", &id))
            result = !strcmp(operation, "finish") ? pico_recovery_finish(state, id, state->upload_file) :
                pico_recovery_abort(state, id);
    } else if (!strcmp(operation, "select")) {
        const Field *trial = field(&object, "trial", J_BOOL);
        if (object.count == 2 && uint_field(&object, "id", &id) && trial)
            result = pico_recovery_select(state, id, trial->boolean);
    } else return respond(response, arena, capacity, 404, "{\"error\":\"not_found\"}");
    return result == PICO_RECOVERY_OK ? operation_response(state, response, arena, capacity) :
        core_error(response, arena, capacity, result);
}
