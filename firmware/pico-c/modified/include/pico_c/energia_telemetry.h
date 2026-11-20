#ifndef PICO_C_ENERGIA_TELEMETRY_H
#define PICO_C_ENERGIA_TELEMETRY_H
#include "pico_c/local_telemetry.h"
#ifdef __cplusplus
extern "C" {
#endif
const PicoHistoryStorage *pico_energia_telemetry_storage(void);
const PicoTelemetryTransport *pico_energia_telemetry_transport(void);
#ifdef __cplusplus
}
#endif
#endif
