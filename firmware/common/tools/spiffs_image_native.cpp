// Host formatter using the pinned, unmodified SPIFFS C implementation.
// The Python owner verifies SDK identity, namespace, geometry and on-flash ABI.
#include "spiffs/spiffs.h"
#include "spiffs/spiffs_nucleus.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<u8_t> flash;
u32_t physical_start = 0;
std::size_t reads = 0, writes = 0, erases = 0;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool in_range(u32_t address, u32_t size) {
    return address >= physical_start && address - physical_start <= flash.size() &&
           size <= flash.size() - (address - physical_start);
}
#if SPIFFS_HAL_CALLBACK_EXTRA
#define FS_ARGUMENT spiffs*,
#else
#define FS_ARGUMENT
#endif
s32_t read_flash(FS_ARGUMENT u32_t address, u32_t size, u8_t* data) {
    ++reads;
    if (!in_range(address, size)) return SPIFFS_ERR_INTERNAL;
    std::memcpy(data, flash.data() + address - physical_start, size);
    return SPIFFS_OK;
}
s32_t write_flash(FS_ARGUMENT u32_t address, u32_t size, u8_t* data) {
    ++writes;
    if (!in_range(address, size)) return SPIFFS_ERR_INTERNAL;
    for (u32_t index = 0; index < size; ++index)
        flash[address - physical_start + index] &= data[index];
    return SPIFFS_OK;
}
s32_t erase_flash(FS_ARGUMENT u32_t address, u32_t size) {
    ++erases;
    if (!in_range(address, size) || address % 4096 || size % 4096)
        return SPIFFS_ERR_INTERNAL;
    std::fill(flash.begin() + address - physical_start,
              flash.begin() + address - physical_start + size, 0xff);
    return SPIFFS_OK;
}
#undef FS_ARGUMENT

