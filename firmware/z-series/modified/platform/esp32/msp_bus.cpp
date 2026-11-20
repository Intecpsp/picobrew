#include "msp_bus.hpp"
#include <cstring>

namespace zseries { namespace standalone { namespace esp32 {
z_msp_transport MspBus::transport() noexcept {
    z_msp_transport io{}; io.context=this;
    io.transmit=[](void* p,std::uint8_t address,const std::uint8_t* data,std::size_t size){static_cast<MspBus*>(p)->transmit(address,data,size);};
    io.receive=[](void* p,std::uint8_t address,std::uint8_t* data,std::size_t size){static_cast<MspBus*>(p)->receive(address,data,size);};
    io.delay_ms=[](void*,std::uint32_t value){::delay(value);};
    return io;
}
void MspBus::transmit(std::uint8_t address,const std::uint8_t* data,std::size_t size){
    // Every recovered application/BSL frame fits 24 bytes. Copy into owned
    // mutable memory for this SDK's historical non-const write signature.
    std::uint8_t frame[24];
    if(!data||!size||size>sizeof frame){failed_=true;return;}
    const bool cutoff=size>=5&&data[0]==8&&data[2]==Z_MSP_PUMPS_SET&&data[3]==0;
    if(failed_&&!cutoff)return;
    std::memcpy(frame,data,size); wire_.setTimeOut(50);
    if(wire_.writeTransmission(address,frame,static_cast<std::uint16_t>(size),true)!=I2C_ERROR_OK)failed_=true;
}
void MspBus::receive(std::uint8_t address,std::uint8_t* data,std::size_t size){
    if(!data||!size||size>9){failed_=true;return;}
    std::memset(data,0,size);if(failed_)return;
    std::uint32_t received=0;wire_.setTimeOut(50);
    const auto result=wire_.readTransmission(address,data,static_cast<std::uint16_t>(size),true,&received);
    if(result!=I2C_ERROR_OK||received!=size){failed_=true;std::memset(data,0,size);}
}
}}}
