#include "hardware_bridge.cpp"
#include "formatter.hpp"
static_assert(sizeof(picoferm::oem::DeviceData)==112,"Original Data arrays");
extern "C" unsigned ferm_data_run(unsigned op,std::uint8_t* bytes,std::uint8_t* stored,std::uint8_t* output,unsigned capacity,const char* first,const char* second,const std::uint8_t* raw,unsigned length){
    ports.events.clear();DeviceData data;std::memcpy(&data,bytes,112);PersistentData record;std::memcpy(record.bytes.data(),stored,236);FakeFormatter formatter;bool changed=false;
    switch(op){case 0:bounded_copy(reinterpret_cast<char*>(output),capacity,first,ports);break;case 1:encode_hex(reinterpret_cast<char*>(output),capacity,raw,length,ports);break;
    case 2:data.initialize(first,second,ports);break;case 3:data.set_mac(raw,length,ports);break;case 4:data.update_token(first,second,ports);break;
    case 5:configure_credentials(data,record,first,second,reinterpret_cast<char*>(output),capacity,changed,ports);break;case 6:report_storage(record,ports,formatter);break;}
    std::memcpy(bytes,&data,112);std::memcpy(stored,record.bytes.data(),236);return changed;
}
