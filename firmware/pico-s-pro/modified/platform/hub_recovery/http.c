/* Compile the pinned web recovery dispatcher under a private name. The Hub
 * build adds a single authenticated, read-only appliance identity endpoint. */
#include "version.h"
#define pico_recovery_http_handler pico_recovery_web_handler
#include "../../recovery/recovery/http.c"
#undef pico_recovery_http_handler

extern bool pico_recovery_read_uid(char uid[33]);

bool pico_recovery_http_handler(const PicoHttpRequest *request, PicoHttpResponse *response,
                                char *arena, size_t capacity, void *context)
{
    if (!request || strcmp(request->path, "/api/v1/recovery/identity") ||
        !request->auth_configured || !request->authenticated)
        return pico_recovery_web_handler(request, response, arena, capacity, context);
    if (request->method != PICO_HTTP_GET)
        return respond(response, arena, capacity, 405, "{\"error\":\"method_not_allowed\"}");
    char uid[33], json[96];
    if (!pico_recovery_read_uid(uid))
        return respond(response, arena, capacity, 409, "{\"error\":\"identity_unavailable\"}");
    snprintf(json, sizeof json, "{\"hub_ota_protocol\":1,\"device_uid\":\"%s\"}", uid);
    return respond(response, arena, capacity, 200, json);
}
