#include "picobrew/verified_storage.hpp"
#include <cstring>

namespace picobrew { namespace standalone {
namespace {
bool alphanumeric(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}
bool exists_or_missing(StorageResult result) {
    return result == StorageResult::Ok || result == StorageResult::Missing;
}
StorageResult read_binding(void* context, const char* name, std::size_t offset,
                           std::uint8_t* destination, std::size_t capacity,
                           std::size_t& copied, std::size_t& total) {
    return static_cast<VerifiedStorage*>(context)->read(name, offset, destination,
                                                       capacity, copied, total);
}
StorageResult write_binding(void* context, const char* name,
                            const std::uint8_t* data, std::size_t length) {
    return static_cast<VerifiedStorage*>(context)->write_atomic(name, data, length);
}
StorageResult remove_binding(void* context, const char* name) {
    return static_cast<VerifiedStorage*>(context)->remove(name);
}
}

VerifiedStorage::VerifiedStorage(FileIo io, const char* prefix) noexcept : io_(io) {
    if (!prefix || !io.stat || !io.open_read || !io.open_write || !io.seek ||
        !io.read || !io.write || !io.sync || !io.close || !io.rename || !io.remove)
        return;
    std::size_t n = 0;
    while (n < sizeof(prefix_) && prefix[n]) {
        if (!alphanumeric(prefix[n]) && prefix[n] != '_' && prefix[n] != '/')
            return;
        ++n;
    }
    if (!n || n >= sizeof(prefix_) || prefix[0] != '/' || prefix[n - 1] != '/')
        return;
    std::memcpy(prefix_, prefix, n + 1);
    valid_ = true;
}

bool VerifiedStorage::valid() const noexcept { return valid_; }

Storage VerifiedStorage::binding() noexcept {
    Storage result;
    result.context = this;
    result.read = read_binding;
    result.write_atomic = write_binding;
    result.remove = remove_binding;
    return result;
}

bool VerifiedStorage::paths(const char* name, char (&main)[32], char (&temp)[32],
                            char (&backup)[32]) const noexcept {
    if (!valid_ || !name || !*name || *name == '.') return false;
    std::size_t n = 0;
    while (n < 24 && name[n]) {
        if ((!alphanumeric(name[n]) && name[n] != '_' && name[n] != '-' && name[n] != '.') ||
            (name[n] == '.' && n && name[n - 1] == '.')) return false;
        ++n;
    }
    if (!n || n > 23 || name[n - 1] == '.') return false;
    if (n >= 4 && (!std::strcmp(name + n - 4, ".tmp") ||
                   !std::strcmp(name + n - 4, ".bak"))) return false;
    const std::size_t p = std::strlen(prefix_);
    std::memcpy(main, prefix_, p);
    std::memcpy(main + p, name, n + 1);
    std::memcpy(temp, main, p + n);
    std::memcpy(backup, main, p + n);
    std::memcpy(temp + p + n, ".tmp", 5);
    std::memcpy(backup + p + n, ".bak", 5);
    return true;
}

StorageResult VerifiedStorage::read(const char* name, std::size_t offset,
                                   std::uint8_t* destination, std::size_t capacity,
                                   std::size_t& bytes_read, std::size_t& total_bytes) {
    bytes_read = 0;
    total_bytes = 0;
    char main[32], temp[32], backup[32];
    if (!paths(name, main, temp, backup) || (capacity && !destination))
        return StorageResult::Error;
    const char* selected = main;
    StorageResult result = io_.stat(io_.context, selected, total_bytes);
    if (result == StorageResult::Missing) {
        selected = backup;
        result = io_.stat(io_.context, selected, total_bytes);
    }
    if (result != StorageResult::Ok) {
        total_bytes = 0;
        return result;
    }
    if (!capacity || offset >= total_bytes) return StorageResult::Ok;
    const std::size_t remaining = total_bytes - offset;
    const std::size_t wanted = capacity < remaining ? capacity : remaining;
    FileHandle file = invalid_file_handle;
    if (io_.open_read(io_.context, selected, file) != StorageResult::Ok ||
        file == invalid_file_handle) return StorageResult::Error;
    bool ok = io_.seek(io_.context, file, offset);
    while (ok && bytes_read < wanted) {
        const std::size_t need = wanted - bytes_read;
        const std::size_t chunk = need < 256 ? need : 256;
        std::size_t got = 0;
        result = io_.read(io_.context, file, destination + bytes_read, chunk, got);
        if (got > chunk) { ok = false; break; }
        bytes_read += got;
        if (result != StorageResult::Ok || !got) ok = false;
    }
    if (!io_.close(io_.context, file)) ok = false;
    return ok ? StorageResult::Ok : StorageResult::Error;
}

bool VerifiedStorage::verify(const char* name, const std::uint8_t* data,
                             std::size_t length) {
    std::size_t actual = 0;
    if (io_.stat(io_.context, name, actual) != StorageResult::Ok || actual != length)
        return false;
    FileHandle file = invalid_file_handle;
    if (io_.open_read(io_.context, name, file) != StorageResult::Ok ||
        file == invalid_file_handle) return false;
    std::uint8_t buffer[256];
    std::size_t offset = 0;
    bool ok = true;
    while (ok && offset < length) {
        const std::size_t rest = length - offset;
        const std::size_t chunk = rest < sizeof(buffer) ? rest : sizeof(buffer);
        std::size_t got = 0;
        const StorageResult result = io_.read(io_.context, file, buffer, chunk, got);
        if (result != StorageResult::Ok || !got || got > chunk ||
            std::memcmp(buffer, data + offset, got)) ok = false;
        else offset += got;
    }
    if (ok) {
        std::size_t got = 0;
        ok = io_.read(io_.context, file, buffer, 1, got) == StorageResult::Ok && !got;
    }
    if (!io_.close(io_.context, file)) ok = false;
    return ok;
}

bool VerifiedStorage::erase_if_present(const char* path) {
    return exists_or_missing(io_.remove(io_.context, path));
}

void VerifiedStorage::restore_backup(const char* main, const char* backup,
                                     bool remove_main) noexcept {
    std::size_t size = 0;
    // An error is never treated as absence. Leave the backup intact if recovery
    // cannot establish whether the main name is available.
    if (io_.stat(io_.context, backup, size) != StorageResult::Ok) return;
    const StorageResult current = io_.stat(io_.context, main, size);
    if (current == StorageResult::Error) return;
    if (current == StorageResult::Ok && (!remove_main || !erase_if_present(main)))
        return;
    (void)io_.rename(io_.context, backup, main);
}

StorageResult VerifiedStorage::write_atomic(const char* name,
                                            const std::uint8_t* data,
                                            std::size_t length) {
    char main[32], temp[32], backup[32];
    if (!paths(name, main, temp, backup) || (length && !data))
        return StorageResult::Error;
    std::size_t size = 0;
    StorageResult main_state = io_.stat(io_.context, main, size);
    StorageResult backup_state = io_.stat(io_.context, backup, size);
    if (!exists_or_missing(main_state) || !exists_or_missing(backup_state))
        return StorageResult::Error;
    if (main_state == StorageResult::Missing && backup_state == StorageResult::Ok) {
        if (io_.rename(io_.context, backup, main) != StorageResult::Ok)
            return StorageResult::Error;
        main_state = StorageResult::Ok;
        backup_state = StorageResult::Missing;
    }
    if (!erase_if_present(temp)) return StorageResult::Error;
    FileHandle file = invalid_file_handle;
    if (io_.open_write(io_.context, temp, file) != StorageResult::Ok ||
        file == invalid_file_handle) return StorageResult::Error;
    std::size_t offset = 0;
    bool ok = true;
    while (ok && offset < length) {
        const std::size_t rest = length - offset;
        const std::size_t chunk = rest < 256 ? rest : 256;
        std::size_t wrote = 0;
        const StorageResult result = io_.write(io_.context, file, data + offset,
                                               chunk, wrote);
        if (result != StorageResult::Ok || !wrote || wrote > chunk) ok = false;
        else offset += wrote;
    }
    if (ok && !io_.sync(io_.context, file)) ok = false;
    if (!io_.close(io_.context, file)) ok = false;
    if (!ok || !verify(temp, data, length)) {
        // A failed partial write is never a recovery generation. Reclaim its
        // scratch space when possible so it cannot strand the history/catalogue
        // update needed after a full-filesystem restart. The original failure
        // remains the result even if cleanup itself fails.
        (void)erase_if_present(temp);
        return StorageResult::Error;
    }
    if (main_state == StorageResult::Ok) {
        if (backup_state == StorageResult::Ok && !erase_if_present(backup))
            return StorageResult::Error;
        if (io_.rename(io_.context, main, backup) != StorageResult::Ok)
            return StorageResult::Error;
    }
    if (io_.rename(io_.context, temp, main) != StorageResult::Ok) {
        restore_backup(main, backup, false);
        return StorageResult::Error;
    }
    if (!verify(main, data, length)) {
        if (main_state == StorageResult::Ok) restore_backup(main, backup, true);
        else (void)erase_if_present(main);
        return StorageResult::Error;
    }
    if (!erase_if_present(backup)) return StorageResult::Error;
    return StorageResult::Ok;
}

StorageResult VerifiedStorage::remove(const char* name) {
    char main[32], temp[32], backup[32];
    if (!paths(name, main, temp, backup)) return StorageResult::Error;
    std::size_t size = 0;
    const StorageResult primary = io_.stat(io_.context, main, size);
    const StorageResult recovery = io_.stat(io_.context, backup, size);
    if (!exists_or_missing(primary) || !exists_or_missing(recovery))
        return StorageResult::Error;
    // Remove backup before main so an interrupted deletion cannot resurrect it.
    if (!erase_if_present(temp) || !erase_if_present(backup) || !erase_if_present(main))
        return StorageResult::Error;
    return primary == StorageResult::Missing && recovery == StorageResult::Missing
           ? StorageResult::Missing : StorageResult::Ok;
}

}}
