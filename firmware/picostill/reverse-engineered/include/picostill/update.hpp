#pragma once
#include "picostill/http.hpp"
#include "picostill/transport.hpp"
#include <array>
namespace picostill { namespace oem {
struct UpdateParameters {
    std::array<char,64> host={{0}},path={{0}};
    bool valid=false;
    std::uint32_t host_length=0,host_remaining=63,path_length=0,path_remaining=63;
    void parse(const char* uri,Diagnostics& diagnostics);
    void report(HttpRuntime& runtime) const;
};
struct UpdatePorts {
    virtual ~UpdatePorts() = default;
    virtual bool begin(std::uint32_t size) = 0;
    virtual std::size_t write(std::uint8_t* bytes,std::size_t size) = 0;
    virtual bool end() = 0;
    virtual bool finished() = 0;
    virtual unsigned error() = 0;
    virtual std::string md5() = 0;
    virtual void console(const char* message,bool line) = 0;
};
class FirmwareUpdate {
public:
    FirmwareUpdate(OutboundClient& client,UpdatePorts& flash,HttpRuntime& runtime,Diagnostics& diagnostics) noexcept
        :client_(client),flash_(flash),runtime_(runtime),diagnostics_(diagnostics) {}
    std::uint32_t state=0,changed_at=0,written=0;
    std::int32_t content_length=0;
    bool binary_content=false;
    std::array<std::uint8_t,2048> buffer={{0}};
    void begin(const char* host,const char* path);
    void poll();
private:
    OutboundClient& client_;UpdatePorts& flash_;HttpRuntime& runtime_;Diagnostics& diagnostics_;
};
}} // namespace picostill::oem
