/* Deterministic power cuts, UART-independent storage, and the actual request handler. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stock.h"
#include "picobrew/http_server.h"

extern bool stock_http(const PicoHttpRequest *, PicoHttpResponse *, char *, size_t, void *);
static StockRestore worker;
static const char fake_uid[] = "0123456789abcdef0123456789abcdef";
static uint8_t original[2][STOCK_APP_BYTES], stored[2][STOCK_APP_BYTES];
static const size_t lengths[] = {STOCK_APP_BYTES, STOCK_RECOVERY_BYTES};
static size_t written[2];
static unsigned opens[2], removals;
static int selected, failure;
static bool worker_present, authorized;
static char response[4096];

static bool qualify(char uid[33])
{
    memcpy(uid, fake_uid, sizeof fake_uid);
    return authorized && worker_present;
}
static int selector(void) { return selected; }
static bool matches(StockTarget t)
{
    return written[t] == lengths[t] && !memcmp(stored[t], original[t], lengths[t]);
}
static int open_slot(StockTarget t)
{
    assert(selected == 2 && worker_present);
    ++opens[t];
    written[t] = 0;
    return (int)t + 10;
}
static bool write_slot(int handle, uint32_t offset, const uint8_t *bytes, size_t count)
{
    int t = handle - 10;
    assert(selected == 2 && worker_present && t >= 0 && t < 2 && count <= 1024);
    assert(offset == written[t] && offset + count <= lengths[t]);
    if (failure == 1) return false;
    memcpy(stored[t] + offset, bytes, count);
    written[t] += count;
    return true;
}
static bool close_slot(int handle) { assert(handle == 10 || handle == 11); return failure != 2; }
static bool select_stock(void)
{
    assert(selected == 2 && worker_present && matches(STOCK_APPLICATION) && matches(STOCK_RECOVERY));
    selected = 1;
    return failure != 3; /* includes lost readback after a successful write */
}
static bool remove_worker(void)
{
    assert(selected == 1 && matches(STOCK_APPLICATION) && matches(STOCK_RECOVERY));
    if (failure == 4) return false;
    if (worker_present) ++removals;
    worker_present = false;
    return true;
}
static const StockStorage storage = {qualify, selector, matches, open_slot, write_slot, close_slot,
                                    select_stock, remove_worker};
void fixture_power_cycle(void) { stock_init(&worker, &storage); }
void fixture_reset(const uint8_t *application, const uint8_t *recovery)
{
    memcpy(original[0], application, lengths[0]);
    memcpy(original[1], recovery, lengths[1]);
    memset(written, 0, sizeof written);
    memset(opens, 0, sizeof opens);
    selected = 2;
    authorized = worker_present = true;
    failure = removals = 0;
    fixture_power_cycle();
}
void fixture_profile(int permitted, int selector) { authorized = !!permitted; selected = selector; }
void fixture_failure(int value) { failure = value; }
int fixture_opens(int target) { return opens[target]; }
int fixture_cleanup(void) { return removals; }

int fixture_request(const char *method, const char *op, const char *body, int auth)
{
    PicoHttpRequest request = {0};
    request.method = !strcmp(method, "GET") ? PICO_HTTP_GET : PICO_HTTP_POST;
    snprintf(request.path, sizeof request.path, "/api/v1/stock/%s", op);
    request.auth_configured = auth != -1;
    request.authenticated = auth == 1;
    request.body = body;
    request.body_length = strlen(body);
    PicoHttpResponse out = {0};
    bool handled = stock_http(&request, &out, response, sizeof response, &worker);
    assert(handled);
    return (int)out.status;
}
const char *fixture_response(void) { return response; }
