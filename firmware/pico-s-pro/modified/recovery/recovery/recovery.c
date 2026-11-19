#include "recovery.h"
#include "sha256.h"
#include <string.h>

#define IMAGE_BASE UINT32_C(0x20004000)
#define SRAM_END UINT32_C(0x20040000)

static uint32_t little_word(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_word(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(n >> (8 * i));
}

const char *pico_recovery_file_path(PicoRecoveryFile file)
{
    static const char *const paths[PICO_RECOVERY_FILE_COUNT] = {
        "/sys/mcuimg2.bin", "/sys/mcuimg3.bin", "/local/web/i.gz",
        "/local/web/a.gz", "/local/web/e.gz", "/local/web/h.gz", "/local/web/s.gz"
    };
    return (unsigned)file < PICO_RECOVERY_FILE_COUNT ? paths[file] : NULL;
}

uint32_t pico_recovery_file_limit(PicoRecoveryFile file)
{
    if ((unsigned)file >= PICO_RECOVERY_FILE_COUNT)
        return 0;
    if (file <= PICO_RECOVERY_IMAGE3)
        return PICO_RECOVERY_IMAGE_MAX;
    return file == PICO_RECOVERY_WEB_E || file == PICO_RECOVERY_WEB_S ? 3584u : 7680u;
}

const char *pico_recovery_result_name(PicoRecoveryResult result)
{
    static const char *const names[] = {
        "ok", "argument", "locked", "busy", "plan", "not_found", "capacity",
        "session", "offset", "content", "checksum", "io", "incomplete", "state"
    };
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "unknown";
}

static bool configured(const PicoRecovery *r)
{
    return r && r->ops.stat && r->ops.open_read && r->ops.open_existing_write &&
           r->ops.open_create && r->ops.read && r->ops.write && r->ops.close &&
           r->ops.boot_read && r->ops.boot_write;
}

static PicoRecoveryResult result(PicoRecovery *r, PicoRecoveryResult value)
{
    if (r)
        r->last_result = value;
    return value;
}

static bool record_writable(const uint8_t record[8])
{
    return record[0] == 0 && little_word(record + 4) == PICO_RECOVERY_NORMAL_MAGIC;
}

static PicoRecoveryResult guard(PicoRecovery *r, uint8_t record[8])
{
    if (!configured(r))
        return PICO_RECOVERY_ARGUMENT;
    if (r->ops.boot_read(r->context, record) != 0)
        return PICO_RECOVERY_IO;
    return record_writable(record) ? PICO_RECOVERY_OK : PICO_RECOVERY_LOCKED;
}

/* Closing an already-open write handle is mandatory cleanup, even if a changed
 * boot record now forbids additional data writes. The partial file stays
 * unverified. No failure path retries a write, creates an application, or deletes. */
static PicoRecoveryResult drop_upload(PicoRecovery *r)
{
    PicoRecoveryResult value = PICO_RECOVERY_OK;
    if (r->handle >= 0) {
        int32_t handle = r->handle;
        r->handle = -1;
        if (r->ops.close(r->context, handle) != 0)
            value = PICO_RECOVERY_IO;
    }
    if ((unsigned)r->upload_file < PICO_RECOVERY_FILE_COUNT)
        r->verified[r->upload_file] = false;
    r->upload_active = false;
    r->upload_file = PICO_RECOVERY_FILE_COUNT;
    r->written = 0;
    return value;
}

static PicoRecoveryResult failed_io(PicoRecovery *r, PicoRecoveryResult value)
{
    (void)drop_upload(r);
    memset(r->verified, 0, sizeof(r->verified));
    return result(r, value);
}

static const PicoRecoveryArtifact *artifact(const PicoRecovery *r, PicoRecoveryFile file)
{
    if (!r->planned || (unsigned)file >= PICO_RECOVERY_FILE_COUNT)
        return NULL;
    if (file <= PICO_RECOVERY_IMAGE3)
        return (unsigned)file + 2 == r->plan.slot ? &r->plan.image : NULL;
    return r->plan.include_web ? &r->plan.web[file - PICO_RECOVERY_WEB_I] : NULL;
}

static PicoRecoveryResult session(PicoRecovery *r, uint32_t id)
{
    if (!configured(r))
        return PICO_RECOVERY_ARGUMENT;
    if (!r->planned || !id || id != r->session_id)
        return PICO_RECOVERY_SESSION;
    return r->boot_committed ? PICO_RECOVERY_STATE : PICO_RECOVERY_OK;
}

static bool version_valid(const char version[PICO_RECOVERY_VERSION_SIZE])
{
    for (unsigned i = 0; i < PICO_RECOVERY_VERSION_SIZE; ++i) {
        unsigned char c = (unsigned char)version[i];
        if (!c)
            return i != 0;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '+'))
            return false;
    }
    return false;
}

