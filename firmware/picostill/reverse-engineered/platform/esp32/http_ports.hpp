#pragma once
#include "picostill/http.hpp"
#include "arduino_ports.hpp"
#include <WiFi.h>

namespace picostill { namespace oem {
class ArduinoHttpStream final:public HttpStream {
public:
    explicit ArduinoHttpStream(Client& client) noexcept:client_(client) {}
    bool connected() override;
    int available() override;
    int read() override;
    std::size_t write(const char* bytes,std::size_t size) override;
    void flush() override;
    void stop() override;
    void decimal(double value,unsigned digits) override;
    void unsigned_number(std::uint32_t value) override;
private:
    Client& client_;
};
class ArduinoHttpListener final:public HttpListener {
public:
    ArduinoHttpListener():server_(80),stream_(client_) {}
    void listen(unsigned port) override;
    HttpStream* accept() override;
    void release(HttpStream& stream) override;
private:
    WiFiServer server_;
    WiFiClient client_;
    ArduinoHttpStream stream_;
};
class ArduinoHttpRuntime final:public HttpRuntime {
public:
    explicit ArduinoHttpRuntime(ArduinoPorts& ports,HardwareSerial& serial=Serial) noexcept:ports_(ports),serial_(serial) {}
    std::uint32_t millis() override;
    void cooperate() override;
    void variable(const char* scope,const char* name,const char* value) override;
    void integer(const char* scope,const char* name,std::uint32_t value) override;
    void message(const char* value) override;
private:
    ArduinoPorts& ports_;
    HardwareSerial& serial_;
};
}} // namespace picostill::oem
