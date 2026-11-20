#pragma once
#include "picoferm/data.hpp"
#include "picoferm/hardware.hpp"
#include "picoferm/transport.hpp"
namespace picoferm { namespace oem {
struct BackendStatus {
    bool started=false,done=false,error=false,known=false,registered=false,needs_update=false;
    std::array<char,128> update_address={{0}};
    Session session;
    bool data_sent=false;
};
class Backend {
public:
    Backend(Ports& ports,HardwarePorts& hardware,Runtime& runtime,Formatter& formatter,OutboundClient& client,
            DeviceData& data,PersistentData& stored) noexcept
        : ports_(ports),hardware_(hardware),runtime_(runtime),formatter_(formatter),client_(client),data_(data),stored_(stored) {}
    BackendStatus status;
    std::uint32_t timeout=30000,deadline=0,host_ip=0,received=0,pending_command=0;
    bool pending=false,reading=false,inside=false,send_logs=false;
    const char* transport_error=nullptr;
    std::int32_t rate=0;float voltage=0;
    std::array<char,256> response={{0}};
    Buffer<512> request;Buffer<64> extra_headers;
    void set_headers(const char* website); // 0x402054f4
    void request_prefix(const char* action); // 0x402053a8
    void argument(const char* key,const char* value); // 0x40205450
    void send(const char* text,const char* headers); // 0x40206a28
    void fail(const char* message); // 0x40205630
    void sync(std::int32_t minutes,float battery,bool logs); // 0x40206b0c
    void request_state(); // 0x40207528
    void request_logs(); // 0x40207584
    void complete(); // 0x402076c4
    void poll(); // 0x40207878
private:
    Ports& ports_;HardwarePorts& hardware_;Runtime& runtime_;Formatter& formatter_;OutboundClient& client_;DeviceData& data_;PersistentData& stored_;
    void finish_request(std::uint32_t command);
    void receive_session(); // 0x40204350
};
}}
