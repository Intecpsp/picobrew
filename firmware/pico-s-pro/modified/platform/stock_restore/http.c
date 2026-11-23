/* Reuse the shipped recovery parser's bounded, duplicate-key-rejecting JSON
   handling. Only stock_http is registered with this independent application. */
#define pico_recovery_http_handler unused_recovery_handler
#include "../../recovery/recovery/http.c"
#undef pico_recovery_http_handler
#include "stock.h"

static bool equals(const Field *value, const char *expected)
{
    return value && value->length == strlen(expected) && !memcmp(value->text, expected, value->length);
}

bool stock_http(const PicoHttpRequest *request, PicoHttpResponse *response,
                char *arena, size_t capacity, void *context)
{
    StockRestore *s = context;
    const char prefix[] = "/api/v1/stock/";
    if (strncmp(request->path, prefix, sizeof prefix - 1)) return false;
    if (!request->auth_configured || !request->authenticated)
        return respond(response, arena, capacity, 401, "{\"error\":\"authentication_required\"}");
    const char *op = request->path + sizeof prefix - 1;
    if (!stock_qualify(s))
        return respond(response, arena, capacity, 409, "{\"error\":\"unsupported_stock_layout\"}");
    char json[192];
    if (!strcmp(op, "status") && request->method == PICO_HTTP_GET) {
        snprintf(json, sizeof json, "{\"ok\":true,\"device_uid\":\"%s\",\"app_verified\":%s,"
                 "\"recovery_verified\":%s,\"boot_committed\":%s}", s->uid,
                 stock_matches(s, STOCK_APPLICATION) ? "true" : "false",
                 stock_matches(s, STOCK_RECOVERY) ? "true" : "false", s->committed ? "true" : "false");
        return respond(response, arena, capacity, 200, json);
    }
    if (request->method != PICO_HTTP_POST)
        return respond(response, arena, capacity, 405, "{\"error\":\"method_not_allowed\"}");
    Object object;
    if (!parse_object(request->body, request->body_length, &object) ||
        !equals(field(&object, "device_uid", J_STRING), s->uid))
        return respond(response, arena, capacity, 400, "{\"error\":\"invalid_request_or_device\"}");
    bool success;
    if (!strcmp(op, "begin")) {
        const Field *target = field(&object, "target", J_STRING);
        if (object.count != 2 || !(equals(target, "application") || equals(target, "recovery")))
            return respond(response, arena, capacity, 400, "{\"error\":\"invalid_target\"}");
        success = stock_begin(s, equals(target, "application") ? STOCK_APPLICATION : STOCK_RECOVERY);
    } else if (!strcmp(op, "chunk")) {
        uint32_t offset;
        uint8_t bytes[STOCK_CHUNK_MAX];
        size_t length;
        if (object.count != 3 || !uint_field(&object, "offset", &offset) ||
            !hex_bytes(field(&object, "data", J_STRING), bytes, sizeof bytes, &length) || !length)
            return respond(response, arena, capacity, 400, "{\"error\":\"invalid_chunk\"}");
        success = stock_chunk(s, offset, bytes, length);
    } else if (object.count != 1) {
        return respond(response, arena, capacity, 400, "{\"error\":\"invalid_fields\"}");
    } else if (!strcmp(op, "finish")) success = stock_finish(s);
    else if (!strcmp(op, "commit")) success = stock_commit(s);
    else if (!strcmp(op, "reboot")) success = stock_reboot(s);
    else return respond(response, arena, capacity, 404, "{\"error\":\"unknown_operation\"}");
    if (!success) return respond(response, arena, capacity, 409, "{\"error\":\"stock_restore_unverified\"}");
    snprintf(json, sizeof json, "{\"ok\":true,\"next_offset\":%lu}", (unsigned long)s->offset);
    return respond(response, arena, capacity, 200, json);
}
