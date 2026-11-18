#ifndef PICO_RECOVERY_HTTP_H
#define PICO_RECOVERY_HTTP_H
#include "picobrew/http_server.h"
#ifdef __cplusplus
extern "C" {
#endif
/* context is a PicoRecovery. Uses the existing authenticated, bounded HTTP
 * transport; no endpoint accepts a filesystem path or performs a reset. */
bool pico_recovery_http_handler(const PicoHttpRequest *request,
                                PicoHttpResponse *response, char *arena,
                                size_t capacity, void *context);
#ifdef __cplusplus
}
#endif
#endif
