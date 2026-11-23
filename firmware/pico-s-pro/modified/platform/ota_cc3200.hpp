/* Standalone boot handoff for the original CC3200 loader. */
#pragma once
#include <string.h>
#include "stock_restore/stock.h"
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
    if (*trial) {
        const char *stage = "Verifying firmware";
        if (pico_local_control_busy()) {
            // Initial boot inspection runs before the main menu is displayed.
            pico_display_clear();
            pico_display_center_text(3, stage);
            pico_display_flush();
        } else {
            pico_local_control_show_update_notice(stage);
        }
    }
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
    if (sl_FsClose(handle, NULL, NULL, 0) != 0 || n != 8 ||
        !ota_record(readback) || memcmp(record, readback, 8)) return false;
    if (confirm) {
        pico_local_control_show_update_notice("Update confirmed");
    } else {
        pico_display_clear();
        pico_display_center_text(0, "Installing firmware");
        pico_display_center_text(4, "Keep power on");
        pico_display_flush();
    }
    return true;
}

// The original loader can run a third image independently of both OEM slots.
// This model/UID profile is shared with the USB installer's verified S/Pro backup.
static const _u8 stock_worker_file[] = "/sys/mcuimg3.bin";
static const uint8_t stock_reservation[] = "PicoBrew stock restore v1";

static bool stock_slot_available(bool *absent)
{
    SlFsFileInfo_t info = {};
    int rc = sl_FsGetInfo(stock_worker_file, 0, &info);
    *absent = rc == SL_FS_ERR_FILE_NOT_EXISTS;
    if (*absent) return true;
    if (rc != 0 || info.AllocatedLen < STOCK_WORKER_RESERVATION ||
        info.AllocatedLen > STOCK_WORKER_MAX_ALLOCATION ||
        info.FileLen != sizeof stock_reservation)
        return false;
    _i32 handle = -1;
    if (sl_FsOpen(stock_worker_file, FS_MODE_OPEN_READ, NULL, &handle) != 0)
        return false;
    uint8_t bytes[sizeof stock_reservation];
    int n = sl_FsRead(handle, 0, bytes, sizeof bytes);
    return sl_FsClose(handle, NULL, NULL, 0) == 0 &&
           n == sizeof bytes && memcmp(bytes, stock_reservation, sizeof bytes) == 0;
}

static bool stock_layout_matches(const char *uid, bool allow_trial, bool *absent)
{
    static const uint8_t profile_hash[] = {
        0x72, 0x90, 0x67, 0x46, 0x10, 0xec, 0xa1, 0xcf, 0x26, 0x58, 0x17, 0x79, 0xa7, 0x1c, 0x7f, 0x41,
        0xa9, 0xc2, 0xfc, 0x36, 0x7e, 0xb7, 0x18, 0x9d, 0x0e, 0x93, 0x91, 0x83, 0x19, 0x96, 0x4b, 0xc7
    };
    if (!uid || strnlen(uid, 33) != 32) return false;
    PicoRecoverySha256 sha;
    uint8_t digest[32], record[8];
    pico_recovery_sha256_init(&sha);
    pico_recovery_sha256_update(&sha, reinterpret_cast<const uint8_t *>(uid), 32);
    pico_recovery_sha256_final(&sha, digest);
    if (memcmp(digest, profile_hash, sizeof digest) != 0 || !ota_record(record) ||
        memcmp(record + 1, "\x3d\x00\x20", 3) != 0 ||
        !(ota_boot_state(record, false) || (allow_trial && ota_boot_state(record, true))))
        return false;
    SlFsFileInfo_t app = {}, recovery = {};
    return sl_FsGetInfo(reinterpret_cast<const _u8 *>("/sys/mcuimg2.bin"), 0, &app) == 0 &&
           app.AllocatedLen >= STOCK_APP_MIN_ALLOCATION && app.AllocatedLen <= STOCK_APP_MAX_ALLOCATION &&
           app.FileLen <= app.AllocatedLen &&
           sl_FsGetInfo(reinterpret_cast<const _u8 *>("/sys/mcuimg1.bin"), 0, &recovery) == 0 &&
           recovery.AllocatedLen >= STOCK_RECOVERY_BYTES && recovery.AllocatedLen <= STOCK_RECOVERY_MAX_ALLOCATION &&
           recovery.FileLen <= recovery.AllocatedLen && stock_slot_available(absent);
}

static bool stock_prepare(const char *uid)
{
    bool absent;
    if (!stock_layout_matches(uid, false, &absent)) return false;
    if (!absent) return true; // Retry after reserving, before entering recovery.
    _i32 handle = -1;
    if (sl_FsOpen(stock_worker_file, FS_MODE_OPEN_CREATE(STOCK_WORKER_RESERVATION, 0), NULL, &handle) != 0)
        return false;
    int n = sl_FsWrite(handle, 0, const_cast<uint8_t *>(stock_reservation), sizeof stock_reservation);
    int closed = sl_FsClose(handle, NULL, NULL, 0);
    return closed == 0 && n == sizeof stock_reservation && stock_slot_available(&absent) && !absent;
}
