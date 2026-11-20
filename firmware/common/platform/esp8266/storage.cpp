#include "storage.hpp"
#include "spiffs/spiffs.h"
#include <cstring>
#include <limits>
#include <new>

// The pinned SDK's checked physical flash HAL; the filesystem wrapper's begin()
// automatically formats failed mounts, so this adapter uses the raw API.
extern std::int32_t spiffs_hal_read(std::uint32_t, std::uint32_t, std::uint8_t*);
extern std::int32_t spiffs_hal_write(std::uint32_t, std::uint32_t, std::uint8_t*);
extern std::int32_t spiffs_hal_erase(std::uint32_t, std::uint32_t);
extern "C" std::uint32_t _SPIFFS_start, _SPIFFS_end, _SPIFFS_page, _SPIFFS_block;

namespace picobrew { namespace standalone { namespace esp8266 {
namespace {
SpiffsGeometry linked_partition() {
    SpiffsGeometry result;
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(&_SPIFFS_start);
    const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(&_SPIFFS_end);
    const std::uintptr_t page = reinterpret_cast<std::uintptr_t>(&_SPIFFS_page);
    const std::uintptr_t block = reinterpret_cast<std::uintptr_t>(&_SPIFFS_block);
    if (start < 0x40200000u || end < start || end - 0x40200000u > UINT32_MAX ||
        page > UINT32_MAX || block > UINT32_MAX) return result;
    result.start = static_cast<std::uint32_t>(start - 0x40200000u);
    result.size = static_cast<std::uint32_t>(end - start);
    result.page_size = static_cast<std::uint32_t>(page);
    result.block_size = static_cast<std::uint32_t>(block);
    return result;
}
}

struct SpiffsStorage::Impl {
    spiffs fs;
    SpiffsGeometry geometry;
    std::uint8_t* work = nullptr;
    std::uint8_t* descriptors = nullptr;
    std::uint8_t* cache = nullptr;
    std::size_t descriptor_bytes = 0, cache_bytes = 0;
    std::int32_t error = SPIFFS_ERR_NOT_MOUNTED;

