#ifndef PICO_RECOVERY_H
#define PICO_RECOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PICO_RECOVERY_IMAGE_MAX 188416u
#define PICO_RECOVERY_VERSION "0.1.0"
#define PICO_RECOVERY_CHUNK_MAX 1024u
#define PICO_RECOVERY_VERSION_SIZE 32u
#define PICO_RECOVERY_WEB_COUNT 5u
#define PICO_RECOVERY_NORMAL_MAGIC UINT32_C(0xabcddcba)
#define PICO_RECOVERY_TRIAL_MAGIC UINT32_C(0x56788765)

/* No caller-supplied path ever reaches the storage interface. */
typedef enum {
    PICO_RECOVERY_IMAGE2 = 0,
    PICO_RECOVERY_IMAGE3,
    PICO_RECOVERY_WEB_I,
    PICO_RECOVERY_WEB_A,
    PICO_RECOVERY_WEB_E,
    PICO_RECOVERY_WEB_H,
    PICO_RECOVERY_WEB_S,
    PICO_RECOVERY_FILE_COUNT
} PicoRecoveryFile;

typedef enum {
    PICO_RECOVERY_OK = 0,
    PICO_RECOVERY_ARGUMENT,
    PICO_RECOVERY_LOCKED,
    PICO_RECOVERY_BUSY,
    PICO_RECOVERY_PLAN,
    PICO_RECOVERY_NOT_FOUND,
    PICO_RECOVERY_CAPACITY,
    PICO_RECOVERY_SESSION,
    PICO_RECOVERY_OFFSET,
    PICO_RECOVERY_CONTENT,
    PICO_RECOVERY_CHECKSUM,
    PICO_RECOVERY_IO,
    PICO_RECOVERY_INCOMPLETE,
    PICO_RECOVERY_STATE
} PicoRecoveryResult;

typedef struct {
    uint32_t size;
    uint32_t allocated;
} PicoRecoveryFileInfo;

typedef struct {
    /* stat: 0 present, 1 definitely absent, negative error. Other functions:
     * open returns a nonnegative handle, read/write return byte counts,
     * close/boot_read/boot_write return 0 only for complete success.
     * boot callbacks operate on exactly eight logical filesystem bytes. */
    int (*stat)(void *, PicoRecoveryFile, PicoRecoveryFileInfo *);
    int32_t (*open_read)(void *, PicoRecoveryFile);
    int32_t (*open_existing_write)(void *, PicoRecoveryFile);
    int32_t (*open_create)(void *, PicoRecoveryFile, uint32_t max_bytes);
    int32_t (*read)(void *, int32_t, uint32_t, uint8_t *, size_t);
    int32_t (*write)(void *, int32_t, uint32_t, const uint8_t *, size_t);
    int (*close)(void *, int32_t);
    int (*boot_read)(void *, uint8_t record[8]);
    int (*boot_write)(void *, const uint8_t record[8]);
} PicoRecoveryOps;

typedef struct {
    uint32_t length;
    uint8_t sha256[32];
} PicoRecoveryArtifact;

typedef struct {
    unsigned slot;                 /* exactly 2 or 3 */
    char version[PICO_RECOVERY_VERSION_SIZE];
    PicoRecoveryArtifact image;
    bool include_web;              /* all five entries when true */
    PicoRecoveryArtifact web[PICO_RECOVERY_WEB_COUNT];
} PicoRecoveryPlan;

typedef struct {
    uint8_t boot_info[8];
    bool boot_info_readable;
    bool writable;                 /* normal magic, selector 0 */
    int file_status[PICO_RECOVERY_FILE_COUNT];
    PicoRecoveryFileInfo files[PICO_RECOVERY_FILE_COUNT];
} PicoRecoveryInventory;

/* The caller may read state for its response serializer, but only core
 * functions may change it. A successful set_plan copies the plan. */
typedef struct {
    PicoRecoveryOps ops;
    void *context;
    PicoRecoveryPlan plan;
    uint32_t session_id;
    uint32_t sequence;
    uint32_t written;
    int32_t handle;
    PicoRecoveryFile upload_file;
    PicoRecoveryResult last_result;
    bool planned;
    bool upload_active;
    bool verified[PICO_RECOVERY_FILE_COUNT];
    bool boot_committed;
} PicoRecovery;

void pico_recovery_init(PicoRecovery *, const PicoRecoveryOps *, void *, uint32_t session_seed);
PicoRecoveryResult pico_recovery_inspect(PicoRecovery *, PicoRecoveryInventory *);
PicoRecoveryResult pico_recovery_set_plan(PicoRecovery *, const PicoRecoveryPlan *, uint32_t *session);
/* begin only prepares an upload. Destructive open occurs in the first chunk,
 * after complete vector (8 byte) or gzip-header (10 byte) validation. */
PicoRecoveryResult pico_recovery_begin(PicoRecovery *, uint32_t session, PicoRecoveryFile);
PicoRecoveryResult pico_recovery_chunk(PicoRecovery *, uint32_t session, PicoRecoveryFile,
                                     uint32_t offset, const uint8_t *, size_t);
PicoRecoveryResult pico_recovery_finish(PicoRecovery *, uint32_t session, PicoRecoveryFile);
PicoRecoveryResult pico_recovery_abort(PicoRecovery *, uint32_t session);
/* verify reads an already-present file; it never opens it for writing. */
PicoRecoveryResult pico_recovery_verify(PicoRecovery *, uint32_t session, PicoRecoveryFile);
/* Re-verifies the whole plan and commits/readbacks the record. trial is allowed
 * only for image2, retaining selector0 so the next unconfirmed reset returns to
 * recovery. Never resets; native glue may reset after its HTTP response flush. */
PicoRecoveryResult pico_recovery_select(PicoRecovery *, uint32_t session, bool trial);
const char *pico_recovery_result_name(PicoRecoveryResult);
const char *pico_recovery_file_path(PicoRecoveryFile);
uint32_t pico_recovery_file_limit(PicoRecoveryFile);

#ifdef __cplusplus
}
#endif
#endif
