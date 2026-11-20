/* Pico C reconstruction, adapted from the archived project S/Pro typed source.
 * Original C entry and data correspondence: analysis/function-correspondence.json.
 * Behavioral coverage is reported separately; no physical qualification is implied.
 */
#ifndef PICO_C_PICO_HTTP_CLIENT_H
#define PICO_C_PICO_HTTP_CLIENT_H
#include <WiFiClient.h>

/* Pico's two observed TCP connection paths with native SDK object lifetime,
 * layout and inherited I/O. No extra state or guessed vtable offsets. */
class PicoHttpClient : public WiFiClient
{
  public:
    virtual int connect(IPAddress address, uint16_t port);
    using WiFiClient::connect;
    int connectPico(IPAddress address, uint16_t port, bool background);
};
#endif