static bool header_valid(PicoRecoveryFile file, const uint8_t *data, size_t count,
                         uint32_t image_length)
{
    if (file <= PICO_RECOVERY_IMAGE3) {
        if (count < 8)
            return false;
        uint32_t sp = little_word(data), reset = little_word(data + 4);
        uint32_t entry = reset & ~UINT32_C(1);
        uint32_t image_end = IMAGE_BASE + image_length;
        /* The first stack push must not overwrite loaded code. A Thumb entry
         * also needs at least one complete halfword, including odd file sizes.
         * Subtracting from the bounded image end avoids entry+2 overflow. */
        return (sp & 7u) == 0 && sp >= image_end + 8u && sp <= SRAM_END &&
               (reset & 1u) && entry >= IMAGE_BASE + 8u && entry <= image_end - 2u;
    }
    return count >= 10 && data[0] == 0x1f && data[1] == 0x8b &&
           data[2] == 8 && !(data[3] & 0xe0);
}

static PicoRecoveryResult capacity(PicoRecovery *r, PicoRecoveryFile file,
                                   const PicoRecoveryArtifact *a, bool *exists)
{
    PicoRecoveryFileInfo info = {0, 0};
    int status = r->ops.stat(r->context, file, &info);
    if (status == 1) {
        *exists = false;
        return file <= PICO_RECOVERY_IMAGE3 ? PICO_RECOVERY_NOT_FOUND : PICO_RECOVERY_OK;
    }
    if (status != 0 || info.size > info.allocated)
        return PICO_RECOVERY_IO;
    *exists = true;
    return a->length <= info.allocated ? PICO_RECOVERY_OK : PICO_RECOVERY_CAPACITY;
}

void pico_recovery_init(PicoRecovery *r, const PicoRecoveryOps *ops, void *context,
                        uint32_t session_seed)
{
    if (!r)
        return;
    memset(r, 0, sizeof(*r));
    if (ops)
        r->ops = *ops;
    r->context = context;
    r->sequence = session_seed;
    r->handle = -1;
    r->upload_file = PICO_RECOVERY_FILE_COUNT;
}

PicoRecoveryResult pico_recovery_inspect(PicoRecovery *r, PicoRecoveryInventory *out)
{
    if (!configured(r) || !out)
        return result(r, PICO_RECOVERY_ARGUMENT);
    memset(out, 0, sizeof(*out));
    out->boot_info_readable = r->ops.boot_read(r->context, out->boot_info) == 0;
    out->writable = out->boot_info_readable && record_writable(out->boot_info) &&
                    !r->boot_committed;
    PicoRecoveryResult value = out->boot_info_readable ? PICO_RECOVERY_OK : PICO_RECOVERY_IO;
    for (unsigned i = 0; i < PICO_RECOVERY_FILE_COUNT; ++i) {
        out->file_status[i] = r->ops.stat(r->context, (PicoRecoveryFile)i, &out->files[i]);
        if (out->file_status[i] != 0 && out->file_status[i] != 1)
            value = PICO_RECOVERY_IO;
    }
    return value == PICO_RECOVERY_IO ? failed_io(r, value) : result(r, value);
}

PicoRecoveryResult pico_recovery_set_plan(PicoRecovery *r, const PicoRecoveryPlan *plan,
                                         uint32_t *id)
{
    uint8_t record[8];
    if (!configured(r) || !plan || !id)
        return result(r, PICO_RECOVERY_ARGUMENT);
    if (r->upload_active)
        return result(r, PICO_RECOVERY_BUSY);
    if (r->boot_committed)
        return result(r, PICO_RECOVERY_STATE);
    if ((plan->slot != 2 && plan->slot != 3) || !version_valid(plan->version) ||
        plan->image.length < 16 || plan->image.length > PICO_RECOVERY_IMAGE_MAX)
        return result(r, PICO_RECOVERY_PLAN);
    if (plan->include_web) {
        for (unsigned i = 0; i < PICO_RECOVERY_WEB_COUNT; ++i)
            if (plan->web[i].length < 10 || plan->web[i].length >
                pico_recovery_file_limit((PicoRecoveryFile)(PICO_RECOVERY_WEB_I + i)))
                return result(r, PICO_RECOVERY_PLAN);
    }
    PicoRecoveryResult value = guard(r, record);
    if (value != PICO_RECOVERY_OK)
        return failed_io(r, value);
    bool exists;
    value = capacity(r, (PicoRecoveryFile)(plan->slot - 2), &plan->image, &exists);
    if (value != PICO_RECOVERY_OK)
        return value == PICO_RECOVERY_IO ? failed_io(r, value) : result(r, value);
    if (plan->include_web) {
        for (unsigned i = 0; i < PICO_RECOVERY_WEB_COUNT; ++i) {
            value = capacity(r, (PicoRecoveryFile)(PICO_RECOVERY_WEB_I + i), &plan->web[i],
                             &exists);
            if (value != PICO_RECOVERY_OK)
                return value == PICO_RECOVERY_IO ? failed_io(r, value) : result(r, value);
        }
    }
    r->plan = *plan;
    r->planned = true;
    if (++r->sequence == 0)
        ++r->sequence;
    r->session_id = r->sequence;
    memset(r->verified, 0, sizeof(r->verified));
    *id = r->session_id;
    return result(r, PICO_RECOVERY_OK);
}

