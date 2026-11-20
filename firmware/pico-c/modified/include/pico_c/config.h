/* Pico C reconstruction, adapted from the archived project S/Pro typed source.
 * Original C entry and data correspondence: analysis/function-correspondence.json.
 * Behavioral coverage is reported separately; no physical qualification is implied.
 */
#ifndef PICO_C_CONFIG_H
#define PICO_C_CONFIG_H
#include "pico_c/version.h"
#include "pico_c/model.h"
#ifndef PICO_STANDALONE
#define PICO_STANDALONE 0
#endif
#define PICO_LOCAL_API_PORT 80
#define PICO_LOCAL_API_KEY_FILE "/local/api.key"

/* Original protocol destination. Change these before rebuilding to use a
 * compatible community server. No account credentials belong in this file. */
#ifndef PICO_SERVER_HOST
#define PICO_SERVER_HOST "picobrew.com"
#endif
#ifndef PICO_SERVER_PORT
#define PICO_SERVER_PORT 80
#endif
#endif
