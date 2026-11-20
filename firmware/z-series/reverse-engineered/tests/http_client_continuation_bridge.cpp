#include "zseries/http_client.hpp"
#include "zseries/diagnostics.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
using namespace zseries::oem;
namespace {
enum Index { STATE,CLASS,PRESENT,ERROR,SOCKET_ERROR,NOW,CLOCK,DATE,DEADLINE,REPLY_TIMEOUT,HEADER_TIMEOUT,STALLED,
    RECEIVED,STATUS,CONTENT_LENGTH,ARGUMENT,ACQUIRE_RESULT,ACQUIRED_CLASS,CONNECT_RESULT,WRITE_SHORT,HEAP_BEFORE,HEAP_AFTER,
    CONNECTED,HEADERS_ACCEPT,READ_ERROR_AT,READ_LIMIT,FROZEN,TRUNCATED,SEND_DATE,HEADERS_DATE,BODY_DATE,SEND_DURATION,REPLY_DURATION,BODY_DURATION,BODY_PRESENT,BODY_SIZE,WORDS };
void text(std::vector<std::uint32_t>& out,const void* value,std::size_t count){out.push_back(static_cast<std::uint32_t>(count));const auto* bytes=static_cast<const std::uint8_t*>(value);for(std::size_t i=0;i<count;++i)out.push_back(bytes[i]);}
void text(std::vector<std::uint32_t>& out,const char* value){text(out,value,value?std::strlen(value):0);}
template<std::size_t N> bool copy(std::array<char,N>& out,const char* value){const auto n=std::strlen(value);std::memcpy(out.data(),value,std::min(n,N-1));return n>=N;}
struct Fixture {const std::uint32_t* input;std::uint32_t now;DiagnosticState diagnostics;std::string wire;std::size_t position=0;unsigned writes=0,heaps=0;std::vector<std::vector<std::uint32_t>> events;
    Fixture(const std::uint32_t* input,const char* wire):input(input),now(input[CLOCK]),wire(wire){}
};
void event(Fixture& f,std::uint32_t id,std::uint32_t argument=0,std::uint32_t second=0,const char* value=nullptr,const void* data=nullptr,std::size_t size=0){
    std::vector<std::uint32_t> row{id,argument,second};text(row,value);text(row,data,size);f.events.push_back(row);
}
std::uint32_t clock(void* p){return static_cast<Fixture*>(p)->now;}
void assertion_log(void* p,double seconds,const char* file,std::uint32_t line,const char* expression){
    auto& f=*static_cast<Fixture*>(p);const std::string text=std::string(file)+"|"+expression;
    event(f,16,line,f.diagnostics.assertion_count,text.c_str(),&seconds,sizeof seconds);
}
void delay_ms(void* p,std::uint32_t duration){auto& f=*static_cast<Fixture*>(p);event(f,17,duration,f.diagnostics.assertion_count);f.now+=duration;}
bool assertion(void* p,const char* file,std::uint32_t line,const char* expression){
    DiagnosticServices d;d.context=p;d.millis=clock;d.assertion_log=assertion_log;d.delay_ms=delay_ms;
    return record_assertion(static_cast<Fixture*>(p)->diagnostics,d,file,line,expression)==DiagnosticResult::Ok;
}
bool log(void* p,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* message){
    auto& f=*static_cast<Fixture*>(p);const double seconds=static_cast<float>(f.now)/1000.0f;
    const std::string text=std::string(tag)+"|"+(file?file:"")+"|"+message;
    event(f,18,static_cast<unsigned>(level),line,text.c_str(),&seconds,sizeof seconds);return true;
}
bool perform(void* context,const HttpAction& a,std::uint32_t& result){
    auto& f=*static_cast<Fixture*>(context);const auto* in=f.input;event(f,static_cast<std::uint32_t>(a.effect),a.effect==HttpEffect::ReadBytes?static_cast<std::uint32_t>(a.size):a.argument,a.second,a.text,a.data,a.size&&a.data?a.size:0);result=0;
    switch(a.effect){
    case HttpEffect::AcquireClient:*a.acquired_class=static_cast<HttpClientClass>(in[ACQUIRED_CLASS]);result=in[ACQUIRE_RESULT];break;
    case HttpEffect::PrepareEthernet:break;
    case HttpEffect::FreeHeap:result=f.heaps++?in[HEAP_AFTER]:in[HEAP_BEFORE];break;
    case HttpEffect::ConnectHost:case HttpEffect::ConnectAddress:result=in[CONNECT_RESULT];break;
    case HttpEffect::Write:result=static_cast<std::uint32_t>(a.size);if(++f.writes==in[WRITE_SHORT]&&result)--result;break;
    case HttpEffect::Available:result=in[READ_LIMIT]==0xffffffffu?0xffffffffu:static_cast<std::uint32_t>(std::min<std::size_t>(f.wire.size()-f.position,in[READ_LIMIT]));break;
    case HttpEffect::ReadByte:
        if(f.position==in[READ_ERROR_AT]||f.position==f.wire.size())result=0xffffffffu;else result=static_cast<unsigned char>(f.wire[f.position++]);break;
    case HttpEffect::ReadBytes:{const auto n=std::min(a.size,f.wire.size()-f.position);std::memcpy(a.destination,f.wire.data()+f.position,n);f.position+=n;result=static_cast<std::uint32_t>(n);break;}
    case HttpEffect::Connected:result=in[CONNECTED];break;
    case HttpEffect::SocketError:result=in[SOCKET_ERROR];break;
    default:break;
    }return true;
}
bool headers(void* p,const HttpResponse& r){auto& f=*static_cast<Fixture*>(p);event(f,13,static_cast<std::uint32_t>(r.status),static_cast<std::uint32_t>(r.content_length),r.content_type.data());return f.input[HEADERS_ACCEPT]!=0;}
void body(void* p,const std::uint8_t* data,std::size_t size){event(*static_cast<Fixture*>(p),14,0,0,nullptr,data,size);}
void complete(void* p){event(*static_cast<Fixture*>(p),15);}
}
extern "C" int http_client_continuation_test(unsigned operation,std::uint32_t initial_counter,unsigned trace,const std::uint32_t* a,const char* uri,const char* wire,const char* value,const char* content_type,const char* pending_buffer,std::uint32_t* output,std::size_t capacity){
    HttpClient c;Fixture f(a,wire);c.services.context=&f;c.services.millis=clock;c.services.perform=perform;
    c.receiver.context=&f;c.receiver.headers=headers;c.receiver.body=body;c.receiver.complete=complete;
    c.request.parse(uri);copy(c.request.authorization,"old-token");
    c.state=static_cast<HttpState>(a[STATE]);c.client_class=static_cast<HttpClientClass>(a[CLASS]);c.client_present=a[PRESENT]!=0;c.error=static_cast<HttpClientErrorId>(a[ERROR]);c.socket_error=a[SOCKET_ERROR];
    c.state_date=a[DATE];c.deadline=a[DEADLINE];c.reply_timeout_ms=a[REPLY_TIMEOUT];c.header_timeout_ms=a[HEADER_TIMEOUT];c.stalled=a[STALLED]!=0;
    c.content_received=a[RECEIVED];c.response.status=static_cast<std::int32_t>(a[STATUS]);c.response.content_length=static_cast<std::int32_t>(a[CONTENT_LENGTH]);copy(c.response.content_type,"old/type");
    c.buffer_length=std::min<std::size_t>(std::strlen(pending_buffer),1024);std::memcpy(c.buffer.data(),pending_buffer,c.buffer_length);c.buffer_remaining=1024-c.buffer_length;c.buffer_frozen=a[FROZEN]!=0;c.buffer_truncated=a[TRUNCATED]!=0;
    c.timing.trace=false;c.timing.name="";c.timing.send_date=a[SEND_DATE];c.timing.headers_date=a[HEADERS_DATE];c.timing.body_date=a[BODY_DATE];c.timing.send_duration=a[SEND_DURATION];c.timing.reply_duration=a[REPLY_DURATION];c.timing.body_duration=a[BODY_DURATION];
    f.diagnostics.assertion_count=initial_counter;c.trace=trace!=0;c.name="HTTP";
    c.services.diagnostics.context=&f;c.services.diagnostics.assertion=assertion;c.services.diagnostics.log=log;
    bool accepted=false;HttpResult status=HttpResult::Ok;std::uint32_t result=0;
    switch(operation){
    case 0:result=c.request.parse(uri,&c.services.diagnostics);status=c.request.diagnostic_result;break;
    case 1:status=c.reset();break;
    case 2:status=c.transition(static_cast<HttpState>(a[ARGUMENT]));break;
    case 3:status=c.set_error(static_cast<HttpClientErrorId>(a[ARGUMENT]));break;
    case 4:status=c.cancel();break;
    case 5:{const auto receiver=c.receiver;status=c.configure(uri,receiver,static_cast<HttpClientClass>(a[ARGUMENT]),accepted);result=accepted;break;}
    case 6:result=c.set_authorization(value);status=c.request.diagnostic_result;break;
    case 7:status=c.get(accepted);result=accepted;break;
    case 8:status=c.post(content_type,a[BODY_PRESENT]?value:nullptr,a[BODY_SIZE]==0xffffffffu?std::strlen(value):a[BODY_SIZE],accepted);result=accepted;break;
    case 9:status=c.put(content_type,a[BODY_PRESENT]?value:nullptr,a[BODY_SIZE]==0xffffffffu?std::strlen(value):a[BODY_SIZE],accepted);result=accepted;break;
    case 10:status=c.tick(a[NOW]);break;
    case 11:status=c.request.out(c.services.diagnostics);break;
    case 12:status=c.report_timing();break;
    default:return 1;
    }
    if(status!=HttpResult::Ok)return 10+static_cast<int>(status);
    std::vector<std::uint32_t> state{static_cast<std::uint32_t>(c.state),static_cast<std::uint32_t>(c.client_class),c.client_present,static_cast<std::uint32_t>(c.error),c.socket_error,c.state_date,c.deadline,c.stalled,c.content_received,
        static_cast<std::uint32_t>(c.response.status),static_cast<std::uint32_t>(c.response.content_length),c.response.content_type_truncated,c.assertion_line,
        static_cast<std::uint32_t>(c.buffer_length),static_cast<std::uint32_t>(c.buffer_remaining),c.buffer_truncated,c.buffer_frozen,c.receiver.headers!=nullptr,
        c.timing.send_date,c.timing.headers_date,c.timing.body_date,c.timing.send_duration,c.timing.reply_duration,c.timing.body_duration,
        c.request.default_http_port,c.request.default_https_port,c.request.port,c.request.http,c.request.https,c.request.address,c.request.host_truncated,c.request.path_truncated,c.request.authorization_truncated,
        static_cast<std::uint32_t>(f.position),f.now,f.diagnostics.assertion_count};
    text(state,c.request.host.data());text(state,c.request.path.data());text(state,c.request.authorization.data());text(state,c.response.content_type.data());
    text(state,c.buffer.data(),c.buffer.size());
    std::vector<std::uint32_t> all{result,static_cast<std::uint32_t>(state.size()),static_cast<std::uint32_t>(f.events.size())};all.insert(all.end(),state.begin(),state.end());
    for(const auto& row:f.events){all.push_back(static_cast<std::uint32_t>(row.size()));all.insert(all.end(),row.begin(),row.end());}
    if(all.size()+1>capacity)return 2;
    output[0]=static_cast<std::uint32_t>(all.size());std::copy(all.begin(),all.end(),output+1);return 0;
}
