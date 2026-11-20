#include "msp_bus.hpp"
#include <cassert>
#include <cstring>
using zseries::standalone::esp32::MspBus;
int main(){
    std::uint8_t command[]={8,0,Z_MSP_PUMPS_SET,0x70,0xad,0xde};
    std::uint8_t cutoff[]={8,0,Z_MSP_PUMPS_SET,0,0xad,0xde};
    std::uint8_t response[9];
    {TwoWire wire;MspBus bus(wire);auto io=bus.transport();io.transmit(io.context,0x40,command,sizeof command);
        io.receive(io.context,0x40,response,sizeof response);assert(!bus.failed()&&wire.deadline==50&&wire.reads==1&&wire.writes==1&&response[0]==0x7f);}
    {TwoWire wire;wire.received=8;MspBus bus(wire);auto io=bus.transport();io.receive(io.context,0x40,response,sizeof response);
        assert(bus.failed()&&wire.reads==1);for(auto byte:response)assert(byte==0);
        io.transmit(io.context,0x40,command,sizeof command);assert(!wire.writes);
        io.transmit(io.context,0x40,cutoff,sizeof cutoff);assert(wire.writes==1&&wire.frame[3]==0);
        io.receive(io.context,0x40,response,sizeof response);assert(wire.reads==1);}
    {TwoWire wire;wire.write_error=I2C_ERROR_TIMEOUT;MspBus bus(wire);auto io=bus.transport();io.transmit(io.context,0x40,command,sizeof command);
        assert(bus.failed()&&wire.deadline==50);io.receive(io.context,0x40,response,sizeof response);assert(!wire.reads);for(auto byte:response)assert(byte==0);}
    {TwoWire wire;MspBus bus(wire);auto io=bus.transport();std::uint8_t oversized[25]={};io.transmit(io.context,0x40,oversized,sizeof oversized);assert(bus.failed()&&!wire.writes);}
}
