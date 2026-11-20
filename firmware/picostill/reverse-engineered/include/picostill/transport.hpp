#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace picostill { namespace oem {
// Concrete ESP32 adapter uses a distinct WiFiClient per backend/updater.
struct OutboundClient {
    virtual ~OutboundClient() = default;
    virtual bool connect_ip(std::uint32_t address,unsigned port) = 0;
    virtual bool connect_host(const char* host,unsigned port) = 0;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual bool connected() = 0;
    virtual void write(const char* bytes,std::size_t length) = 0;
    virtual std::string read_line() = 0;
    virtual std::size_t read_bytes(std::uint8_t* buffer,std::size_t size) = 0;
    virtual void stop() = 0;
    virtual void flush() = 0;
};
}} // namespace picostill::oem
