#include "recovery.h"
#include "sha256.h"
#include <string.h>

enum { RF_STAT, RF_OPEN_READ, RF_OPEN_WRITE, RF_CREATE, RF_READ, RF_WRITE,
       RF_CLOSE, RF_BOOT_READ, RF_BOOT_WRITE, RF_OPERATIONS };

typedef struct {
    uint8_t bytes[PICO_RECOVERY_IMAGE_MAX];
    uint32_t size, allocated;
    int present;
} File;

typedef struct { int used, writing; PicoRecoveryFile file; } Handle;
static File files[PICO_RECOVERY_FILE_COUNT];
static Handle handles[16];
static uint8_t boot_info[8];
static unsigned calls[RF_OPERATIONS], fault_at[RF_OPERATIONS], fault_mode[RF_OPERATIONS];
static unsigned protected_violations, boot_change_at;
static PicoRecovery recovery;
static PicoRecoveryPlan request_plan;

static unsigned called(unsigned operation)
{
    ++calls[operation];
    return calls[operation] == fault_at[operation] ? fault_mode[operation] : 0;
}

static int file_valid(PicoRecoveryFile file)
{
    return (unsigned)file < PICO_RECOVERY_FILE_COUNT;
}

static int writable(void)
{
    return boot_info[0] == 0 && boot_info[4] == 0xba && boot_info[5] == 0xdc &&
           boot_info[6] == 0xcd && boot_info[7] == 0xab;
}

static int mutation(PicoRecoveryFile file)
{
    if (!file_valid(file) || !writable()) {
        ++protected_violations;
        return 0;
    }
    return 1;
}

static int32_t new_handle(PicoRecoveryFile file, int writing)
{
    for (unsigned i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i)
        if (!handles[i].used) {
            handles[i] = (Handle){1, writing, file};
            return (int32_t)i;
        }
    return -99;
}

static int valid_handle(int32_t handle)
{
    return handle >= 0 && (unsigned)handle < sizeof(handles) / sizeof(handles[0]) &&
           handles[handle].used;
}

static int stat_file(void *context, PicoRecoveryFile file, PicoRecoveryFileInfo *info)
{
    (void)context;
    if (called(RF_STAT) || !file_valid(file))
        return -7;
    if (!files[file].present)
        return 1;
    info->size = files[file].size;
    info->allocated = files[file].allocated;
    return 0;
}

static int32_t open_read(void *context, PicoRecoveryFile file)
{
    (void)context;
    if (called(RF_OPEN_READ) || !file_valid(file) || !files[file].present)
        return -7;
    return new_handle(file, 0);
}

static int32_t open_write(void *context, PicoRecoveryFile file)
{
    (void)context;
    unsigned fault = called(RF_OPEN_WRITE);
    if (!mutation(file) || !files[file].present || fault == 1)
        return -7;
    /* Faithful destructive-open model: existing contents disappear here. */
    memset(files[file].bytes, 0xff, sizeof(files[file].bytes));
    files[file].size = 0;
    return fault ? -7 : new_handle(file, 1);
}

static int32_t create(void *context, PicoRecoveryFile file, uint32_t maximum)
{
    (void)context;
    unsigned fault = called(RF_CREATE);
    if (!mutation(file) || file <= PICO_RECOVERY_IMAGE3 || files[file].present ||
        maximum != pico_recovery_file_limit(file)) {
        ++protected_violations;
        return -7;
    }
    if (fault == 1)
        return -7;
    files[file].present = 1;
    files[file].size = 0;
    files[file].allocated = maximum;
    memset(files[file].bytes, 0xff, sizeof(files[file].bytes));
    return fault ? -7 : new_handle(file, 1);
}

static int32_t read_file(void *context, int32_t handle, uint32_t offset,
                         uint8_t *data, size_t size)
{
    (void)context;
    unsigned fault = called(RF_READ);
    if (!valid_handle(handle) || handles[handle].writing || fault == 1)
        return -7;
    File *f = &files[handles[handle].file];
    if (offset > f->size || size > f->size - offset)
        return -7;
    size_t count = size - (fault == 2 && size ? 1 : 0);
    memcpy(data, f->bytes + offset, count);
    if (fault == 3 && count)
        data[count - 1] ^= 1;
    return (int32_t)count;
}

