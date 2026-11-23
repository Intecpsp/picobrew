/* Reuse the qualified station, socket, bearer-key and GPIO implementation.
   The two original entry points and their recovery write paths are discarded
   by the linker. Only the dedicated stock protocol is registered below. */
#define setup unused_recovery_setup
#define loop unused_recovery_loop
#include "../../recovery/recovery/native.cpp"
#undef setup
#undef loop
extern "C" {
#include "stock.h"
#include "sha256.h"
bool pico_recovery_read_uid(char uid[33]);
bool stock_http(const PicoHttpRequest *, PicoHttpResponse *, char *, size_t, void *);
}

namespace {
const char *const app_path = "/sys/mcuimg2.bin";
const char *const factory_path = "/sys/mcuimg1.bin";
const char *const worker_path = "/sys/mcuimg3.bin";
const char *const loader_path = "/sys/mcuimg.bin";
const char *const app_hash = "e18c240fa1cf35222674bf3b4fa992a306c6e63b0b99be8793604f199085d702";
const char *const factory_hash = "26829ef97df46e6d3667afe8352fa86492f3ddf667371f2d19eafa8468862c63";
const char *const loader_hash = "67e22913fd7805a84d965928198f7d8cb795af125b81f1e0c29479a84980b975";
const char *const uid_hash = "7290674610eca1cf26581779a71c7f41a9c2fc367eb7189d0e93918319964bc7";
const uint8_t normal_record[] = {1, 0x3d, 0, 0x20, 0xba, 0xdc, 0xcd, 0xab};

/* The release builder replaces this unique marker with the digest of the
   complete raw image with these 32 bytes zeroed. The same bytes in the ELF
   and raw image are updated. Use volatile reads to load the installed seal. */
extern "C" const uint8_t stock_worker_seal[32] __attribute__((used)) = {
    0x22,0x6c,0xab,0x39,0xb4,0xce,0x60,0xa5,0x77,0xc0,0x84,0x19,0x41,0xc1,0xe9,0x76,
    0xe0,0x75,0x36,0x9a,0xb7,0x30,0xa0,0xc5,0xb7,0xa1,0x92,0x95,0xd0,0x8c,0x62,0x0d
};
StockRestore stock;
uint32_t last_chunk_ms, retry_station_ms;

const _u8 *file_name(const char *path) { return reinterpret_cast<const _u8 *>(path); }

bool text_digest(const uint8_t bytes[32], const char *expected)
{
    const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i)
        if (hex[bytes[i] >> 4] != expected[2*i] || hex[bytes[i] & 15] != expected[2*i+1])
            return false;
    return true;
}

bool inspect_file(const char *path, uint32_t length, const char *hash)
{
    SlFsFileInfo_t info = {};
    if (sl_FsGetInfo(file_name(path), 0, &info) || info.FileLen != length ||
        info.AllocatedLen < length) return false;
    const bool self = hash == NULL;
    const uint32_t seal_offset = reinterpret_cast<uintptr_t>(stock_worker_seal) - 0x20004000u;
    const volatile uint8_t *seal = stock_worker_seal;
    if (self && (length < 32 || seal_offset > length - 32)) return false;
    _i32 handle = -1;
    if (sl_FsOpen(file_name(path), FS_MODE_OPEN_READ, NULL, &handle)) return false;
    PicoRecoverySha256 sha;
    pico_recovery_sha256_init(&sha);
    bool ok = true;
    uint8_t block[1024];
    for (uint32_t offset = 0; ok && offset < length;) {
        uint32_t count = length - offset;
        if (count > sizeof block) count = sizeof block;
        if (sl_FsRead(handle, offset, block, count) != static_cast<int>(count)) {
            ok = false;
            break;
        }
        if (self && offset < seal_offset + 32 && offset + count > seal_offset) {
            uint32_t first = offset > seal_offset ? offset : seal_offset;
            uint32_t last = offset + count < seal_offset + 32 ? offset + count : seal_offset + 32;
            for (uint32_t i = first; i < last; ++i) {
                if (block[i-offset] != seal[i-seal_offset]) ok = false;
                block[i-offset] = 0;
            }
        }
        pico_recovery_sha256_update(&sha, block, count);
        offset += count;
    }
    if (sl_FsClose(handle, NULL, NULL, 0) || !ok) return false;
    uint8_t digest[32];
    pico_recovery_sha256_final(&sha, digest);
    if (!self) return text_digest(digest, hash);
    for (unsigned i = 0; i < 32; ++i) if (digest[i] != seal[i]) return false;
    return true;
}

bool allocation(const char *path, uint32_t minimum, uint32_t maximum)
{
    SlFsFileInfo_t info = {};
    return !sl_FsGetInfo(file_name(path), 0, &info) &&
           info.AllocatedLen >= minimum && info.AllocatedLen <= maximum &&
           info.FileLen <= info.AllocatedLen;
}

bool own_worker()
{
    SlFsFileInfo_t info = {};
    return !sl_FsGetInfo(file_name(worker_path), 0, &info) &&
           info.FileLen <= STOCK_WORKER_RESERVATION && inspect_file(worker_path, info.FileLen, NULL);
}

