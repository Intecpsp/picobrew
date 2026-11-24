// Minimal host declarations for the pinned SDK boundary used by FactoryFlash.
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
enum { ESP_OK=0, ESP_IMAGE_VERIFY=0 };
enum esp_partition_type_t { ESP_PARTITION_TYPE_APP=0, ESP_PARTITION_TYPE_DATA=1 };
enum esp_partition_subtype_t { ESP_PARTITION_SUBTYPE_APP_FACTORY=0, ESP_PARTITION_SUBTYPE_APP_OTA_0=16,
    ESP_PARTITION_SUBTYPE_DATA_OTA=0, ESP_PARTITION_SUBTYPE_DATA_NVS=2, ESP_PARTITION_SUBTYPE_DATA_SPIFFS=130,
    ESP_PARTITION_SUBTYPE_ANY=255 };
struct esp_partition_t { esp_partition_type_t type; esp_partition_subtype_t subtype; unsigned address, size; char label[17]; bool encrypted; };
typedef const esp_partition_t* esp_partition_iterator_t;
typedef unsigned esp_ota_handle_t;
struct esp_partition_pos_t { unsigned offset, size; };
struct esp_image_header_t { bool hash_appended; unsigned char segment_count; unsigned char padding[22]; };
struct esp_image_segment_header_t { unsigned load_addr, data_len; };
struct esp_app_desc_t { unsigned char data[256]; };
struct esp_image_metadata_t { esp_image_header_t image; esp_image_segment_header_t segments[16]; unsigned image_len; };
esp_partition_iterator_t esp_partition_find(esp_partition_type_t,esp_partition_subtype_t,const char*);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t);
const esp_partition_t* esp_partition_find_first(esp_partition_type_t,esp_partition_subtype_t,const char*);
const esp_partition_t* esp_ota_get_running_partition();
const esp_partition_t* esp_ota_get_boot_partition();
esp_err_t esp_image_verify(int,const esp_partition_pos_t*,esp_image_metadata_t*);
esp_err_t esp_partition_read(const esp_partition_t*,unsigned,void*,size_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
esp_err_t esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t,const void*,size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
