#pragma once
namespace picobrew { namespace standalone { namespace esp32 {
// Bounded 32-byte SSID/64-hex PSK support without Arduino 1.0.1's 31-byte
// SSID limit. Configures RAM only and requests association without waiting.
// Returns the exact first SDK error (0 means requested, not connected).
int connect_station(const char* ssid, const char* password) noexcept;
}}}