static int32_t write_file(void *context, int32_t handle, uint32_t offset,
                          const uint8_t *data, size_t size)
{
    (void)context;
    unsigned fault = called(RF_WRITE);
    if (!valid_handle(handle) || !handles[handle].writing ||
        !mutation(handles[handle].file) || fault == 1)
        return -7;
    File *f = &files[handles[handle].file];
    if (offset > f->allocated || size > f->allocated - offset ||
        offset > sizeof(f->bytes) || size > sizeof(f->bytes) - offset)
        return -7;
    size_t count = size - (fault == 2 && size ? 1 : 0);
    memcpy(f->bytes + offset, data, count);
    if (fault == 3 && count)
        f->bytes[offset + count - 1] ^= 1;
    if (f->size < offset + count)
        f->size = (uint32_t)(offset + count);
    return (int32_t)count;
}

static int close_file(void *context, int32_t handle)
{
    (void)context;
    unsigned fault = called(RF_CLOSE);
    if (!valid_handle(handle))
        return -7;
    if (fault == 2 && files[handles[handle].file].size)
        files[handles[handle].file].bytes[0] ^= 1;
    handles[handle].used = 0;
    return fault == 1 ? -7 : 0;
}

static int read_boot(void *context, uint8_t data[8])
{
    (void)context;
    unsigned fault = called(RF_BOOT_READ);
    if (calls[RF_BOOT_READ] == boot_change_at)
        boot_info[0] = 1;
    if (fault)
        return -7;
    memcpy(data, boot_info, 8);
    return 0;
}

static int write_boot(void *context, const uint8_t data[8])
{
    (void)context;
    unsigned fault = called(RF_BOOT_WRITE);
    if (!writable() || memcmp(data + 1, boot_info + 1, 3) ||
        !(((data[0] == 1 || data[0] == 2) && !memcmp(data + 4, "\xba\xdc\xcd\xab", 4)) ||
          (data[0] == 0 && !memcmp(data + 4, "\x65\x87\x78\x56", 4)))) {
        ++protected_violations;
        return -7;
    }
    if (fault == 1)
        return -7;
    if (fault == 2) {
        memcpy(boot_info, data, 4);
        return -7;
    }
    if (fault == 3)
        return 0; /* Successful return without durable data. */
    memcpy(boot_info, data, 8);
    if (fault == 5)
        boot_info[1] ^= 1;
    return fault == 4 ? -7 : 0;
}

static const PicoRecoveryOps ops = {
    stat_file, open_read, open_write, create, read_file, write_file, close_file,
    read_boot, write_boot
};

void rf_reset(uint32_t seed)
{
    memset(files, 0, sizeof(files));
    memset(handles, 0, sizeof(handles));
    memset(calls, 0, sizeof(calls));
    memset(fault_at, 0, sizeof(fault_at));
    memset(fault_mode, 0, sizeof(fault_mode));
    memset(&request_plan, 0, sizeof(request_plan));
    memcpy(boot_info, "\x00\x3d\x00\x20\xba\xdc\xcd\xab", 8);
    protected_violations = 0;
    boot_change_at = 0;
    pico_recovery_init(&recovery, &ops, NULL, seed);
}

void rf_reboot(uint32_t seed)
{
    memset(handles, 0, sizeof(handles));
    memset(fault_at, 0, sizeof(fault_at));
    memset(fault_mode, 0, sizeof(fault_mode));
    boot_change_at = 0;
    pico_recovery_init(&recovery, &ops, NULL, seed);
}

void rf_set_file(unsigned file, const uint8_t *data, uint32_t size, uint32_t allocated)
{
    if (file >= PICO_RECOVERY_FILE_COUNT || size > sizeof(files[file].bytes))
        return;
    files[file].present = 1;
    files[file].size = size;
    files[file].allocated = allocated;
    memcpy(files[file].bytes, data, size);
}

void rf_remove_file(unsigned file)
{
    if (file < PICO_RECOVERY_FILE_COUNT)
        files[file].present = 0;
}

uint32_t rf_copy_file(unsigned file, uint8_t *data, uint32_t maximum)
{
    if (file >= PICO_RECOVERY_FILE_COUNT || !files[file].present)
        return 0;
    uint32_t take = files[file].size < maximum ? files[file].size : maximum;
    memcpy(data, files[file].bytes, take);
    return take;
}