int selector()
{
    uint8_t record[8];
    return !logical_read(boot_path, record, sizeof record) &&
           !memcmp(record+1, normal_record+1, 7) ? record[0] : -1;
}

bool qualify(char uid[33])
{
    if (!pico_recovery_read_uid(uid)) return false;
    PicoRecoverySha256 sha;
    uint8_t digest[32];
    pico_recovery_sha256_init(&sha);
    pico_recovery_sha256_update(&sha, reinterpret_cast<const uint8_t *>(uid), 32);
    pico_recovery_sha256_final(&sha, digest);
    /* Profiled 1 MiB S/Pro layout. SDK reports the on-device allocated payload
       capacity, while the published raw FAT sizes also include metadata. */
    return text_digest(digest, uid_hash) && inspect_file(loader_path, 14956, loader_hash) &&
           allocation(app_path, 188416, 48*4096) && allocation(factory_path, STOCK_RECOVERY_BYTES, 16*4096) &&
           allocation(worker_path, STOCK_WORKER_RESERVATION, STOCK_WORKER_RESERVATION+4096) && own_worker();
}

bool matches(StockTarget target)
{
    return target == STOCK_APPLICATION ? inspect_file(app_path, STOCK_APP_BYTES, app_hash) :
                                         inspect_file(factory_path, STOCK_RECOVERY_BYTES, factory_hash);
}

int open_slot(StockTarget target)
{
    _i32 handle = -1;
    return sl_FsOpen(file_name(target == STOCK_APPLICATION ? app_path : factory_path),
                     FS_MODE_OPEN_WRITE, NULL, &handle) == 0 ? handle : -1;
}

bool write_slot(int handle, uint32_t offset, const uint8_t *bytes, size_t length)
{
    return sl_FsWrite(handle, offset, const_cast<_u8 *>(bytes), length) == static_cast<int>(length);
}

bool close_slot(int handle) { return !sl_FsClose(handle, NULL, NULL, 0); }

bool select_stock()
{
    if (selector() != 2) return false;
    _i32 handle = -1;
    if (sl_FsOpen(file_name(boot_path), FS_MODE_OPEN_WRITE, NULL, &handle)) return false;
    int written = sl_FsWrite(handle, 0, const_cast<_u8 *>(normal_record), sizeof normal_record);
    return !sl_FsClose(handle, NULL, NULL, 0) && written == 8 && selector() == 1;
}

bool remove_worker()
{
    if (selector() != 1) return false;
    SlFsFileInfo_t info = {};
    int status = sl_FsGetInfo(file_name(worker_path), 0, &info);
    if (status == SL_FS_ERR_FILE_NOT_EXISTS) return true;
    return !status && own_worker() && selector() == 1 && !sl_FsDel(file_name(worker_path), 0) &&
           sl_FsGetInfo(file_name(worker_path), 0, &info) == SL_FS_ERR_FILE_NOT_EXISTS;
}

const StockStorage stock_storage = {qualify, selector, matches, open_slot, write_slot, close_slot,
                                    select_stock, remove_worker};

bool dispatch(const PicoHttpRequest *req, PicoHttpResponse *res, char *arena, size_t capacity, void *s)
{
    uint32_t before = millis();
    bool handled = stock_http(req, res, arena, capacity, s);
    if (handled && res->status == 200 &&
        (!strcmp(req->path, "/api/v1/stock/begin") || !strcmp(req->path, "/api/v1/stock/chunk")))
        last_chunk_ms = millis();
    handler_elapsed += static_cast<uint32_t>(millis() - before);
    return handled;
}
}

extern "C" void setup()
{
    outputs_off();
    stock_init(&stock, &stock_storage);
    pico_http_server_initialize(&server, &socket_io, NULL, dispatch, &stock, NULL, 0);
    if (start_station()) load_key();
    retry_station_ms = millis();
}

extern "C" void loop()
{
    service_network();
    uint32_t now = millis();
    if (stock.reboot) {
        close_listener();
        if (server.client >= 0) poll_http(now);
        if (server.client < 0) {
            if (!reset_waiting) { reset_waiting = true; reset_at = now; }
            if (now - reset_at >= reset_drain_ms) {
                outputs_off();
                sl_Stop(200);
                PRCMMCUReset(true);
            }
        }
    } else if (online()) {
        retry_station_ms = now;
        if (stock.handle >= 0 && now - last_chunk_ms >= upload_inactivity_ms)
            stock_disconnect_upload(&stock);
        if (listener < 0 && now - listener_attempt >= 1000) { listener_attempt = now; start_listener(); }
        if (listener >= 0) poll_http(now);
    } else {
        pico_http_server_disconnect(&server);
        close_listener();
        if (stock.handle >= 0) stock_disconnect_upload(&stock);
        if (now - retry_station_ms >= 30000) {
            retry_station_ms = now;
            if (nwp_started) { sl_Stop(200); nwp_started = false; }
            if (start_station()) load_key();
        }
    }
    delay(1);
}
