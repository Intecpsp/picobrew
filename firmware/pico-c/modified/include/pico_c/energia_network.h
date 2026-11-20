/* Pico C reconstruction, adapted from the archived project S/Pro typed source.
 * Original C entry and data correspondence: analysis/function-correspondence.json.
 * Behavioral coverage is reported separately; no physical qualification is implied.
 */
#ifndef PICO_C_ENERGIA_NETWORK_H
#define PICO_C_ENERGIA_NETWORK_H
#include "pico_c/network.h"
#include "pico_c/pico_http_client.h"

struct PicoEnergiaNetwork {
    PicoHttpClient *client;
};

/* The object supplied here must already have its native C++ constructor run. */
const PicoNetworkPlatform *pico_energia_network_platform();
#endif