uint32_t rf_file_size(unsigned file)
{
    return file < PICO_RECOVERY_FILE_COUNT && files[file].present ? files[file].size : 0;
}

uint32_t rf_file_allocation(unsigned file)
{
    return file < PICO_RECOVERY_FILE_COUNT && files[file].present ? files[file].allocated : 0;
}

void rf_set_boot(const uint8_t data[8]) { memcpy(boot_info, data, 8); }
void rf_get_boot(uint8_t data[8]) { memcpy(data, boot_info, 8); }

int rf_plan(unsigned slot, const char *version, int web,
             const uint32_t lengths[6], const uint8_t hashes[192])
{
    memset(&request_plan, 0, sizeof(request_plan));
    request_plan.slot = slot;
    size_t count = strlen(version);
    if (count > sizeof(request_plan.version))
        count = sizeof(request_plan.version);
    memcpy(request_plan.version, version, count);
    request_plan.image.length = lengths[0];
    memcpy(request_plan.image.sha256, hashes, 32);
    request_plan.include_web = web != 0;
    for (unsigned i = 0; i < PICO_RECOVERY_WEB_COUNT; ++i) {
        request_plan.web[i].length = lengths[i + 1];
        memcpy(request_plan.web[i].sha256, hashes + 32 * (i + 1), 32);
    }
    uint32_t id = 0;
    return pico_recovery_set_plan(&recovery, &request_plan, &id);
}

void rf_mutate_request_length(uint32_t size) { request_plan.image.length = size; }
int rf_begin(uint32_t id, unsigned file)
{ return pico_recovery_begin(&recovery, id, (PicoRecoveryFile)file); }
int rf_chunk(uint32_t id, unsigned file, uint32_t offset, const uint8_t *data, size_t size)
{ return pico_recovery_chunk(&recovery, id, (PicoRecoveryFile)file, offset, data, size); }
int rf_finish(uint32_t id, unsigned file)
{ return pico_recovery_finish(&recovery, id, (PicoRecoveryFile)file); }
int rf_abort(uint32_t id) { return pico_recovery_abort(&recovery, id); }
int rf_verify(uint32_t id, unsigned file)
{ return pico_recovery_verify(&recovery, id, (PicoRecoveryFile)file); }
int rf_select(uint32_t id, int trial)
{ return pico_recovery_select(&recovery, id, trial != 0); }
uint32_t rf_session(void) { return recovery.session_id; }
uint32_t rf_written(void) { return recovery.written; }
int rf_active(void) { return recovery.upload_active; }
int rf_committed(void) { return recovery.boot_committed; }
int rf_verified(unsigned file)
{ return file < PICO_RECOVERY_FILE_COUNT && recovery.verified[file]; }
int rf_inventory(unsigned file, uint32_t *size, uint32_t *allocation, int *writable_out)
{
    PicoRecoveryInventory inventory;
    (void)pico_recovery_inspect(&recovery, &inventory);
    if (file >= PICO_RECOVERY_FILE_COUNT)
        return -7;
    *size = inventory.files[file].size;
    *allocation = inventory.files[file].allocated;
    *writable_out = inventory.writable;
    return inventory.file_status[file];
}
void rf_fault(unsigned operation, unsigned after, unsigned mode)
{
    if (operation < RF_OPERATIONS) {
        fault_at[operation] = calls[operation] + after;
        fault_mode[operation] = mode;
    }
}
void rf_boot_change(unsigned after) { boot_change_at = calls[RF_BOOT_READ] + after; }
unsigned rf_count(unsigned operation)
{ return operation < RF_OPERATIONS ? calls[operation] : 0; }
unsigned rf_violations(void) { return protected_violations; }
unsigned rf_live_handles(void)
{
    unsigned live = 0;
    for (unsigned i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i)
        live += handles[i].used != 0;
    return live;
}
void rf_hash(const uint8_t *data, size_t size, size_t stride, uint8_t digest[32])
{
    PicoRecoverySha256 sha;
    pico_recovery_sha256_init(&sha);
    if (!stride)
        stride = size ? size : 1;
    for (size_t offset = 0; offset < size;) {
        size_t take = size - offset;
        if (take > stride)
            take = stride;
        pico_recovery_sha256_update(&sha, data + offset, take);
        offset += take;
    }
    pico_recovery_sha256_final(&sha, digest);
}
