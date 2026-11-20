#pragma once
#include <cstddef>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=0x103;
struct esp_vfs_spiffs_conf_t {const char* base_path;const char* partition_label;std::size_t max_files;bool format_if_mount_failed;};
esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t*);
bool esp_spiffs_mounted(const char*);
esp_err_t esp_spiffs_info(const char*,std::size_t*,std::size_t*);
