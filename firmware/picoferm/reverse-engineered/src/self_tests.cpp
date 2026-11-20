#include "picoferm/application.hpp"
#include "picoferm/http.hpp"
#include <algorithm>
#include <cstring>
namespace picoferm { namespace oem {
namespace {
void require(bool value,Ports& ports){if(!value)diagnostic_assertion(ports);}
bool same_text(const char* first,const char* second){return std::strcmp(first,second)==0;}
bool equal_record(const PersistentData& first,const PersistentData& second){
    const auto& a=first.bytes;const auto& b=second.bytes;
    for(unsigned offset:{0x61u,0x62u,0x63u,0x6du,0x8bu,0x76u})if(a[offset]!=b[offset])return false;
    for(unsigned offset:{0x65u,0x6eu})if(first.word(offset)!=second.word(offset))return false;
    const auto version=[](const PersistentData& data){return data.word(0x69)==0xdeadbeefu?data.word(0x72):0;};
    if(version(first)!=version(second)||!same_text(reinterpret_cast<const char*>(a.data()),reinterpret_cast<const char*>(b.data()))||
       !same_text(reinterpret_cast<const char*>(a.data()+32),reinterpret_cast<const char*>(b.data()+32)))return false;
    unsigned index=a[0x76];for(unsigned count=0;count<a[0x8b];++count){Measurement x,y;
        if(!first.measurement(index,x)||!second.measurement(index,y)||x.temperature!=y.temperature||x.pressure!=y.pressure)return false;
        index=(index+1)%12;
    }return true;
}
void data_tests(Ports& ports){
    const std::uint8_t bytes[5][8]={{0},{0},{255},{4,255,1},{1,2,3,4,5,6,7,8}};
    const unsigned lengths[]={0,1,1,3,8};const char* expected[]={"","00","ff","04ff01","0102030405060708"};
    for(unsigned i=0;i<5;++i){char hex[32];encode_hex(hex,sizeof(hex),bytes[i],lengths[i],ports);require(same_text(hex,expected[i]),ports);}
    DeviceData data;data.initialize("0.2.0","my.test.website.com",ports);require(same_text(data.version.data(),"0.2.0"),ports);require(same_text(data.website.data(),"my.test.website.com"),ports);
    const std::uint8_t mac[]={1,2,3,4,5,6};data.set_mac(mac,6,ports);require(same_text(data.ap_ssid.data(),"PicoFerm_010203040506"),ports);
    data.initialize("0.2.0","my.test.website.com",ports);require(same_text(data.token.data(),"00000000"),ports);
    data.update_token("foo","bar",ports);require(!same_text(data.token.data(),"00000000"),ports);
    data.update_token("","",ports);require(same_text(data.token.data(),"00000000"),ports);
    data.update_token("    ","",ports);require(same_text(data.token.data(),"20202020"),ports);
    data.update_token("    ","    ",ports);require(same_text(data.token.data(),"00000000"),ports);
}
void buffer_tests(Ports& ports){
    Buffer<16> buffer;buffer.clear();buffer.append("",ports);buffer.clear();buffer.append("a",ports);require(buffer.length==1,ports);require(same_text(buffer.c_str(),"a"),ports);
    buffer.append("b",ports);require(buffer.length==2,ports);require(same_text(buffer.c_str(),"ab"),ports);buffer.append("",ports);require(buffer.length==2,ports);require(same_text(buffer.c_str(),"ab"),ports);
    buffer.clear();buffer.append("0123456789",ports);require(buffer.length==10,ports);ports.text("TEST","ExpectedError","ERR ASSERT datalen < _left Buffer not big enough");
    buffer.append("0123456789",ports);require(buffer.length==15,ports);require(same_text(buffer.c_str(),"012345678901234"),ports);
}
void eeprom_tests(Ports& ports,Formatter& formatter){
    PersistentData unused,first,second;unused.reset(ports);first.reset(ports);second.reset(ports);require(equal_record(first,second),ports);
    second.word(0x70,second.word(0x70)&0xffff);second.word(0x74,second.word(0x74)&0xffff0000);second.word(0x68,second.word(0x68)|0xffffff00u);second.word(0x6c,second.word(0x6c)|255);require(!equal_record(first,second),ports);
    second.reset(ports);for(unsigned i=0;i<20;++i){second.append({static_cast<float>(i),static_cast<float>(i)},ports);require(second.bytes[0x8b]==std::min(i+1,12u),ports);}
    require(second.bytes[0x8b]==12,ports);require(second.bytes[0x76]==8,ports);report_storage(second,ports,formatter);
    ports.text("EPRM","API","clearDataPoints()");second.bytes[0x76]=0;second.bytes[0x8b]=0;report_storage(second,ports,formatter);
}
void memory_tests(Ports& ports,Formatter& formatter,SystemPorts& system){
    const auto load=[&](PersistentData& record){if(system.storage_size()>=PersistentData::size)system.storage_read(record.bytes.data(),PersistentData::size);record.sanitize(ports);};
    PersistentData saved,first,second;saved.reset(ports);load(saved);first.reset(ports);second.reset(ports);
    bounded_copy(reinterpret_cast<char*>(first.bytes.data()),32,"ssid",ports);bounded_copy(reinterpret_cast<char*>(first.bytes.data()+32),64,"password",ports);
    const auto round_trip=[&](){ports.persist(first.bytes.data(),PersistentData::size);load(second);require(equal_record(first,second),ports);};
    round_trip();first.bytes[0x61]=1;round_trip();first.bytes[0x61]=0;round_trip();first.word(0x65,123456);round_trip();
    report_storage(first,ports,formatter);report_storage(second,ports,formatter);ports.persist(saved.bytes.data(),PersistentData::size);
}
void session_tests(Ports& ports){
    Session value;const char* texts[]={"2","2,","10","10,64"};
    for(unsigned i=0;i<4;++i){parse_session(value,texts[i],ports);require((value.error_flags!=0)==(i==3),ports);require(value.state==SessionState::Idle||value.state==SessionState::Fermenting,ports);require(value.state==(i<2?SessionState::Idle:SessionState::Fermenting),ports);}
    for(const char* text:{"123","123,","123,456"}){ports.text("TEST","ExpectedError","ERR State number out of range");parse_session(value,text,ports);if(same_text(text,"123,456")){require(value.error_flags!=0,ports);require(value.error_flags==456,ports);}else require(value.error_flags==0,ports);require(value.state!=SessionState::Idle&&value.state!=SessionState::Fermenting,ports);}
}
}
void Application::run_embedded_tests(){
    ports_.text("MAIN","TESTS","Assert");ports_.text("TEST","ExpectedError","ERR ASSERT This is a test for a failing assertion");diagnostic_assertion(ports_);
    ports_.text("MAIN","TESTS","CString::tests()");run_cstring_self_tests(ports_);
    ports_.text("MAIN","TESTS","Data::tests()");data_tests(ports_);
    ports_.text("MAIN","TESTS","HttpParser::tests");run_http_parser_self_tests(ports_);
    ports_.text("MAIN","TESTS","Buffer<0>::tests()");buffer_tests(ports_);
    ports_.text("MAIN","TESTS","EepromData::tests()");eeprom_tests(ports_,formatter_);
    ports_.text("MAIN","TESTS","MemoryHandler::tests()");memory_tests(ports_,formatter_,system_);
    ports_.text("MAIN","TESTS","Backend::tests()");ports_.text("MAIN","TESTS","SessionData::tests()");session_tests(ports_);
}
}}
