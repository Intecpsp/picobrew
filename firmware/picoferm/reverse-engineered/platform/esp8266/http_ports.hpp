#pragma once
#include "picoferm/http.hpp"
#include <DNSServer.h>
#include <ESP8266WiFi.h>
namespace picoferm { namespace oem {
class ArduinoHttpStream final:public HttpStream {
public:
    explicit ArduinoHttpStream(WiFiClient& client) noexcept:client_(client){}
    bool connected() override;int available() override;int read() override;
    std::size_t write(const char* bytes,std::size_t length) override;
    std::size_t write_flash(const char* bytes,std::size_t length) override;
    void flush() override;
private:WiFiClient& client_;
};
class ArduinoHttpListener final:public HttpListener {
public:
    ArduinoHttpListener():server_(80),stream_(client_){}
    void listen(unsigned port) override;void process_dns() override;
    HttpStream* accept() override;void release(HttpStream& stream) override;
    void set_dns_ttl(unsigned value);
    void set_dns_reply_code(unsigned value);
    bool start_dns(unsigned port,const char* domain,std::uint32_t address);
    void stop_dns();
private:DNSServer dns_;WiFiServer server_;WiFiClient client_;ArduinoHttpStream stream_;
};
}}
