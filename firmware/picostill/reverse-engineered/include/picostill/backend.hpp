#pragma once
#include "picostill/application.hpp"
#include "picostill/http.hpp"
#include "picostill/transport.hpp"
namespace picostill { namespace oem {
class Backend {
public:
    Backend(BackendStatus& status,DeviceData& data,OutboundClient& client,HttpRuntime& runtime,Diagnostics& diagnostics) noexcept
        :status_(status),data_(data),client_(client),runtime_(runtime),diagnostics_(diagnostics) {}
    std::uint32_t timeout=30000,deadline=0,received=0,pending_command=0;
    bool pending=false,reading=false,inside=false,has_error=false;
    const char* error=nullptr;
    std::array<char,256> response={{0}};
    std::array<char,512> request={{0}};
    std::array<char,64> extra_headers={{0}};
    std::array<char,128> update_address={{0}};
    std::uint32_t request_length=0,request_remaining=511;
    void reset();
    void sync(std::uint32_t command,const char* action,const char* key,const char* value);
    void request_firmware(){sync(1,"getFirmwareAddress","version",data_.version.data());}
    void send_error(StillError value){sync(2,"logError","error",to_string(value));}
    void send(const char* action,const char* key,const char* value);
    void poll();
    void complete();
private:
    BackendStatus& status_;DeviceData& data_;OutboundClient& client_;HttpRuntime& runtime_;Diagnostics& diagnostics_;
    void append(const char* value);
    void fail(const char* message);
};
}} // namespace picostill::oem