PicoRecoveryResult pico_recovery_begin(PicoRecovery *r, uint32_t id, PicoRecoveryFile file)
{
    uint8_t record[8];
    PicoRecoveryResult value = session(r, id);
    if (value != PICO_RECOVERY_OK)
        return result(r, value);
    if (r->upload_active)
        return result(r, PICO_RECOVERY_BUSY);
    const PicoRecoveryArtifact *a = artifact(r, file);
    if (!a)
        return result(r, PICO_RECOVERY_PLAN);
    value = guard(r, record);
    if (value != PICO_RECOVERY_OK)
        return failed_io(r, value);
    bool exists;
    value = capacity(r, file, a, &exists);
    if (value != PICO_RECOVERY_OK)
        return value == PICO_RECOVERY_IO ? failed_io(r, value) : result(r, value);
    r->upload_file = file;
    r->upload_active = true;
    r->written = 0;
    r->handle = -1;
    r->verified[file] = false;
    return result(r, PICO_RECOVERY_OK);
}

PicoRecoveryResult pico_recovery_chunk(PicoRecovery *r, uint32_t id, PicoRecoveryFile file,
                                      uint32_t offset, const uint8_t *data, size_t length)
{
    uint8_t record[8];
    PicoRecoveryResult value = session(r, id);
    if (value != PICO_RECOVERY_OK)
        return result(r, value);
    if (!data || !length || length > PICO_RECOVERY_CHUNK_MAX)
        return result(r, PICO_RECOVERY_ARGUMENT);
    const PicoRecoveryArtifact *a = artifact(r, file);
    if (!a || !r->upload_active || r->upload_file != file)
        return result(r, PICO_RECOVERY_STATE);
    if (offset != r->written || offset > a->length || length > a->length - offset)
        return result(r, PICO_RECOVERY_OFFSET);
    value = guard(r, record);
    if (value != PICO_RECOVERY_OK)
        return failed_io(r, value);
    if (r->handle < 0) {
        if (offset || !header_valid(file, data, length, a->length))
            return failed_io(r, PICO_RECOVERY_CONTENT);
        bool exists;
        value = capacity(r, file, a, &exists);
        if (value != PICO_RECOVERY_OK)
            return failed_io(r, value);
        value = guard(r, record);
        if (value != PICO_RECOVERY_OK)
            return failed_io(r, value);
        r->handle = exists ? r->ops.open_existing_write(r->context, file) :
                             r->ops.open_create(r->context, file, pico_recovery_file_limit(file));
        if (r->handle < 0)
            return failed_io(r, PICO_RECOVERY_IO);
    }
    value = guard(r, record);
    if (value != PICO_RECOVERY_OK)
        return failed_io(r, value);
    if (r->ops.write(r->context, r->handle, offset, data, length) != (int32_t)length)
        return failed_io(r, PICO_RECOVERY_IO);
    r->written += (uint32_t)length;
    return result(r, PICO_RECOVERY_OK);
}

