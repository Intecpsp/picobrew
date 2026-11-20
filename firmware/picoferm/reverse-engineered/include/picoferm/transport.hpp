#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace picoferm { namespace oem {
struct Runtime {
    virtual ~Runtime()=default;
    virtual void delay_ms(std::uint32_t duration)=0;
};
struct OutboundClient {
    virtual ~OutboundClient()=default;
    virtual bool connect_ip(std::uint32_t address,unsigned port)=0;
    virtual int available()=0;
    virtual int read()=0;
    virtual bool connected()=0;
    virtual void write(const char* bytes,std::size_t length)=0;
    virtual void stop()=0;
};
}}
