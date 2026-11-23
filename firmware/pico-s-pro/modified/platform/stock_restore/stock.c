#include "stock.h"
#include <string.h>

static uint32_t image_size(StockTarget target)
{
    return target == STOCK_APPLICATION ? STOCK_APP_BYTES : STOCK_RECOVERY_BYTES;
}

void stock_init(StockRestore *s, const StockStorage *storage)
{
    memset(s, 0, sizeof *s);
    s->storage = *storage;
    s->handle = -1;
}

bool stock_qualify(StockRestore *s)
{
    if (!s->qualified) {
        s->qualified = s->storage.selector() == 2 && s->storage.qualify(s->uid);
    }
    return s->qualified;
}

bool stock_matches(StockRestore *s, StockTarget target)
{
    return stock_qualify(s) && (s->handle < 0 || s->target != target) && s->storage.matches(target);
}

void stock_disconnect_upload(StockRestore *s)
{
    if (s->handle >= 0) {
        s->storage.close(s->handle);
        s->handle = -1;
    }
    s->offset = 0;
}

bool stock_begin(StockRestore *s, StockTarget target)
{
    if (!stock_qualify(s) || s->commit_attempted || s->storage.selector() != 2 ||
        (target != STOCK_APPLICATION && target != STOCK_RECOVERY)) return false;
    /* Explicitly restarting the upload can replace a short/disconnected write.
       The selected worker remains intact throughout either destination write. */
    stock_disconnect_upload(s);
    if (target == STOCK_RECOVERY && !s->storage.matches(STOCK_APPLICATION)) return false;
    s->target = target;
    if (s->storage.matches(target)) {
        s->offset = image_size(target);
        return true;
    }
    s->handle = s->storage.open_write(target);
    return s->handle >= 0;
}

bool stock_chunk(StockRestore *s, uint32_t offset, const uint8_t *bytes, size_t size)
{
    if (s->handle < 0 || s->commit_attempted || s->storage.selector() != 2 ||
        offset != s->offset || !bytes || !size || size > STOCK_CHUNK_MAX ||
        offset > image_size(s->target) || size > image_size(s->target) - offset) return false;
    if (!s->storage.write(s->handle, offset, bytes, size)) {
        stock_disconnect_upload(s);
        return false;
    }
    s->offset += size;
    return true;
}

bool stock_finish(StockRestore *s)
{
    if (!stock_qualify(s) || s->commit_attempted || s->storage.selector() != 2 ||
        s->offset != image_size(s->target)) return false;
    if (s->handle >= 0) {
        bool closed = s->storage.close(s->handle);
        s->handle = -1;
        if (!closed) return false;
    }
    return s->storage.matches(s->target);
}

bool stock_commit(StockRestore *s)
{
    if (!stock_qualify(s) || s->handle >= 0) return false;
    int selector = s->storage.selector();
    if (!(selector == 2 || (selector == 1 && s->commit_attempted))) return false;
    if (!s->storage.matches(STOCK_APPLICATION) || !s->storage.matches(STOCK_RECOVERY)) return false;
    if (selector == 2) {
        /* Recheck loader, allocations, own executable, and device identity before
           the point where the OEM application becomes the next boot target. */
        char uid[33];
        if (!s->storage.qualify(uid) || strcmp(uid, s->uid)) return false;
        s->commit_attempted = true;
        if (!s->storage.select_application()) return false;
    }
    if (s->storage.selector() != 1 || !s->storage.remove_worker()) return false;
    s->committed = true;
    return true;
}

bool stock_reboot(StockRestore *s)
{
    if (!s->committed || s->storage.selector() != 1) return false;
    s->reboot = true;
    return true;
}
