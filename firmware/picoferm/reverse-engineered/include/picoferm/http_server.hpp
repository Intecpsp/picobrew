#pragma once
#include "picoferm/http.hpp"
#include "picoferm/transport.hpp"
namespace picoferm { namespace oem {
struct HttpDevice {
    virtual ~HttpDevice()=default;
    virtual CalibrationStatus calibration()=0;
    virtual void configure(char* result,std::size_t capacity,const char* ssid,const char* password)=0;
    virtual void factory_reset()=0;
    virtual void calibrate()=0;
    virtual int scan_count()=0;
    virtual const char* scan_ssid(unsigned index)=0;
};
class HttpServer {
public:
    HttpServer(HttpListener& listener,Ports& ports,HardwarePorts& hardware,Runtime& runtime,Formatter& formatter,
               DeviceData& data,PersistentData& stored,HttpDevice& device,std::uint32_t request_timeout=1000) noexcept
        :timeout(request_timeout),listener_(listener),ports_(ports),hardware_(hardware),runtime_(runtime),formatter_(formatter),
         data_(data),stored_(stored),device_(device){}
    bool enabled=false;
    std::uint32_t timeout;
    HttpRequest request;
    WifiForm form;
    std::array<char,10> trigger={{0}};
    void start(){listener_.listen(80);}
    void poll(); // 0x402079c0: DNS, yield, enabled gate, accept, parse, dispatch, release
    void dispatch(HttpStream& stream,HttpRequest& incoming);
    void page_header(HttpStream& stream); // 0x40205f84
    void page_footer(HttpStream& stream); // 0x40202f14
    void setup_page(HttpStream& stream); // 0x40206074
    void reset_page(HttpStream& stream); // 0x40206284
    void calibration_page(HttpStream& stream); // 0x40206338
    void details_page(HttpStream& stream); // 0x402063dc
    void configure_page(HttpStream& stream,HttpRequest& incoming); // 0x40206608
    bool validate_trigger(HttpStream& stream,HttpRequest& incoming,const char* expected); // 0x402069d4
private:
    HttpListener& listener_;Ports& ports_;HardwarePorts& hardware_;Runtime& runtime_;Formatter& formatter_;
    DeviceData& data_;PersistentData& stored_;HttpDevice& device_;
    void bad_request(HttpStream& stream);
};
}}
