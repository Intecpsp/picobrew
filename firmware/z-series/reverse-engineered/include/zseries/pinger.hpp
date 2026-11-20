// Z119 Pinger.h. The SDK owns ICMP transport; this object owns OEM policy.
#pragma once
#include "zseries/console.hpp"
#include <cstdint>
namespace zseries { namespace oem {
struct PingStatistics {
    std::uint32_t response_ms=0,timeouts=0,sent=0,received=0,errors=0,bytes=0;
    std::uint32_t total_bytes=0,total_ms=0,minimum_ms=0,maximum_ms=0;
};
struct PingerServices {
    void* context=nullptr;
    bool (*wifi_status)(void*,std::int32_t&)=nullptr;
    bool (*set_option)(void*,std::uint32_t option,std::uint32_t value)=nullptr;
    bool (*start)(void*,std::int32_t& error)=nullptr;
    bool (*resolve)(void*,const char*,std::uint32_t& address,std::int32_t& result)=nullptr;
    // IPAddress::fromString's boolean is ignored by the original caller.
    bool (*parse)(void*,const char*,std::uint32_t& address)=nullptr;
    bool (*register_commands)(void*)=nullptr;
    ConsoleServices diagnostics;
};
enum class PingerResult {Ok,MissingService,ServiceFailure};
class Pinger {
public:
    PingerServices services;
    std::uint32_t target=0;
    // The lwIP callback and the Core console task share this aligned word.
    volatile std::int32_t remaining=0;
    PingerResult setup(); // 0x400f71b0, count10/timeout1000/delay500
    PingerResult ping(std::uint32_t address,bool& started); // 0x400e14c4
    PingerResult hostname(const char*,bool& started); // 0x400e15f8
    PingerResult address(const char*,bool& started); // 0x400e1738
    PingerResult receive(const PingStatistics&); // 0x400db05c
private:
    PingerResult assertion(std::uint32_t,const char*);
    PingerResult diagnostic(ConsoleLevel,std::uint32_t,const char*);
};
}} // namespace zseries::oem