    explicit Impl(SpiffsGeometry g) : geometry(g) { std::memset(&fs, 0, sizeof(fs)); }
    ~Impl() {
        SPIFFS_unmount(&fs);
        delete[] work; delete[] descriptors; delete[] cache;
    }
    bool geometry_valid() const {
        const std::uint32_t page = geometry.page_size, block = geometry.block_size;
        if (page < 128 || page > 4096 || (page & (page - 1)) || block < 4096 ||
            block % 4096 || block % page || geometry.start % 4096 ||
            geometry.size < block || geometry.size / block < 3 ||
            geometry.size % 4096 || geometry.start > UINT32_MAX - geometry.size)
            return false;
        const std::uint32_t pages = geometry.size / page;
        return geometry.size / block <= std::numeric_limits<spiffs_block_ix>::max() &&
               pages <= std::numeric_limits<spiffs_page_ix>::max() &&
               2u + (pages / 2u) * 2u <= std::numeric_limits<spiffs_obj_id>::max() &&
               pages - 1u <= std::numeric_limits<spiffs_span_ix>::max();
    }
    bool begin() {
        if (SPIFFS_mounted(&fs)) return true;
        if (!geometry_valid()) { error = SPIFFS_ERR_NOT_A_FS; return false; }
        spiffs_config config;
        std::memset(&config, 0, sizeof(config));
        config.hal_read_f = spiffs_hal_read;
        config.hal_write_f = spiffs_hal_write;
        config.hal_erase_f = spiffs_hal_erase;
        config.phys_addr = geometry.start;
        config.phys_size = geometry.size;
        config.phys_erase_block = 4096;
        config.log_block_size = geometry.block_size;
        config.log_page_size = geometry.page_size;
        if (!work) {
            fs.cfg.log_page_size = geometry.page_size;
            descriptor_bytes = SPIFFS_buffer_bytes_for_filedescs(&fs, 3);
            cache_bytes = SPIFFS_buffer_bytes_for_cache(&fs, 3);
            work = new (std::nothrow) std::uint8_t[geometry.page_size * 2u];
            descriptors = new (std::nothrow) std::uint8_t[descriptor_bytes];
            cache = new (std::nothrow) std::uint8_t[cache_bytes];
        }
        if (!work || !descriptors || !cache) {
            delete[] work; delete[] descriptors; delete[] cache;
            work = descriptors = cache = nullptr;
            error = SPIFFS_ERR_INTERNAL; return false;
        }
        error = SPIFFS_mount(&fs, &config, work, descriptors,
                             static_cast<u32_t>(descriptor_bytes), cache,
                             static_cast<u32_t>(cache_bytes), nullptr);
        return error == SPIFFS_OK;
    }
    static Impl* mounted(void* context) {
        Impl* self = static_cast<Impl*>(context);
        if (!self || !SPIFFS_mounted(&self->fs)) {
            if (self) self->error = SPIFFS_ERR_NOT_MOUNTED;
            return nullptr;
        }
        return self;
    }
    StorageResult status(s32_t result) {
        error = result < SPIFFS_OK ? result : SPIFFS_OK;
        if (result >= SPIFFS_OK) return StorageResult::Ok;
        return result == SPIFFS_ERR_NOT_FOUND ? StorageResult::Missing : StorageResult::Error;
    }
    static StorageResult stat(void* context, const char* path, std::size_t& size) {
        size = 0; Impl* self = mounted(context); if (!self) return StorageResult::Error;
        spiffs_stat info;
        const StorageResult result = self->status(SPIFFS_stat(&self->fs, path, &info));
        if (result == StorageResult::Ok) size = info.size;
        return result;
    }
    static StorageResult open(void* context, const char* path, FileHandle& handle, bool write) {
        handle = invalid_file_handle;
        Impl* self = mounted(context); if (!self) return StorageResult::Error;
        const spiffs_flags flags = write ? (SPIFFS_O_CREAT | SPIFFS_O_TRUNC | SPIFFS_O_WRONLY)
                                         : SPIFFS_O_RDONLY;
        const spiffs_file file = SPIFFS_open(&self->fs, path, flags, 0);
        const StorageResult result = self->status(file);
        if (result == StorageResult::Ok) handle = file;
        return result;
    }
    static StorageResult open_read(void* c, const char* p, FileHandle& h) { return open(c,p,h,false); }
    static StorageResult open_write(void* c, const char* p, FileHandle& h) { return open(c,p,h,true); }
    static bool seek(void* context, FileHandle file, std::size_t offset) {
        Impl* self = mounted(context); if (!self || offset > INT32_MAX) return false;
        const s32_t result = SPIFFS_lseek(&self->fs, static_cast<spiffs_file>(file),
                                         static_cast<s32_t>(offset), SPIFFS_SEEK_SET);
        self->error = result < 0 ? result : SPIFFS_OK;
        return result >= 0 && static_cast<std::size_t>(result) == offset;
    }
    static StorageResult read(void* context, FileHandle file, std::uint8_t* data,
                              std::size_t capacity, std::size_t& count) {
        count = 0; Impl* self = mounted(context);
        if (!self || capacity > INT32_MAX) return StorageResult::Error;
        const s32_t result = SPIFFS_read(&self->fs, static_cast<spiffs_file>(file), data,
                                        static_cast<s32_t>(capacity));
        if (result >= 0) count = static_cast<std::size_t>(result);
        return self->status(result);
    }
    static StorageResult write(void* context, FileHandle file, const std::uint8_t* data,
                               std::size_t length, std::size_t& count) {
        count = 0; Impl* self = mounted(context);
        if (!self || length > INT32_MAX) return StorageResult::Error;
        const s32_t result = SPIFFS_write(&self->fs, static_cast<spiffs_file>(file),
                                         const_cast<std::uint8_t*>(data), static_cast<s32_t>(length));
        if (result >= 0) count = static_cast<std::size_t>(result);
        return self->status(result);
    }
    static bool sync(void* context, FileHandle file) {
        Impl* self = mounted(context); if (!self) return false;
        return self->status(SPIFFS_fflush(&self->fs, static_cast<spiffs_file>(file))) == StorageResult::Ok;
    }
    static bool close(void* context, FileHandle file) {
        Impl* self = mounted(context); if (!self) return false;
        const s32_t result = SPIFFS_close(&self->fs, static_cast<spiffs_file>(file));
        self->error = result;
        if (result != SPIFFS_OK) {
            // SPIFFS_close leaves its descriptor open when cache flushing fails.
            // Invalidate this mount, releasing all descriptors; never promote a
            // temporary file after that failure. A subsequent begin remounts it.
            SPIFFS_unmount(&self->fs);
            return false;
        }
        return true;
    }
    static StorageResult rename(void* context, const char* from, const char* to) {
        Impl* self = mounted(context); if (!self) return StorageResult::Error;
        return self->status(SPIFFS_rename(&self->fs, from, to));
    }
    static StorageResult remove(void* context, const char* path) {
        Impl* self = mounted(context); if (!self) return StorageResult::Error;
        return self->status(SPIFFS_remove(&self->fs, path));
    }
    FileIo io() {
        FileIo result;
        result.context=this; result.stat=stat; result.open_read=open_read; result.open_write=open_write;
        result.seek=seek; result.read=read; result.write=write; result.sync=sync; result.close=close;
        result.rename=rename; result.remove=remove;
        return result;
    }
};

SpiffsStorage::SpiffsStorage(const char* prefix) noexcept : SpiffsStorage(linked_partition(),prefix) {}
SpiffsStorage::SpiffsStorage(SpiffsGeometry geometry, const char* prefix) noexcept
    : impl_(new (std::nothrow) Impl(geometry)), storage_(impl_ ? impl_->io() : FileIo(),prefix) {}
SpiffsStorage::~SpiffsStorage() { delete impl_; }
bool SpiffsStorage::begin() { return impl_ && storage_.valid() && impl_->begin(); }
void SpiffsStorage::end() noexcept { if (impl_) SPIFFS_unmount(&impl_->fs); }
bool SpiffsStorage::mounted() const noexcept { return impl_ && SPIFFS_mounted(&impl_->fs); }
std::int32_t SpiffsStorage::last_error() const noexcept { return impl_ ? impl_->error : SPIFFS_ERR_INTERNAL; }
Storage SpiffsStorage::binding() noexcept { return storage_.binding(); }

}}}
