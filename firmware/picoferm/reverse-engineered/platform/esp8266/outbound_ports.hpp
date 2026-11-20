#pragma once
#include "picoferm/data.hpp"
#include "picoferm/transport.hpp"
#include <ESP8266WiFi.h>
namespace picoferm { namespace oem {
class ArduinoOutboundClient final:public OutboundClient {
public:
    bool connect_ip(std::uint32_t address,unsigned port) override;
    int available() override;int read() override;bool connected() override;
    void write(const char* bytes,std::size_t length) override;void stop() override;
private:WiFiClient client_;
};
class ArduinoRuntime final:public Runtime,public Formatter {
public:void delay_ms(std::uint32_t duration) override;void decimal(float value,char* destination,std::size_t capacity) override;
};
}}
