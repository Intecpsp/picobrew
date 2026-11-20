#include "zseries/still_client.hpp"
#include <algorithm>
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace {
enum Index { PENDING,ERROR,RETRYABLE,DONE,HTTP_STATE,HTTP_ERROR,SOCKET_ERROR,CONFIGURE_RESULT,GET_RESULT,POST_RESULT,
    STATUS,LENGTH,NOW,OLD_TIMESTAMP,OLD_DELTA,WINDOW10,WINDOW60,STATE,POLL_STOP,ARGUMENT,CAPACITY,REPEAT,SEED,WORDS };
template<std::size_t N> void copy(std::array<char,N>& out,const char* value){std::strncpy(out.data(),value,N-1);}
void text(std::vector<std::uint32_t>& out,const char* value,std::size_t count){out.push_back(static_cast<std::uint32_t>(count));for(std::size_t i=0;i<count;++i)out.push_back(static_cast<unsigned char>(value[i]));}
void text(std::vector<std::uint32_t>& out,const char* value){text(out,value,value?std::strlen(value):0);}
void number(std::vector<std::uint32_t>& out,double value){std::uint64_t bits;std::memcpy(&bits,&value,8);out.push_back(static_cast<std::uint32_t>(bits));out.push_back(static_cast<std::uint32_t>(bits>>32));}
void sample(std::vector<std::uint32_t>& out,const PicoStillSensors& value){
    out.push_back(value.timestamp);for(double n:value.temperature)number(out,n);number(out,value.pressure);
    out.push_back(value.pump_running);out.push_back(value.fan_running);out.push_back(value.program_running);text(out,value.error.data());out.push_back(value.delta_ms);
    for(double n:value.speed)number(out,n);
}
void stats(std::vector<std::uint32_t>& out,const PicoStillStatistics& value){
    for(double n:value.values)number(out,n);
    out.push_back(value.count);out.push_back(value.next);
    number(out,value.sum);number(out,value.mean);number(out,value.squared_deviations);number(out,value.difference);number(out,value.difference_alpha);
    out.push_back(value.difference_defined);number(out,value.smoothed_difference);
}
struct Fixture{const std::uint32_t* input;std::vector<std::vector<std::uint32_t>> events;};
bool service(void* context,const StillHttpAction& action,std::uint32_t& result){
    auto& f=*static_cast<Fixture*>(context);std::vector<std::uint32_t> row{static_cast<std::uint32_t>(action.effect),action.effect==StillHttpEffect::Configure?action.client_class:0};
    text(row,action.uri);text(row,action.content_type);text(row,action.body,action.body_length);f.events.push_back(row);
    result=action.effect==StillHttpEffect::Configure?f.input[CONFIGURE_RESULT]:action.effect==StillHttpEffect::Get?f.input[GET_RESULT]:action.effect==StillHttpEffect::Post?f.input[POST_RESULT]:1;
    return true;
}
PicoStillSensors initial(std::uint32_t timestamp,std::uint32_t delta){
    PicoStillSensors s;s.timestamp=timestamp;s.delta_ms=delta;s.temperature={{1,2,3,4}};s.pressure=5;s.pump_running=s.program_running=true;copy(s.error,"old error");s.speed={{-1,-2,-3,-4,-5}};return s;
}
}
extern "C" int still_client_test(unsigned op,const std::uint32_t* a,const char* input,const char* password,std::uint32_t* output,std::size_t capacity){
    PicoStillState state;PicoStillSamples samples;samples.current=initial(a[OLD_TIMESTAMP],a[OLD_DELTA]);samples.seconds10=initial(a[WINDOW10],a[OLD_DELTA]);samples.seconds60=initial(a[WINDOW60],a[OLD_DELTA]);
    state.id=static_cast<PicoStillStateId>(a[STATE]);state.polling_stopped=a[POLL_STOP]!=0;copy(state.ip,"old-ip");copy(state.version,"old-version");copy(state.error,"old error");state.response_date=300;
    StillClient c(state,samples);Fixture f{a,{}};c.services.context=&f;c.services.perform=service;
    c.pending=static_cast<Command>(a[PENDING]);c.error=a[ERROR]!=0;c.retryable=a[RETRYABLE]!=0;c.response_done=a[DONE]!=0;
    c.http.state=a[HTTP_STATE];c.http.error=a[HTTP_ERROR];c.http.socket_error=a[SOCKET_ERROR];c.response.status=static_cast<std::int32_t>(a[STATUS]);c.response.content_length=static_cast<std::int32_t>(a[LENGTH]);copy(c.response.content_type,"old/type");
    copy(c.host,"192.168.4.1");copy(c.uri,"old-uri");copy(c.body,"old-response");c.body_length=12;
    std::uint32_t result=0;
    switch(op){
    case 0:c.set_host(input);break;
    case 1:c.build_uri(static_cast<Command>(a[ARGUMENT]));break;
    case 2:result=static_cast<std::uint32_t>(c.reset());break;
    case 3:result=static_cast<std::uint32_t>(c.command(static_cast<Command>(a[ARGUMENT])));break;
    case 4:{WifiCredentials credentials;copy(credentials.ssid,input);copy(credentials.password,password);result=static_cast<std::uint32_t>(c.configure(credentials));break;}
    case 5:c.tick();break;
    case 6:c.mark_error();break;
    case 7:{StillHttpResponse response;response.status=static_cast<std::int32_t>(a[ARGUMENT]);response.content_length=static_cast<std::int32_t>(a[LENGTH]);copy(response.content_type,input);result=c.receive_headers(response);break;}
    case 8:result=static_cast<std::uint32_t>(c.receive_body(input,std::strlen(input)));break;
    case 9:c.body.fill(0);c.body_length=0;c.receive_body(input,std::strlen(input));c.complete(a[NOW]);break;
    case 10:result=samples.parse(a[NOW],input,std::strlen(input));c.assertion_line=samples.assertion_line;break;
    case 11:{auto previous=samples.current;samples.current.timestamp=a[NOW];for(std::size_t i=0;i<4;++i)samples.current.temperature[i]+=10.25;samples.current.pressure-=2.5;samples.current.update_speeds(previous);break;}
    case 12:
        for(std::uint32_t i=0;i<a[REPEAT];++i){samples.current.timestamp=a[NOW]+i*3000u;samples.current.temperature[0]=static_cast<double>((i*17u+a[SEED])%31u)/4.0;
            samples.current.temperature[1]=static_cast<double>((i*7u+a[SEED])%53u)/8.0;samples.current.pressure=static_cast<double>((i*11u+a[SEED])%13u)/16.0;samples.update_statistics(samples.current.timestamp);}
        c.assertion_line=samples.assertion_line;break;
    case 13:{std::array<char,128> destination;destination.fill('Z');result=static_cast<std::uint32_t>(still_form_encode(destination.data(),a[CAPACITY],input));
        if(capacity<132)return 2;
        output[0]=131;output[1]=result;output[2]=128;output[3]=0;for(std::size_t i=0;i<128;++i)output[i+4]=static_cast<unsigned char>(destination[i]);return 0;}
    case 14:state.error.fill(0);copy(state.error,input);samples.current.error=state.error;result=still_has_error(state);break;
    case 15:{state.error.fill(0);copy(state.error,input);std::vector<std::uint32_t> message;const char* value=still_error_text(state,c.assertion_line);message.push_back(c.assertion_line);text(message,value);
        if(capacity<message.size()+4)return 2;
        output[0]=static_cast<std::uint32_t>(message.size()+3);output[1]=0;output[2]=static_cast<std::uint32_t>(message.size());output[3]=0;std::copy(message.begin(),message.end(),output+4);return 0;}
    default:return 1;
    }
    std::vector<std::uint32_t> values{static_cast<std::uint32_t>(c.pending),c.error,c.retryable,c.response_done,c.http.state,c.http.error,c.http.socket_error,
        static_cast<std::uint32_t>(c.response.status),static_cast<std::uint32_t>(c.response.content_length),static_cast<std::uint32_t>(c.body_length),c.host_truncated,c.uri_truncated,c.body_truncated,c.assertion_line,
        static_cast<std::uint32_t>(state.id),state.polling_stopped,state.response_date};
    text(values,c.host.data());text(values,c.uri.data());text(values,c.body.data(),c.body_length);text(values,c.response.content_type.data());text(values,state.ip.data());text(values,state.version.data());
    sample(values,samples.current);sample(values,samples.seconds10);sample(values,samples.seconds60);stats(values,samples.t1);stats(values,samples.t2);stats(values,samples.pressure);
    std::vector<std::uint32_t> all{result,static_cast<std::uint32_t>(values.size()),static_cast<std::uint32_t>(f.events.size())};all.insert(all.end(),values.begin(),values.end());
    for(const auto& row:f.events){all.push_back(static_cast<std::uint32_t>(row.size()));all.insert(all.end(),row.begin(),row.end());}
    if(all.size()+1>capacity)return 2;
    output[0]=static_cast<std::uint32_t>(all.size());std::copy(all.begin(),all.end(),output+1);return 0;
}
