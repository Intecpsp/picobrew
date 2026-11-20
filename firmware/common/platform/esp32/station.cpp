#include "station.hpp"
#include <WiFi.h>
#include <esp_wifi.h>
#include <tcpip_adapter.h>
#include <cstring>

namespace picobrew { namespace standalone { namespace esp32 {
namespace {
std::size_t length(const char* text, std::size_t limit) {
    if (!text) return 0;
    std::size_t count = 0; while (count < limit && text[count]) ++count; return count;
}
bool hex(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
}
int connect_station(const char* ssid, const char* password) noexcept {
    const std::size_t name_size = length(ssid, 33), pass_size = length(password, 65);
    if (!name_size || name_size > 32 || pass_size > 64 || (pass_size && pass_size < 8)) return ESP_ERR_INVALID_ARG;
    for (std::size_t i = 0; i < pass_size; ++i) {
        const auto c = static_cast<unsigned char>(password[i]);
        if (c < 32 || c > 126 || (pass_size == 64 && !hex(c))) return ESP_ERR_INVALID_ARG;
    }
    WiFi.persistent(false);
    if (!WiFi.enableSTA(true)) return ESP_FAIL;
    int result = esp_wifi_set_storage(WIFI_STORAGE_RAM); if (result) return result;
    // The pinned SDK still documents and implements this call; it marks it
    // deprecated but provides no equivalent setter in this historical API.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    result = esp_wifi_set_auto_connect(false);
#pragma GCC diagnostic pop
    if (result) return result;
    result = esp_wifi_disconnect(); if (result && result != ESP_ERR_WIFI_NOT_CONNECT) return result;
    wifi_config_t configuration{};
    std::memcpy(configuration.sta.ssid, ssid, name_size);
    if (pass_size) std::memcpy(configuration.sta.password, password, pass_size);
    result = esp_wifi_set_config(WIFI_IF_STA, &configuration);
    // Do not retain an extra plaintext credential copy on the task stack.
    volatile unsigned char* clear = reinterpret_cast<volatile unsigned char*>(&configuration);
    for (std::size_t i = 0; i < sizeof(configuration); ++i) clear[i] = 0;
    if (result) return result;
    result = tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
    if (result && result != ESP_ERR_TCPIP_ADAPTER_DHCP_ALREADY_STARTED) return result;
    return esp_wifi_connect();
}
}}}