PicoRecoveryResult pico_recovery_verify(PicoRecovery *r, uint32_t id, PicoRecoveryFile file)
{
    uint8_t record[8], buffer[512], digest[32];
    PicoRecoveryResult value = session(r, id);
    if (value != PICO_RECOVERY_OK)
        return result(r, value);
    if (r->upload_active)
        return result(r, PICO_RECOVERY_BUSY);
    const PicoRecoveryArtifact *a = artifact(r, file);
    if (!a)
        return result(r, PICO_RECOVERY_PLAN);
    r->verified[file] = false;
    value = guard(r, record);
    if (value != PICO_RECOVERY_OK)
        return failed_io(r, value);
    PicoRecoveryFileInfo info = {0, 0};
    int status = r->ops.stat(r->context, file, &info);
    if (status == 1)
        return result(r, PICO_RECOVERY_NOT_FOUND);
    if (status != 0 || info.size > info.allocated)
        return failed_io(r, PICO_RECOVERY_IO);
    if (info.size != a->length || info.allocated < a->length)
        return result(r, PICO_RECOVERY_INCOMPLETE);
    int32_t handle = r->ops.open_read(r->context, file);
    if (handle < 0)
        return failed_io(r, PICO_RECOVERY_IO);
    PicoRecoverySha256 sha;
    pico_recovery_sha256_init(&sha);
    for (uint32_t offset = 0; offset < a->length;) {
        size_t take = a->length - offset;
        if (take > sizeof(buffer))
            take = sizeof(buffer);
        if (r->ops.read(r->context, handle, offset, buffer, take) != (int32_t)take) {
            value = PICO_RECOVERY_IO;
            break;
        }
        if (!offset && !header_valid(file, buffer, take, a->length)) {
            value = PICO_RECOVERY_CONTENT;
            break;
        }
        pico_recovery_sha256_update(&sha, buffer, take);
        offset += (uint32_t)take;
    }
    if (r->ops.close(r->context, handle) != 0)
        value = PICO_RECOVERY_IO;
    if (value == PICO_RECOVERY_OK) {
        pico_recovery_sha256_final(&sha, digest);
        if (memcmp(digest, a->sha256, sizeof(digest)))
            value = PICO_RECOVERY_CHECKSUM;
    }
    if (value == PICO_RECOVERY_IO)
        return failed_io(r, value);
    r->verified[file] = value == PICO_RECOVERY_OK;
    return result(r, value);
}

PicoRecoveryResult pico_recovery_finish(PicoRecovery *r, uint32_t id, PicoRecoveryFile file)
{
    uint8_t record[8];
    PicoRecoveryResult value = session(r, id);
    if (value != PICO_RECOVERY_OK)
        return result(r, value);
    const PicoRecoveryArtifact *a = artifact(r, file);
    if (!a || !r->upload_active || r->upload_file != file)
        return result(r, PICO_RECOVERY_STATE);
    if (r->written != a->length || r->handle < 0)
        return failed_io(r, PICO_RECOVERY_INCOMPLETE);
    value = guard(r, record);
    if (value != PICO_RECOVERY_OK)
        return failed_io(r, value);
    if (drop_upload(r) != PICO_RECOVERY_OK)
        return failed_io(r, PICO_RECOVERY_IO);
    return pico_recovery_verify(r, id, file);
}

PicoRecoveryResult pico_recovery_abort(PicoRecovery *r, uint32_t id)
{
    uint8_t record[8];
    PicoRecoveryResult value = session(r, id);
    if (value != PICO_RECOVERY_OK)
        return result(r, value);
    value = guard(r, record);
    PicoRecoveryResult closed = drop_upload(r);
    if (value != PICO_RECOVERY_OK || closed != PICO_RECOVERY_OK)
        return failed_io(r, value != PICO_RECOVERY_OK ? value : closed);
    return result(r, PICO_RECOVERY_OK);
}

PicoRecoveryResult pico_recovery_select(PicoRecovery *r, uint32_t id, bool trial)
{
    uint8_t record[8], readback[8];
    PicoRecoveryResult value = session(r, id);
    if (value != PICO_RECOVERY_OK)
        return result(r, value);
    if (r->upload_active)
        return result(r, PICO_RECOVERY_BUSY);
    if (trial && r->plan.slot != 2)
        return result(r, PICO_RECOVERY_PLAN);
    for (unsigned i = 0; i < PICO_RECOVERY_FILE_COUNT; ++i)
        if (artifact(r, (PicoRecoveryFile)i)) {
            value = pico_recovery_verify(r, id, (PicoRecoveryFile)i);
            if (value != PICO_RECOVERY_OK)
                return result(r, value);
        }
    value = guard(r, record);
    if (value != PICO_RECOVERY_OK)
        return failed_io(r, value);
    record[0] = trial ? 0 : (uint8_t)(r->plan.slot - 1);
    put_word(record + 4, trial ? PICO_RECOVERY_TRIAL_MAGIC : PICO_RECOVERY_NORMAL_MAGIC);
    if (r->ops.boot_write(r->context, record) != 0)
        return failed_io(r, PICO_RECOVERY_IO);
    if (r->ops.boot_read(r->context, readback) != 0 || memcmp(record, readback, sizeof(record)))
        return failed_io(r, PICO_RECOVERY_IO);
    r->boot_committed = true;
    return result(r, PICO_RECOVERY_OK);
}
