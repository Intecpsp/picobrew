#include "zseries/eeprom.hpp"
#include "zseries/console.hpp"
#include <cstring>
#include <string>
using namespace zseries::oem;
namespace {
using Word=std::uint32_t;
std::string hex(const EepromBytes& bytes){static const char alphabet[]="0123456789abcdef";std::string result;for(auto value:bytes){result+=alphabet[value>>4];result+=alphabet[value&15];}return result;}
struct Fixture {
    EepromBytes cache;Word actual;bool dirty,begin_result,commit_result;Word counter,clock;std::string events;
    bool begin(Word requested){events+="begin|"+std::to_string(requested)+"\n";return begin_result;}
    template<class T>T& get(int address,T& target){if(address<0||static_cast<Word>(address)+sizeof(T)>actual)return target;std::memcpy(&target,cache.data()+address,sizeof(T));return target;}
    template<class T>const T& put(int address,const T& source){if(address<0||static_cast<Word>(address)+sizeof(T)>actual)return source;std::memcpy(cache.data()+address,&source,sizeof(T));dirty=true;return source;}
    bool commit(){events+="commit|"+std::to_string(dirty?1:0)+"|"+hex(cache)+"\n";if(commit_result)dirty=false;return commit_result;}
};
bool assertion(void*p,const char*file,Word line,const char*expression){auto& f=*static_cast<Fixture*>(p);f.events+="assertion|"+std::string(file)+"|"+std::to_string(line)+"|"+expression+"\ndelay|200\n";++f.counter;f.clock+=200;return true;}
bool log(void*p,ConsoleLevel level,const char*tag,const char*file,Word line,const char*message){auto& f=*static_cast<Fixture*>(p);f.events+="log|"+std::to_string(static_cast<unsigned>(level))+"|"+tag+"|"+(file?file:"")+"|"+std::to_string(line)+"|"+message+"\n";return true;}
void registration(void*p,EepromStore&,const std::array<EepromCommandDescriptor,9>&){static_cast<Fixture*>(p)->events+="register\n";}
}
extern "C" unsigned eeprom_continuation_native(unsigned action,const std::uint8_t* data,const std::uint8_t* cache,const Word* args,const char* text,std::uint8_t* out_data,std::uint8_t* out_cache,Word* meta,char* events){
    Fixture fixture{{},args[3],args[1]!=0,args[4]!=0,args[5]!=0,args[6],args[7],{}};std::memcpy(fixture.cache.data(),cache,120);
    EepromBytes bytes;std::memcpy(bytes.data(),data,120);EepromStore store;store.data=decode_eeprom(bytes);store.initialized=args[0]!=0;store.requested_bytes=args[2];store.backend=arduino_eeprom_backend(fixture);
    ConsoleServices diagnostics;diagnostics.context=&fixture;diagnostics.assertion=assertion;diagnostics.log=log;store.services.context=&fixture;store.services.register_commands=registration;store.services.diagnostics=&diagnostics;
    Word value=0;EepromResult result=EepromResult::Ok;
    switch(action){
    case 0:store.data=EepromData{};break;
    case 1:{bool changed;result=eeprom_enforce_valid_enums(store.data,changed,diagnostics);value=changed;break;}
    case 2:value=eeprom_data_valid(store.data);break;
    case 3:value=eeprom_text_boolean(text);break;
    case 4:{std::size_t copied;result=eeprom_set_ssid(store.data,text,copied,diagnostics);value=copied;break;}
    case 5:{std::size_t copied;result=eeprom_set_password(store.data,text,copied,diagnostics);value=copied;break;}
    case 6:result=store.setup();break;
    case 7:result=store.write();break;
    case 8:result=store.reset();break;
    case 9:{EepromData* pointer=nullptr;result=store.get_data(pointer);value=pointer==&store.data;break;}
    case 10:result=store.command(EepromCommand::Ssid,text);break;
    case 11:result=store.command(EepromCommand::Password,text);break;
    case 12:result=store.command(EepromCommand::Azure,text);break;
    case 13:result=store.command(EepromCommand::Ethernet,text);break;
    case 14:result=store.command(EepromCommand::BoilerUnknown);break;
    case 15:result=store.command(EepromCommand::BoilerBig);break;
    case 16:result=store.command(EepromCommand::BoilerSmall);break;
    default:return 98;
    }
    if(fixture.events.size()>=32768)return 99;
    bytes=encode_eeprom(store.data);std::memcpy(out_data,bytes.data(),120);std::memcpy(out_cache,fixture.cache.data(),120);
    meta[0]=store.assertion_line;meta[1]=value;meta[2]=store.initialized;meta[3]=fixture.dirty;meta[4]=fixture.counter;meta[5]=fixture.clock;
    std::memcpy(events,fixture.events.c_str(),fixture.events.size()+1);return static_cast<unsigned>(result);
}
extern "C" unsigned eeprom_continuation_missing_services(){
    EepromStore store;ConsoleServices diagnostics;store.services.diagnostics=&diagnostics;EepromData* value=nullptr;
    unsigned mask=0;if(store.get_data(value)==EepromResult::MissingDiagnostic&&!value)mask|=1;
    if(store.write()==EepromResult::MissingDiagnostic)mask|=2;
    if(store.reset()==EepromResult::MissingDiagnostic)mask|=4;
    if(store.command(EepromCommand::Test)==EepromResult::MissingSelfTest)mask|=8;
    return mask;
}