struct File {
    std::string name;
    std::vector<u8_t> data;
};
bool valid_name(const std::string& name) {
    if (name.compare(0, 4, "/pb/") || name.size() < 5 || name.size() > 27)
        return false;
    const std::string key = name.substr(4);
    if (key.front() == '.' || key.back() == '.' || key.find("..") != std::string::npos)
        return false;
    if (key.size() >= 4 && (key.compare(key.size() - 4, 4, ".tmp") == 0 ||
                           key.compare(key.size() - 4, 4, ".bak") == 0)) return false;
    for (char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
    return true;
}
std::vector<u8_t> load(const char* path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(stream), "Cannot open staged input");
    const std::streamoff length = stream.tellg();
    require(length >= 0 && static_cast<std::size_t>(length) <= flash.size(),
            "Staged input exceeds partition");
    std::vector<u8_t> data(static_cast<std::size_t>(length));
    stream.seekg(0);
    if (!data.empty()) stream.read(reinterpret_cast<char*>(data.data()), data.size());
    require(static_cast<bool>(stream), "Cannot read complete staged input");
    return data;
}
struct Filesystem {
    spiffs fs{};
    spiffs_config configuration{};
    std::vector<u8_t> work, descriptors, cache;
    Filesystem(u32_t size, u32_t block) {
        configuration.phys_addr = physical_start;
        configuration.phys_size = size;
        configuration.phys_erase_block = 4096;
        configuration.log_page_size = 256;
        configuration.log_block_size = block;
        configuration.hal_read_f = read_flash;
        configuration.hal_write_f = write_flash;
        configuration.hal_erase_f = erase_flash;
        fs.cfg.log_page_size = 256;
        work.resize(512);
        descriptors.resize(SPIFFS_buffer_bytes_for_filedescs(&fs, 4));
        cache.resize(SPIFFS_buffer_bytes_for_cache(&fs, 4));
    }
    ~Filesystem() { SPIFFS_unmount(&fs); }
    s32_t mount() {
        return SPIFFS_mount(&fs, &configuration, work.data(), descriptors.data(),
                            descriptors.size(), cache.data(), cache.size(), nullptr);
    }
    void create(const File& input) {
        const spiffs_file file = SPIFFS_open(&fs, input.name.c_str(),
            SPIFFS_CREAT | SPIFFS_EXCL | SPIFFS_WRONLY, 0);
        require(file >= 0, "SPIFFS create failed");
        for (std::size_t offset = 0; offset < input.data.size();) {
            const std::size_t size = std::min<std::size_t>(256, input.data.size() - offset);
            const s32_t wrote = SPIFFS_write(&fs, file,
                const_cast<u8_t*>(input.data.data() + offset), size);
            require(wrote == static_cast<s32_t>(size), "SPIFFS short or failed write");
            offset += size;
        }
        // This SDK may return the positive number of bytes flushed from its
        // write cache. Only a negative result is a flush failure.
        require(SPIFFS_fflush(&fs, file) >= SPIFFS_OK, "SPIFFS flush failed");
#if SPIFFS_OBJ_META_LEN
        // The SDK uses four metadata bytes for mtime. A staged image has the
        // fixed epoch zero; the formatter never consults the host clock.
        u8_t metadata[SPIFFS_OBJ_META_LEN] = {};
        require(SPIFFS_fupdate_meta(&fs, file, metadata) == SPIFFS_OK,
                "SPIFFS metadata update failed");
#endif
        require(SPIFFS_close(&fs, file) == SPIFFS_OK, "SPIFFS close failed");
    }
    void verify(const std::vector<File>& files) {
        std::vector<std::string> actual;
        spiffs_DIR directory{};
        require(SPIFFS_opendir(&fs, "/", &directory) != nullptr,
                "SPIFFS directory open failed");
        spiffs_dirent entry{};
        while (SPIFFS_readdir(&directory, &entry))
            actual.push_back(reinterpret_cast<const char*>(entry.name));
        require(SPIFFS_closedir(&directory) == SPIFFS_OK,
                "SPIFFS directory close failed");
        std::sort(actual.begin(), actual.end());
        require(actual.size() == files.size(), "SPIFFS unexpected file count");
        for (std::size_t index = 0; index < files.size(); ++index) {
            const File& expected = files[index];
            require(actual[index] == expected.name, "SPIFFS unexpected file name");
            spiffs_stat status{};
            require(SPIFFS_stat(&fs, expected.name.c_str(), &status) == SPIFFS_OK &&
                    status.size == expected.data.size(), "SPIFFS file length mismatch");
            const spiffs_file file = SPIFFS_open(&fs, expected.name.c_str(), SPIFFS_RDONLY, 0);
            require(file >= 0, "SPIFFS verification open failed");
            u8_t buffer[257];
            for (std::size_t offset = 0; offset < expected.data.size();) {
                const std::size_t size = std::min<std::size_t>(sizeof(buffer), expected.data.size() - offset);
                require(SPIFFS_read(&fs, file, buffer, size) == static_cast<s32_t>(size) &&
                        !std::memcmp(buffer, expected.data.data() + offset, size),
                        "SPIFFS readback mismatch");
                offset += size;
            }
            require(SPIFFS_read(&fs, file, buffer, 1) == 0, "SPIFFS unexpected trailing data");
            require(SPIFFS_close(&fs, file) == SPIFFS_OK, "SPIFFS verification close failed");
        }
    }
};
void layout() {
    std::printf("\"layout\":{\"little_endian\":true,\"object_id_bytes\":%zu,"
        "\"block_index_bytes\":%zu,\"page_index_bytes\":%zu,\"span_index_bytes\":%zu,"
        "\"page_header\":{\"size\":%zu,\"obj_id\":%zu,\"span_ix\":%zu,\"flags\":%zu},"
        "\"object_header\":{\"size\":%zu,\"p_hdr\":%zu,\"_align\":%zu,"
        "\"file_size\":%zu,\"type\":%zu,\"name\":%zu,\"name_bytes\":%u,\"metadata_bytes\":%u",
        sizeof(spiffs_obj_id), sizeof(spiffs_block_ix), sizeof(spiffs_page_ix), sizeof(spiffs_span_ix),
        sizeof(spiffs_page_header), offsetof(spiffs_page_header, obj_id),
        offsetof(spiffs_page_header, span_ix), offsetof(spiffs_page_header, flags),
        sizeof(spiffs_page_object_ix_header), offsetof(spiffs_page_object_ix_header, p_hdr),
        offsetof(spiffs_page_object_ix_header, _align), offsetof(spiffs_page_object_ix_header, size),
        offsetof(spiffs_page_object_ix_header, type), offsetof(spiffs_page_object_ix_header, name),
        SPIFFS_OBJ_NAME_LEN, SPIFFS_OBJ_META_LEN);
#if SPIFFS_OBJ_META_LEN
    std::printf(",\"meta\":%zu", offsetof(spiffs_page_object_ix_header, meta));
#endif
    std::printf("},\"index_header\":{\"size\":%zu,\"p_hdr\":%zu,\"_align\":%zu},"
                "\"magic_length\":%u,\"gc_max_runs\":%u}",
        sizeof(spiffs_page_object_ix), offsetof(spiffs_page_object_ix, p_hdr),
        offsetof(spiffs_page_object_ix, _align), SPIFFS_USE_MAGIC_LENGTH, SPIFFS_GC_MAX_RUNS);
}
}
int main(int argc, char** argv) {
    try {
        const u16_t endian = 1;
        require(*reinterpret_cast<const u8_t*>(&endian) == 1, "Little-endian host required");
        if (argc == 2 && std::strcmp(argv[1], "--layout") == 0) {
            std::printf("{"); layout(); std::printf("}\n"); return 0;
        }
        const bool readback = argc >= 2 && std::strcmp(argv[1], "--readback") == 0;
        if (readback) { --argc; ++argv; }
        require(argc >= 5 && (argc - 5) % 2 == 0, "Invalid formatter invocation");
        const u32_t size = std::stoul(argv[1]), block = std::stoul(argv[2]);
        physical_start = std::stoul(argv[3]);
        require(size >= block * 3 && size <= 4 * 1024 * 1024 && size % 4096 == 0 &&
                (block == 4096 || block == 8192) && physical_start % 4096 == 0,
                "Invalid formatter geometry");
        flash.assign(size, 0xff);
        std::vector<File> inputs;
        for (int index = 5; index < argc; index += 2) {
            File file; file.name = argv[index];
            require(valid_name(file.name), "Invalid staged logical name");
            file.data = load(argv[index + 1]); inputs.push_back(file);
        }
        std::sort(inputs.begin(), inputs.end(), [](const File& a, const File& b) { return a.name < b.name; });
        for (std::size_t index = 1; index < inputs.size(); ++index)
            require(inputs[index - 1].name != inputs[index].name, "Duplicate staged logical name");
        if (readback) {
            flash = load(argv[4]);
            require(flash.size() == size, "Wrong readback image size");
            Filesystem existing(size, block);
            require(existing.mount() == SPIFFS_OK, "Migrated filesystem did not mount");
            existing.verify(inputs);
            return 0;
        }
        u32_t total = 0, used = 0;
        {
            Filesystem filesystem(size, block);
            require(filesystem.mount() == SPIFFS_ERR_NOT_A_FS, "Blank partition unexpectedly mounted");
            SPIFFS_unmount(&filesystem.fs);
            require(SPIFFS_format(&filesystem.fs) == SPIFFS_OK, "Explicit host formatting failed");
            require(filesystem.mount() == SPIFFS_OK, "Formatted image did not mount");
            for (const File& input : inputs) filesystem.create(input);
            require(SPIFFS_check(&filesystem.fs) == SPIFFS_OK, "Formatted image integrity check failed");
            filesystem.verify(inputs);
        }
        {
            Filesystem filesystem(size, block);
            require(filesystem.mount() == SPIFFS_OK, "Formatted image did not remount");
            filesystem.verify(inputs);
            require(SPIFFS_info(&filesystem.fs, &total, &used) == SPIFFS_OK,
                    "SPIFFS information query failed");
        }
        std::ofstream output(argv[4], std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(output), "Cannot open image output");
        output.write(reinterpret_cast<const char*>(flash.data()), flash.size());
        output.close(); require(static_cast<bool>(output), "Cannot write complete image");
        std::printf("{\"files\":%zu,\"image_bytes\":%zu,\"spiffs_total_bytes\":%u,"
                    "\"spiffs_used_bytes\":%u,\"hal_reads\":%zu,\"hal_writes\":%zu,"
                    "\"hal_erases\":%zu,\"remount_verified\":true,",
                    inputs.size(), flash.size(), total, used, reads, writes, erases);
        layout(); std::printf("}\n"); return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "SPIFFS image: %s\n", error.what()); return 1;
    }
}
