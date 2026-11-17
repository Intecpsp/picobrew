#include "picobrew/energia_http_server.h"
#include <WiFi.h>
#include <utility/simplelink.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

bool WiFiClass::_initialized;
WiFiClass WiFi;
static unsigned cases, calls, socket_calls, accepted_nonblocking, reads, file_closes, socket_closes;
static uint32_t now_ms;
static int wifi_status, stat_result, open_result, read_result, close_result, accept_result;
static const char key[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static uint8_t file_data[4096];
static size_t file_size;

uint32_t millis() { return now_ms; }
uint8_t WiFiClass::status() { return (uint8_t)wifi_status; }
extern "C" {
_i16 sl_NetAppStop(_u32 app) { assert(app == SL_NET_APP_HTTP_SERVER_ID); ++calls; return 0; }
_i16 sl_Socket(_i16 family, _i16 type, _i16 protocol)
{
    assert(family == SL_AF_INET && type == SL_SOCK_STREAM && protocol == SL_IPPROTO_TCP);
    ++calls; ++socket_calls; return 3;
}
_i16 sl_SetSockOpt(_i16 descriptor, _i16 level, _i16 option, const void *value, SlSocklen_t length)
{
    assert(level == SL_SOL_SOCKET && option == SL_SO_NONBLOCKING);
    assert(length == sizeof(SlSockNonblocking_t));
    assert(((const SlSockNonblocking_t *)value)->NonblockingEnabled == 1);
    if (descriptor == 0) ++accepted_nonblocking;
    ++calls; return 0;
}
_u16 sl_Htons(_u16 value) { return (_u16)((value << 8) | (value >> 8)); }
_i16 sl_Bind(_i16 descriptor, const SlSockAddr_t *raw, SlSocklen_t length)
{
    const SlSockAddrIn_t *address = (const SlSockAddrIn_t *)raw;
    assert(descriptor == 3 && length == sizeof(*address));
    assert(address->sin_family == SL_AF_INET && address->sin_port == sl_Htons(80));
    assert(address->sin_addr.s_addr == 0); ++calls; return 0;
}
_i16 sl_Listen(_i16 descriptor, _i16 backlog) { assert(descriptor == 3 && backlog == 1); ++calls; return 0; }
_i16 sl_Accept(_i16 descriptor, SlSockAddr_t *, SlSocklen_t *)
{
    assert(descriptor == 3); ++calls;
    int result = accept_result; accept_result = SL_EAGAIN; return (_i16)result;
}
_i16 sl_Recv(_i16 descriptor, void *, _i16 length, _i16 flags)
{
    assert(descriptor == 0 && length <= 128 && flags == 0); ++calls; return SL_EAGAIN;
}
_i16 sl_Send(_i16, const void *, _i16, _i16) { assert(false); return -1; }
_i16 sl_Close(_i16) { ++calls; ++socket_closes; return 0; }
_i16 sl_FsGetInfo(const _u8 *path, _u32 token, SlFsFileInfo_t *info)
{
    assert(path && !token); ++calls;
    if (!stat_result) info->FileLen = (_u32)file_size;
    return (_i16)stat_result;
}
_i32 sl_FsOpen(const _u8 *path, _u32 mode, _u32 *token, _i32 *handle)
{
    assert(path && mode == FS_MODE_OPEN_READ && token == NULL); ++calls;
    if (!open_result) *handle = 8;
    return open_result;
}
_i32 sl_FsRead(_i32 handle, _u32 offset, _u8 *data, _u32 length)
{
    assert(handle == 8 && offset == 0 && length <= sizeof(file_data));
    ++calls; ++reads;
    if (read_result) return read_result;
    size_t count = length < file_size ? length : file_size;
    memcpy(data, file_data, count);
    return (_i32)count;
}
_i16 sl_FsClose(_i32 handle, const _u8 *certificate, const _u8 *signature, _u32 length)
{
    assert(handle == 8 && !certificate && !signature && !length);
    ++calls; ++file_closes; return (_i16)close_result;
}
}

static void reset()
{
    calls = socket_calls = accepted_nonblocking = reads = file_closes = socket_closes = 0;
    now_ms = 0;
    wifi_status = WL_CONNECTED;
    stat_result = open_result = read_result = close_result = 0;
    accept_result = SL_EAGAIN;
    WiFiClass::_initialized = false;
    memcpy(file_data, key, 64); file_size = 64;
}

int main()
{
    PicoEnergiaHttpServer server;
    reset();
    pico_energia_http_initialize(&server, NULL, NULL, NULL, 0, 80);
    assert(calls == 0 && server.listener == -1 && server.http.client == -1);
    pico_energia_http_poll(&server);
    assert(calls == 0); ++cases;
    WiFiClass::_initialized = true;
    pico_energia_http_poll(&server);
    assert(server.listener == 3 && server.key_loaded && strcmp(server.http.key, key) == 0);
    assert(socket_calls == 1 && reads == 1 && file_closes == 1); ++cases;
    accept_result = 0; now_ms = 1;
    pico_energia_http_poll(&server);
    assert(server.http.client == 0 && accepted_nonblocking == 1); ++cases;
    unsigned before = calls;
    server.in_poll = true;
    pico_energia_http_poll(&server);
    assert(calls == before); server.in_poll = false; ++cases;
    wifi_status = 0;
    pico_energia_http_poll(&server);
    assert(server.listener == -1 && server.http.client == -1 && socket_closes == 2); ++cases;

    wifi_status = WL_CONNECTED;
    pico_energia_http_poll(&server);
    assert(socket_calls == 2);
    accept_result = -9;
    pico_energia_http_poll(&server);
    assert(server.listener == -1); ++cases;
    now_ms = 999;
    pico_energia_http_poll(&server);
    assert(socket_calls == 2); ++cases;
    now_ms = 1001;
    pico_energia_http_poll(&server);
    assert(socket_calls == 3 && server.listener == 3); ++cases;
    pico_energia_http_shutdown(&server);

    size_t size = 77;
    assert(pico_energia_local_file_stat(NULL, "/local/recipe", &size) == 0 && size == 64); ++cases;
    stat_result = SL_FS_ERR_FILE_NOT_EXISTS;
    assert(pico_energia_local_file_stat(NULL, "/local/recipe", &size) == 1); ++cases;
    const int errors[] = {-2, -3, -7, -9, -12, -19, -30, -39, -53};
    for (int error : errors) {
        stat_result = error; size = 71;
        assert(pico_energia_local_file_stat(NULL, "/local/recipe", &size) == error && size == 71);
        ++cases;
    }
    stat_result = 0;
    uint8_t output[512];
    memset(file_data, 0x71, sizeof(output)); file_size = sizeof(output);
    reads = file_closes = 0;
    assert(pico_energia_local_file_read(NULL, "/local/recipe", output, sizeof(output)) == 512);
    assert(reads == 1 && file_closes == 1 && memcmp(output, file_data, sizeof(output)) == 0); ++cases;
    read_result = -22;
    assert(pico_energia_local_file_read(NULL, "/local/recipe", output, sizeof(output)) == -22);
    assert(file_closes == 2); ++cases;
    read_result = 0; close_result = -30;
    assert(pico_energia_local_file_read(NULL, "/local/recipe", output, sizeof(output)) == -30); ++cases;
    close_result = 0; open_result = -11; before = file_closes;
    assert(pico_energia_local_file_read(NULL, "/local/recipe", output, sizeof(output)) == -11);
    assert(file_closes == before); ++cases;
    before = calls;
    assert(pico_energia_local_file_read(NULL, "/local/recipe", output, 4097) < 0);
    assert(calls == before); ++cases;

    for (unsigned mode = 0; mode < 7; ++mode) {
        reset(); WiFiClass::_initialized = true;
        switch (mode) {
        case 0: stat_result = SL_FS_ERR_FILE_NOT_EXISTS; break;
        case 1: stat_result = -39; break;
        case 2: file_data[0] = 'Z'; break;
        case 3: file_size = 67; break;
        case 4: open_result = -6; break;
        case 5: read_result = 63; break;
        case 6: close_result = -7; break;
        }
        pico_energia_http_initialize(&server, NULL, NULL, NULL, 0, 80);
        pico_energia_http_poll(&server);
        assert(server.listener == 3 && server.key_loaded && server.http.key[0] == '\0');
        pico_energia_http_shutdown(&server); ++cases;
    }
    reset(); WiFiClass::_initialized = true;
    file_data[64] = '\r'; file_data[65] = '\n'; file_size = 66;
    pico_energia_http_initialize(&server, NULL, NULL, NULL, 0, 80);
    pico_energia_http_poll(&server);
    assert(strcmp(server.http.key, key) == 0); ++cases;
    pico_energia_http_shutdown(&server);
    printf("Native HTTP adapter: %u mocked SimpleLink cases passed.\n", cases);
}
