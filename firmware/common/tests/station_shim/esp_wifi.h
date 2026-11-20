#pragma once
#include <cstdint>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_WIFI_NOT_CONNECT 0x300f
#define WIFI_STORAGE_RAM 1
#define WIFI_IF_STA 0
struct wifi_config_t { struct { std::uint8_t ssid[32],password[64],reserved[12]; } sta; };
int esp_wifi_set_storage(int);
int esp_wifi_set_auto_connect(bool);
int esp_wifi_disconnect();
int esp_wifi_set_config(int,const wifi_config_t*);
int esp_wifi_connect();
