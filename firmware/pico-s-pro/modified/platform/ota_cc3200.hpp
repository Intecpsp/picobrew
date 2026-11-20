/* Standalone boot handoff for the original CC3200 loader. */
#pragma once
#include <string.h>
extern "C" {
#include <utility/simplelink.h>
#include <utility/fs.h>
#include <driverlib/prcm.h>
#include "../recovery/recovery/sha256.h"
}

static bool ota_digest(const char *path, uint32_t limit, char digest[65], uint32_t *length)
{
    const _u8 *name = reinterpret_cast<const _u8 *>(path);
    SlFsFileInfo_t info = {};
    _i32 handle = -1;
    if (sl_FsGetInfo(name, 0, &info) != 0 || !info.FileLen ||
        info.FileLen > limit || info.AllocatedLen < info.FileLen ||
        sl_FsOpen(name, FS_MODE_OPEN_READ, NULL, &handle) != 0) return false;
    PicoRecoverySha256 sha;
    pico_recovery_sha256_init(&sha);
    uint8_t block[512];
    uint32_t offset = 0;
    while (offset < info.FileLen) {
        uint32_t count = info.FileLen - offset;
        if (count > sizeof block) count = sizeof block;
        if (sl_FsRead(handle, offset, block, count) != static_cast<int>(count)) break;
        pico_recovery_sha256_update(&sha, block, count);
        offset += count;
    }
    if (sl_FsClose(handle, NULL, NULL, 0) != 0 || offset != info.FileLen) return false;
    uint8_t bytes[32];
    pico_recovery_sha256_final(&sha, bytes);
    const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < sizeof bytes; ++i) {
        digest[i*2] = hex[bytes[i] >> 4];
        digest[i*2+1] = hex[bytes[i] & 15];
    }
    digest[64] = 0;
    *length = offset;
    return true;
}

static const _u8 ota_boot_file[] = "/sys/mcubootinfo.bin";
static bool ota_record(uint8_t record[8])
{
    SlFsFileInfo_t info = {};
    _i32 handle = -1;
    if (sl_FsGetInfo(ota_boot_file, 0, &info) != 0 || info.FileLen != 8 ||
        sl_FsOpen(ota_boot_file, FS_MODE_OPEN_READ, NULL, &handle) != 0) return false;
    int n = sl_FsRead(handle, 0, record, 8);
    return sl_FsClose(handle, NULL, NULL, 0) == 0 && n == 8;
}
static bool ota_boot_state(const uint8_t record[8], bool trial)
{
    return record[0] == (trial ? 0 : 1) &&
           !memcmp(record + 4, trial ? "\x21\x43\x34\x12" : "\xba\xdc\xcd\xab", 4);
}

static bool ota_inspect(bool *trial, char digest[65], uint32_t *size)
{
    uint8_t record[8];
    if (!ota_record(record)) return false;
    *trial = ota_boot_state(record, true);
    if (!*trial && !ota_boot_state(record, false)) return false;
    if (!ota_digest("/sys/mcuimg.bin", 14956, digest, size) || *size != 14956 ||
        strcmp(digest, "67e22913fd7805a84d965928198f7d8cb795af125b81f1e0c29479a84980b975")) return false;
    // Only the reviewed local recovery application can receive Hub packages.
    if (!ota_digest("/sys/mcuimg1.bin", 43352, digest, size) || *size != 43352 ||
        strcmp(digest, "c76c390034d1564d4d80ddeeae0347b2e7171c820878b44d0fce7731c359c9ce")) return false;
    return ota_digest("/sys/mcuimg2.bin", 188416, digest, size);
}
static bool ota_commit(bool confirm)
{
    uint8_t record[8], readback[8];
    if (!ota_record(record) || !ota_boot_state(record, confirm)) return false;
    record[0] = confirm ? 1 : 0;
    memcpy(record + 4, "\xba\xdc\xcd\xab", 4);
    _i32 handle = -1;
    if (sl_FsOpen(ota_boot_file, FS_MODE_OPEN_WRITE, NULL, &handle) != 0) return false;
    int n = sl_FsWrite(handle, 0, record, sizeof record);
    return sl_FsClose(handle, NULL, NULL, 0) == 0 && n == 8 &&
           ota_record(readback) && !memcmp(record, readback, 8);
}
