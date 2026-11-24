/* A USB-enrolled recovery's display identity survives replacement of the
 * application and website. The existing core owns hashing and trial selection. */
#define pico_recovery_http_handler shared_recovery_handler
#include "../../../../pico-s-pro/modified/recovery/recovery/http.c"
#undef pico_recovery_http_handler
#include "target.h"

extern bool pico_recovery_read_uid(char uid[33]);

static bool image_matches_display(PicoRecovery *r)
{
    const char expected[] = PICO_C_TARGET_TAG;
    uint8_t found[sizeof expected];
    int32_t handle = r->ops.open_read(r->context, PICO_RECOVERY_IMAGE2);
    if (handle < 0) return false;
    int32_t n = r->ops.read(r->context, handle, PICO_C_TARGET_OFFSET, found, sizeof found);
    return r->ops.close(r->context, handle) == 0 && n == sizeof found &&
           !memcmp(expected, found, sizeof found);
}

bool pico_recovery_http_handler(const PicoHttpRequest *q, PicoHttpResponse *response,
                                char *arena, size_t capacity, void *context)
{
    if (!q || !response || !arena || !context) return false;
    PicoRecovery *r = context;
    if (q->auth_configured && q->authenticated) {
        if (!strcmp(q->path, "/api/v1/recovery/identity") && q->method == PICO_HTTP_GET) {
            char uid[33], json[140];
            if (!pico_recovery_read_uid(uid))
                return respond(response, arena, capacity, 409, "{\"error\":\"identity_unavailable\"}");
            snprintf(json, sizeof json, "{\"hub_ota_protocol\":1,\"device_uid\":\"%s\","
                     "\"model\":\"pico-c\",\"display_variant\":\"" PICO_DISPLAY_VARIANT "\"}", uid);
            return respond(response, arena, capacity, 200, json);
        }
        if (q->method == PICO_HTTP_POST && !strcmp(q->path, "/api/v1/recovery/plan")) {
            Object o;
            PicoRecoveryPlan plan;
            uint32_t id;
            if (!q->body || q->body_length >= PICO_HTTP_ARENA_CAPACITY ||
                !parse_object(q->body, q->body_length, &o))
                return core_error(response, arena, capacity, PICO_RECOVERY_ARGUMENT);
            const Field *variant = field(&o, "display_variant", J_STRING);
            if (!variant || variant->length != strlen(PICO_DISPLAY_VARIANT) ||
                memcmp(variant->text, PICO_DISPLAY_VARIANT, variant->length))
                return respond(response, arena, capacity, 409, "{\"error\":\"display_mismatch\"}");
            o.field[variant - o.field] = o.field[--o.count];
            if (!parse_plan(&o, &plan) || plan.slot != 2 || !plan.include_web)
                return core_error(response, arena, capacity, PICO_RECOVERY_PLAN);
            PicoRecoveryResult rc = pico_recovery_set_plan(r, &plan, &id);
            return rc == PICO_RECOVERY_OK ? operation_response(r, response, arena, capacity) :
                   core_error(response, arena, capacity, rc);
        }
        if (q->method == PICO_HTTP_POST && !strcmp(q->path, "/api/v1/recovery/select")) {
            Object o;
            if (!q->body || q->body_length >= PICO_HTTP_ARENA_CAPACITY ||
                !parse_object(q->body, q->body_length, &o))
                return core_error(response, arena, capacity, PICO_RECOVERY_ARGUMENT);
            const Field *trial = field(&o, "trial", J_BOOL);
            if (!trial || !trial->boolean || !r->planned || !r->verified[PICO_RECOVERY_IMAGE2] ||
                !image_matches_display(r))
                return core_error(response, arena, capacity, PICO_RECOVERY_CONTENT);
        }
    }
    return shared_recovery_handler(q, response, arena, capacity, context);
}
