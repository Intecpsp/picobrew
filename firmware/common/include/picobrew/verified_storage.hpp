// Bounded, checked file transactions shared by both standalone SDK ports.
#pragma once
#include "picobrew/device_services.hpp"

namespace picobrew { namespace standalone {

using FileHandle = std::intptr_t;
constexpr FileHandle invalid_file_handle = -1;

// Paths are logical SPIFFS names, never host paths. A VFS backend prepends its
// mount point. Missing is reserved for an absent object; every other failure
// must be Error. open_write creates/truncates a file. read/write may make a
// partial transfer, but must report the actual count even when returning Error.
// close consumes the handle on every outcome. rename must not replace an
// existing destination. The owner serializes operations on this filesystem.
struct FileIo {
    void* context = nullptr;
    StorageResult (*stat)(void*, const char* path, std::size_t& size) = nullptr;
    StorageResult (*open_read)(void*, const char* path, FileHandle&) = nullptr;
    StorageResult (*open_write)(void*, const char* path, FileHandle&) = nullptr;
    bool (*seek)(void*, FileHandle, std::size_t offset) = nullptr;
    StorageResult (*read)(void*, FileHandle, std::uint8_t*, std::size_t,
                          std::size_t& transferred) = nullptr;
    StorageResult (*write)(void*, FileHandle, const std::uint8_t*, std::size_t,
                           std::size_t& transferred) = nullptr;
    bool (*sync)(void*, FileHandle) = nullptr;
    bool (*close)(void*, FileHandle) = nullptr;
    StorageResult (*rename)(void*, const char* from, const char* to) = nullptr;
    StorageResult (*remove)(void*, const char* path) = nullptr;
};

class VerifiedStorage {
public:
    // The default namespace is separate from OEM filesystem objects. Prefix
    // is copied; it must begin/end in '/', contain only ASCII alphanumerics,
    // '_' and '/', and be 1..4 bytes. Engine keys are 1..23 bytes, ASCII
    // alphanumerics, '_' and '.', with no leading/trailing dot or '..'. Keys
    // ending in the reserved transaction suffixes '.tmp' or '.bak' are rejected.
    // Including '.tmp' or '.bak' and NUL, every name fits SPIFFS' 32 bytes.
    explicit VerifiedStorage(FileIo io, const char* prefix = "/pb/") noexcept;
    VerifiedStorage(const VerifiedStorage&) = delete;
    VerifiedStorage& operator=(const VerifiedStorage&) = delete;
    bool valid() const noexcept;
    Storage binding() noexcept;

    StorageResult read(const char* name, std::size_t offset,
                       std::uint8_t* destination, std::size_t capacity,
                       std::size_t& bytes_read, std::size_t& total_bytes);
    // Writes/verifies .tmp; retains the prior main as .bak; promotes/verifies
    // the new main; then removes .bak. Each callback result is checked. This is
    // a recoverable multi-step transaction, not a single atomic filesystem
    // rename. A reported error can occur after promotion. On read, .bak is
    // consulted ONLY if main is absent, never after main read/stat corruption
    // or I/O failure. The engine separately verifies CRCs/alternate banks.
    StorageResult write_atomic(const char* name, const std::uint8_t* data,
                               std::size_t length);
    StorageResult remove(const char* name);

private:
    FileIo io_;
    char prefix_[5] = {};
    bool valid_ = false;
    bool paths(const char*, char (&main)[32], char (&temp)[32],
               char (&backup)[32]) const noexcept;
    bool verify(const char*, const std::uint8_t*, std::size_t);
    bool erase_if_present(const char*);
    void restore_backup(const char* main, const char* backup,
                        bool remove_main) noexcept;
};

}}
