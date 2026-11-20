#pragma once
#include "picostill/transport.hpp"
#include "picostill/update.hpp"
#include <WiFi.h>
#include <Update.h>
namespace picostill { namespace oem {
class ArduinoOutboundClient final:public OutboundClient {
public:
    bool connect_ip(std::uint32_t address,unsigned port) override;
    bool connect_host(const char* host,unsigned port) override;
    int available() override;
    int read() override;
    bool connected() override;
    void write(const char* bytes,std::size_t length) override;
    std::string read_line() override;
    std::size_t read_bytes(std::uint8_t* buffer,std::size_t size) override;
    void stop() override;
    void flush() override;
private:
    WiFiClient client_;
};
class ArduinoUpdatePorts final:public UpdatePorts {
public:
    explicit ArduinoUpdatePorts(UpdateClass& update=Update,HardwareSerial& serial=Serial) noexcept:update_(update),serial_(serial) {}
    bool begin(std::uint32_t size) override;
    std::size_t write(std::uint8_t* bytes,std::size_t size) override;
    bool end() override;
    bool finished() override;
    unsigned error() override;
    std::string md5() override;
    void console(const char* message,bool line) override;
private:
    UpdateClass& update_;HardwareSerial& serial_;
};
}} // namespace picostill::oem
