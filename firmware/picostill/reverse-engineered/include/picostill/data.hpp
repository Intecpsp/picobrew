// Recovered PicoStill identity and credential record; no original pointers are retained.
#pragma once
#include "picostill/core.hpp"
#include <cstddef>
namespace picostill { namespace oem {
bool bounded_copy(char* destination,std::size_t capacity,const char* source,Diagnostics& diagnostics);
char hex_digit(std::uint8_t value,Diagnostics& diagnostics);
bool encode_hex(char* destination,std::size_t capacity,const std::uint8_t* source,std::size_t count,Diagnostics& diagnostics);
struct DeviceData {
    std::array<char,16> version = {{0}};
    std::array<char,32> website = {{0}};
    std::array<char,16> uid = {{0}};
    std::array<char,32> ap_ssid = {{0}};
    std::array<char,32> ssid = {{0}};
    std::array<char,64> password = {{0}};
    std::array<char,16> token = {{0}};
    void initialize(const char* firmware,const char* host,Diagnostics& diagnostics);
    void set_uid(const char* mac,Diagnostics& diagnostics);
    void set_ssid(const char* value,Diagnostics& diagnostics);
    void set_password(const char* value,Diagnostics& diagnostics);
    void update_token(Diagnostics& diagnostics);
};
}} // namespace picostill::oem
