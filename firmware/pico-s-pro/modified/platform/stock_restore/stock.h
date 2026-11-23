#ifndef PICO_STOCK_RESTORE_H
#define PICO_STOCK_RESTORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { STOCK_APPLICATION, STOCK_RECOVERY } StockTarget;
typedef struct {
    bool (*qualify)(char uid[33]);          /* UID, loader, both allocations and this worker */
    int (*selector)(void);                 /* -1 for unreadable or unfamiliar boot records */
    bool (*matches)(StockTarget);
    int (*open_write)(StockTarget);
    bool (*write)(int handle, uint32_t offset, const uint8_t *data, size_t size);
    bool (*close)(int handle);
    bool (*select_application)(void);
    bool (*remove_worker)(void);           /* only when stock selected; idempotent */
} StockStorage;
typedef struct {
    StockStorage storage;
    char uid[33];
    int handle;
    StockTarget target;
    uint32_t offset;
    bool qualified, commit_attempted, committed, reboot;
} StockRestore;

#define STOCK_APP_BYTES 162792u
#define STOCK_RECOVERY_BYTES 50504u
#define STOCK_WORKER_RESERVATION 49152u
#define STOCK_CHUNK_MAX 1024u

void stock_init(StockRestore *, const StockStorage *);
bool stock_qualify(StockRestore *);
bool stock_matches(StockRestore *, StockTarget);
bool stock_begin(StockRestore *, StockTarget);
bool stock_chunk(StockRestore *, uint32_t, const uint8_t *, size_t);
bool stock_finish(StockRestore *);
bool stock_commit(StockRestore *);
bool stock_reboot(StockRestore *);
void stock_disconnect_upload(StockRestore *);
#endif
