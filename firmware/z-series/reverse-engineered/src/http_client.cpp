#include "zseries/http_client.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <limits>

namespace zseries { namespace oem {
namespace {
bool bound(const ConsoleServices* d) noexcept {return d&&(d->context||d->assertion||d->log);}
HttpResult emit(const ConsoleServices* d,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* message) noexcept {
    if(!bound(d))return HttpResult::Ok;
    if(!d->log)return HttpResult::MissingService;
    return d->log(d->context,level,tag,file,line,message)?HttpResult::Ok:HttpResult::ServiceFailure;
}
HttpResult formatted(const ConsoleServices* d,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* pattern,...){
    if(!bound(d))return HttpResult::Ok;
    char message[768];va_list args;va_start(args,pattern);const int size=::vsnprintf(message,sizeof message,pattern,args);va_end(args);
    if(size<0||static_cast<std::size_t>(size)>=sizeof message)return HttpResult::InvalidSpan;
    return emit(d,level,tag,file,line,message);
}
const char* state_name(HttpState state) noexcept {
    static const char* const names[]={"Idle","Initialized","WaitingReply","ReadingHeaders","DownloadingContent","Terminate"};
    const auto value=static_cast<unsigned>(state);return value<6?names[value]:"<INVALID>";
}
template<std::size_t N> bool copy(std::array<char,N>& out,const char* text,std::size_t length) noexcept {
    out.fill(0);if(text)std::memcpy(out.data(),text,std::min(length,N-1));return length>=N;
}
template<std::size_t N> bool copy(std::array<char,N>& out,const char* text) noexcept {return copy(out,text,text?std::strlen(text):0);}
template<std::size_t N> bool checked_copy(std::array<char,N>& out,const char* text,std::size_t length,const ConsoleServices* d,HttpResult& result) noexcept {
    if(length>=N){
        result=emit(d,ConsoleLevel::Warning,"BUF","Buffer.h",0x66,"Buffer truncated");
        if(result!=HttpResult::Ok)return true;
        char message[48];::snprintf(message,sizeof message,"Dropping %d bytes",static_cast<int>(length-(N-1)));
        result=emit(d,ConsoleLevel::Warning,"BUF","Buffer.h",0x6a,message);
        if(result!=HttpResult::Ok)return true;
    }
    return copy(out,text,length);
}
std::int32_t decimal(const char* text) noexcept {
    if(!text)return 0;
    while(*text==' '||(*text>=9&&*text<=13))++text;
    bool negative=*text=='-';if(*text=='-'||*text=='+')++text;
    const std::uint32_t limit=negative?0x80000000u:0x7fffffffu;std::uint32_t value=0;
    while(*text>='0'&&*text<='9'){
        const auto digit=static_cast<std::uint32_t>(*text++-'0');
        if(value>(limit-digit)/10u)return negative?std::numeric_limits<std::int32_t>::min():std::numeric_limits<std::int32_t>::max();
        value=value*10u+digit;
    }
    const std::uint32_t bits=negative?0u-value:value;std::int32_t result;std::memcpy(&result,&bits,4);return result;
}
bool ipv4(const char* text,std::uint32_t& address) noexcept {
    std::uint16_t accumulator=0;std::uint8_t dots=0;address=0;
    for(;*text;++text){
        if(*text>='0'&&*text<='9'){accumulator=static_cast<std::uint16_t>(accumulator*10+*text-'0');if(accumulator>255)return false;}
        else if(*text=='.'&&dots<3){address|=static_cast<std::uint32_t>(accumulator)<<(dots++*8);accumulator=0;}
        else return false;
    }
    if(dots!=3)return false;
    address|=static_cast<std::uint32_t>(accumulator)<<24;return true;
}
bool starts(const char* text,const char* prefix) noexcept {return std::strncmp(text,prefix,std::strlen(prefix))==0;}
}
void HttpRequest::reset() noexcept {
    diagnostic_result=HttpResult::Ok;http=https=host_truncated=path_truncated=authorization_truncated=false;port=default_http_port;address=0;host.fill(0);path.fill(0);authorization.fill(0);
}
bool HttpRequest::parse(const char* uri,const ConsoleServices* diagnostics) noexcept {
    reset();if(!uri||!*uri){diagnostic_result=emit(diagnostics,ConsoleLevel::Error,"REQUEST","HttpClient.h",0xa9,"Invalid \"uri\" argument");return false;}
    http=starts(uri,"http://");https=starts(uri,"https://");if(http==https){diagnostic_result=emit(diagnostics,ConsoleLevel::Error,"REQUEST","HttpClient.h",0xb1,"Unsupported protocol");return false;}
    port=https?default_https_port:default_http_port;const char* authority=uri+(https?8:7);const char* colon=std::strchr(authority,':');const char* slash=std::strchr(authority,'/');
    // The original subtracts these pointers and can read past malformed URIs
    // when a colon occurs in the path. Reject that undefined source span.
    if(colon&&slash&&colon>slash)return false;
    if(colon){
        host_truncated=checked_copy(host,authority,static_cast<std::size_t>(colon-authority),diagnostics,diagnostic_result);if(diagnostic_result!=HttpResult::Ok)return false;std::array<char,17> number{};
        checked_copy(number,colon+1,slash?static_cast<std::size_t>(slash-colon):std::strlen(colon+1),diagnostics,diagnostic_result);if(diagnostic_result!=HttpResult::Ok)return false;port=static_cast<std::uint16_t>(decimal(number.data()));
    }else host_truncated=checked_copy(host,authority,slash?static_cast<std::size_t>(slash-authority):std::strlen(authority),diagnostics,diagnostic_result);
    if(diagnostic_result!=HttpResult::Ok)return false;
    if(slash)path_truncated=checked_copy(path,slash,std::strlen(slash),diagnostics,diagnostic_result);
    if(diagnostic_result!=HttpResult::Ok)return false;
    if(!ipv4(host.data(),address))address=0;
    return valid();
}
#define HTTP_TRY(expr) do { const HttpResult r=(expr);if(r!=HttpResult::Ok)return r; } while(false)
HttpResult HttpRequest::out(const ConsoleServices& d) const {
    HTTP_TRY(formatted(&d,ConsoleLevel::Info,"REQUEST",nullptr,0,"IsHttps = %s",https?"True":"False"));
    HTTP_TRY(formatted(&d,ConsoleLevel::Info,"REQUEST",nullptr,0,"IsValid = %s",valid()?"True":"False"));
    HTTP_TRY(formatted(&d,ConsoleLevel::Info,"REQUEST",nullptr,0,"Host = %s",host.data()));
    HTTP_TRY(formatted(&d,ConsoleLevel::Info,"REQUEST",nullptr,0,"HostIp = %d.%d.%d.%d",static_cast<int>(address&255),static_cast<int>((address>>8)&255),static_cast<int>((address>>16)&255),static_cast<int>((address>>24)&255)));
    HTTP_TRY(formatted(&d,ConsoleLevel::Info,"REQUEST",nullptr,0,"Port = %d",static_cast<int>(port)));
    HTTP_TRY(formatted(&d,ConsoleLevel::Info,"REQUEST",nullptr,0,"Path = %s",path.data()));
    return formatted(&d,ConsoleLevel::Info,"REQUEST",nullptr,0,"Authorization = %s",authorization.data());
}
HttpResult HttpClient::report_timing(){
    return log(ConsoleLevel::Info,0x178,"%s STATS send = %d wait = %d recv = %d",timing.name,
        static_cast<std::int32_t>(timing.send_duration),static_cast<std::int32_t>(timing.reply_duration),static_cast<std::int32_t>(timing.body_duration));
}
bool HttpClient::diagnostics_bound() const noexcept {return bound(&services.diagnostics);}
HttpResult HttpClient::diagnose(std::uint32_t line,const char* expression,const char* file){
    assertion_line=line;
    if(!diagnostics_bound())return HttpResult::Ok;
    if(!services.diagnostics.assertion)return HttpResult::MissingService;
    return services.diagnostics.assertion(services.diagnostics.context,file,line,expression)?HttpResult::Ok:HttpResult::ServiceFailure;
}
HttpResult HttpClient::log(ConsoleLevel level,std::uint32_t line,const char* format,...){
    if(!diagnostics_bound())return HttpResult::Ok;
    char message[1200];va_list args;va_start(args,format);const int size=::vsnprintf(message,sizeof message,format,args);va_end(args);
    if(size<0||static_cast<std::size_t>(size)>=sizeof message)return HttpResult::InvalidSpan;
    return emit(&services.diagnostics,level,"HTTPC",line?"HttpClient.h":nullptr,line,message);
}
HttpResult HttpClient::buffer_log(std::uint32_t line,const char* format,...){
    if(!diagnostics_bound())return HttpResult::Ok;
    char message[80];va_list args;va_start(args,format);const int size=::vsnprintf(message,sizeof message,format,args);va_end(args);
    if(size<0||static_cast<std::size_t>(size)>=sizeof message)return HttpResult::InvalidSpan;
    return emit(&services.diagnostics,ConsoleLevel::Warning,"BUF","Buffer.h",line,message);
}
HttpResult HttpClient::perform(const HttpAction& action,std::uint32_t* result){
    if(!services.perform)return HttpResult::MissingService;
    std::uint32_t value=0;
    if(!services.perform(services.context,action,value))return HttpResult::ServiceFailure;
    if(result)*result=value;
    return HttpResult::Ok;
}
HttpResult HttpClient::effect(HttpEffect value,std::uint32_t* result){return perform(HttpAction(value),result);}
void HttpClient::clear_buffer() noexcept {buffer.fill(0);buffer_length=0;buffer_remaining=1024;buffer_truncated=buffer_frozen=false;}
HttpResult HttpClient::append(const char* text) {
    if(buffer_frozen){HTTP_TRY(diagnose(0x5d,"!_isFrozen","Buffer.h"));return HttpResult::Ok;}if(!text)return HttpResult::Ok;
    const std::size_t length=std::strlen(text),count=std::min(length,buffer_remaining);
    if(length>buffer_remaining){
        if(!buffer_truncated){buffer_truncated=true;HTTP_TRY(buffer_log(0x66,"Buffer truncated"));}
        else if(buffer_remaining!=0)HTTP_TRY(diagnose(0x68,"count == 0","Buffer.h"));
        HTTP_TRY(buffer_log(0x6a,"Dropping %d bytes",static_cast<int>(length-count)));
    }
    std::memcpy(buffer.data()+buffer_length,text,count);buffer_length+=count;buffer_remaining-=count;buffer[buffer_length]=0;return HttpResult::Ok;
}
HttpResult HttpClient::reset() {
    if(state!=HttpState::Idle){HTTP_TRY(diagnose(0x349,"_state == State::Idle"));return HttpResult::Ok;}
    request.reset();response=HttpResponse{};clear_buffer();content_received=0;client_present=false;client_class=HttpClientClass::None;receiver=HttpReceiver{};
    error=HttpClientErrorId::None;socket_error=0;stalled=false;return HttpResult::Ok;
}
HttpResult HttpClient::set_error(HttpClientErrorId value) {
    if(error!=HttpClientErrorId::None)HTTP_TRY(diagnose(0x256,"!hasError()"));
    if(static_cast<std::uint32_t>(value)>=HttpClientErrorIdCount)HTTP_TRY(diagnose(0x257,"isEnumValid(error)"));
    else if(value==HttpClientErrorId::None)HTTP_TRY(diagnose(0x258,"error != HttpClientErrorId::None"));
    HTTP_TRY(log(ConsoleLevel::Error,0x259,"%s error %s",name,to_string(value)));
    error=value;return HttpResult::Ok;
}
HttpResult HttpClient::transition(HttpState next){
    if(state==next)return log(ConsoleLevel::Error,0x225,"HttpClient useless call to setState()");
    if(trace)HTTP_TRY(log(ConsoleLevel::Info,0x22a,"%s TRANSITION %s -> %s",name,state_name(state),state_name(next)));
    if(!services.millis)return HttpResult::MissingService;
    state=next;state_date=services.millis(services.context);deadline=0;
    switch(next){
    case HttpState::Idle:
        if(client_present){HTTP_TRY(effect(HttpEffect::Stop));client_present=false;client_class=HttpClientClass::None;}break;
    case HttpState::WaitingReply:deadline=state_date+reply_timeout_ms;break;
    case HttpState::Headers:{const auto now=services.millis(services.context);if(timing.send_date==0)HTTP_TRY(diagnose(0x196,"sendTime > 0"));if(now<=timing.send_date)HTTP_TRY(diagnose(0x197,"sendTime < now"));
        timing.headers_date=now;timing.reply_duration=now-timing.send_date;deadline=state_date+header_timeout_ms;break;}
    case HttpState::Body:{const auto now=services.millis(services.context);if(now<=timing.headers_date)HTTP_TRY(diagnose(0x19f,"recvHeaderTime < now"));timing.body_date=now;break;}
    case HttpState::Complete:{const auto now=services.millis(services.context);if(timing.headers_date!=0){if(now<=timing.headers_date)HTTP_TRY(diagnose(0x1aa,"recvHeaderTime < now"));timing.body_duration=now-timing.headers_date;}else HTTP_TRY(log(ConsoleLevel::Warning,0x1a8,"Cannot compute receive duration"));if(timing.trace)HTTP_TRY(report_timing());break;}
    default:break;
    }return HttpResult::Ok;
}
HttpResult HttpClient::cancel(){if(state==HttpState::Idle)return HttpResult::Ok;HTTP_TRY(log(ConsoleLevel::Warning,0x341,"abort()"));return transition(HttpState::Idle);}
HttpResult HttpClient::configure(const char* uri,const HttpReceiver& sink,HttpClientClass supplied,bool& accepted){
    accepted=false;
    if(state!=HttpState::Idle){HTTP_TRY(set_error(HttpClientErrorId::InvalidState));return HttpResult::Ok;}
    if(client_present)HTTP_TRY(diagnose(0x357,"_client == nullptr"));
    HTTP_TRY(reset());request.parse(uri,&services.diagnostics);HTTP_TRY(request.diagnostic_result);
    if(!request.valid()){HTTP_TRY(request.out(services.diagnostics));HTTP_TRY(set_error(HttpClientErrorId::InvalidRequest));return HttpResult::Ok;}
    HttpClientClass selected=supplied;
    if(selected==HttpClientClass::None){HttpAction action(HttpEffect::AcquireClient);action.argument=request.https;action.acquired_class=&selected;std::uint32_t available=0;HTTP_TRY(perform(action,&available));
        if(!available||selected==HttpClientClass::None){HTTP_TRY(set_error(HttpClientErrorId::SystemClientError));return HttpResult::Ok;}}
    client_present=true;client_class=selected;receiver=sink;HTTP_TRY(transition(HttpState::Initialized));if(error!=HttpClientErrorId::None)HTTP_TRY(diagnose(0x36e,"!hasError()"));accepted=true;return HttpResult::Ok;
}
bool HttpClient::set_authorization(const char* text) noexcept {
    if(state!=HttpState::Initialized){request.diagnostic_result=log(ConsoleLevel::Error,0x37b,"setAuthorization called before init() or after request sent");return false;}
    request.diagnostic_result=HttpResult::Ok;request.authorization_truncated=checked_copy(request.authorization,text,text?std::strlen(text):0,&services.diagnostics,request.diagnostic_result);return request.diagnostic_result==HttpResult::Ok;
}
HttpResult HttpClient::flush_buffer(bool& written){
    written=false;if(buffer_truncated)return log(ConsoleLevel::Error,0x20e,"Buffer got truncated");if(!buffer_length){written=true;return HttpResult::Ok;}
    HttpAction action(HttpEffect::Write);action.data=buffer.data();action.size=buffer_length;buffer_frozen=true;std::uint32_t count=0;HTTP_TRY(perform(action,&count));
    if(count==buffer_length){clear_buffer();written=true;}else HTTP_TRY(log(ConsoleLevel::Error,0x215,"Error while sending, or did not sent exact byte count"));return HttpResult::Ok;
}
HttpResult HttpClient::transmit(const char* method,const char* type,const void* body,std::size_t size,HttpClientErrorId& failure){
    failure=HttpClientErrorId::BrokenInvariant;if(!client_present)HTTP_TRY(diagnose(0x273,"_client != nullptr"));if(client_class==HttpClientClass::None)HTTP_TRY(diagnose(0x274,"_clientClass != ClientClass::None"));
    if(!client_present||client_class==HttpClientClass::None)return log(ConsoleLevel::Error,0x276,"start(): trying to sent request in wrong state");
    if(socket_error)HTTP_TRY(diagnose(0x27f,"0 == _errno"));
    HTTP_TRY(log(ConsoleLevel::Info,0x281,"Before client->connect()"));
    std::uint32_t heap=0;HTTP_TRY(effect(HttpEffect::FreeHeap,&heap));HTTP_TRY(log(ConsoleLevel::Info,0,"xPortGetFreeHeapSize() = %lu",static_cast<unsigned long>(heap)));if(heap<=60000)HTTP_TRY(diagnose(0x284,"freeHeapBeforeConnect > 60000"));
    if(client_class==HttpClientClass::Ethernet){HttpAction action(HttpEffect::PrepareEthernet);action.argument=request.https;HTTP_TRY(perform(action));}
    else if(client_class!=HttpClientClass::Wifi&&client_class!=HttpClientClass::WifiSecure)return log(ConsoleLevel::Error,0x2a3,"Unsupported network");
    HttpAction connect(request.address?HttpEffect::ConnectAddress:HttpEffect::ConnectHost);connect.argument=request.port;connect.second=request.address;connect.text=request.address?nullptr:request.host.data();std::uint32_t connected=0;
    HTTP_TRY(perform(connect,&connected));
    if(client_class==HttpClientClass::WifiSecure&&!request.https)HTTP_TRY(diagnose(0x29a,"_request.isHttps()"));
    if(client_class==HttpClientClass::Wifi&&request.https)HTTP_TRY(diagnose(0x2a1,"!_request.isHttps()"));
    if(connected!=1){HTTP_TRY(effect(HttpEffect::SocketError,&socket_error));HTTP_TRY(log(ConsoleLevel::Error,0x2ab,"client->connect() failed [ret = %d] [errno = %d]",static_cast<std::int32_t>(connected),static_cast<std::int32_t>(socket_error)));failure=HttpClientErrorId::ConnectError;return HttpResult::Ok;}
    HTTP_TRY(log(ConsoleLevel::Info,0x2af,"After client->connect()"));HTTP_TRY(effect(HttpEffect::FreeHeap,&heap));HTTP_TRY(log(ConsoleLevel::Info,0,"xPortGetFreeHeapSize() = %lu",static_cast<unsigned long>(heap)));
    if(heap<20000)HTTP_TRY(log(ConsoleLevel::Warning,0x2b4,"xPortGetFreeHeapSize() < 20000"));
    if(heap<=15000)HTTP_TRY(diagnose(0x2b6,"freeHeapAfterConnect > 15000"));
    clear_buffer();HTTP_TRY(append(method));HTTP_TRY(append(" "));HTTP_TRY(append(request.path.data()));HTTP_TRY(append(" HTTP/1.1\r\nHost: "));HTTP_TRY(append(request.host.data()));HTTP_TRY(append("\r\n"));
    bool written=false;HTTP_TRY(flush_buffer(written));failure=HttpClientErrorId::SendError;if(!written)return HttpResult::Ok;
    if(type&&*type){HTTP_TRY(append("Content-Type: "));HTTP_TRY(append(type));HTTP_TRY(append("\r\n"));HTTP_TRY(log(ConsoleLevel::Info,0x2c9,"%s OUT %s: %s",name,"Content-Type",type));}
    if(request.authorization[0]){HTTP_TRY(append("Authorization: "));HTTP_TRY(append(request.authorization.data()));HTTP_TRY(append("\r\n"));HTTP_TRY(log(ConsoleLevel::Info,0x2cf,"%s OUT %s: %s",name,"Authorization","****"));}
    if(!body)size=0;
    std::array<char,12> formatted{};std::array<char,8> length{};::snprintf(formatted.data(),formatted.size(),"%d",static_cast<std::int32_t>(size));copy(length,formatted.data());
    HTTP_TRY(append("Content-Length: "));HTTP_TRY(append(length.data()));HTTP_TRY(append("\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n"));
    HTTP_TRY(flush_buffer(written));if(!written)return HttpResult::Ok;
    if(body&&size){if(trace)HTTP_TRY(log(ConsoleLevel::Info,0x2ed,"%s OUT %d bytes of payload",name,static_cast<std::int32_t>(size)));HttpAction action(HttpEffect::Write);action.data=static_cast<const std::uint8_t*>(body);action.size=size;HTTP_TRY(perform(action));}
    HTTP_TRY(effect(HttpEffect::Flush));failure=HttpClientErrorId::None;return HttpResult::Ok;
}
HttpResult HttpClient::send(const char* method,const char* type,const void* body,std::size_t size,bool& accepted){
    accepted=false;if(!services.millis)return HttpResult::MissingService;
    timing=HttpTiming{};timing.trace=trace;timing.name=name;timing.send_date=services.millis(services.context);
    if(trace)HTTP_TRY(log(ConsoleLevel::Info,0x188,"%s %s BEGIN",name,method));
    if(!std::strcmp(method,"POST")&&(!body||!size))HTTP_TRY(log(ConsoleLevel::Warning,0x38c,"HttpClient::POST() suspicious arg (POST without data)"));
    if(!body&&size){
        HTTP_TRY(log(ConsoleLevel::Error,!std::strcmp(method,"POST")?0x38f:0x39c,"HttpClient::%s() has invalid arguments",method));
        HTTP_TRY(set_error(HttpClientErrorId::InvalidArg));HTTP_TRY(transition(HttpState::Idle));
        const auto now=services.millis(services.context);if(now<=timing.send_date)HTTP_TRY(diagnose(0x18e,"sendTime < now"));timing.send_duration=now-timing.send_date;if(trace)HTTP_TRY(log(ConsoleLevel::Info,0x190,"%s %s END   %d",name,method,static_cast<std::int32_t>(timing.send_duration)));return HttpResult::Ok;
    }
    if(state!=HttpState::Initialized)HTTP_TRY(diagnose(0x2f7,"_state == State::Initialized"));
    if(error!=HttpClientErrorId::None)HTTP_TRY(diagnose(0x2f8,"!hasError()"));
    if(stalled)HTTP_TRY(diagnose(0x2f9,"!_stalled"));
    if(!client_present)HTTP_TRY(diagnose(0x2fa,"_client != nullptr"));
    if(client_class==HttpClientClass::None)HTTP_TRY(diagnose(0x2fb,"_clientClass != ClientClass::None"));
    if(!request.valid())HTTP_TRY(diagnose(0x2fc,"_request.isValid()"));
    if(state==HttpState::Initialized&&error==HttpClientErrorId::None&&client_present&&client_class!=HttpClientClass::None&&request.valid()){
        if(trace)HTTP_TRY(request.out(services.diagnostics));
        HttpClientErrorId failure;HTTP_TRY(transmit(method,type,body,size,failure));
        if(failure!=HttpClientErrorId::None)HTTP_TRY(set_error(failure));
        HTTP_TRY(transition(failure==HttpClientErrorId::None?HttpState::WaitingReply:HttpState::Idle));accepted=failure==HttpClientErrorId::None;
    }else HTTP_TRY(log(ConsoleLevel::Error,0x2fe,"_tryStart(): trying to sent request in wrong state"));
    const auto now=services.millis(services.context);if(now<=timing.send_date)HTTP_TRY(diagnose(0x18e,"sendTime < now"));timing.send_duration=now-timing.send_date;if(trace)HTTP_TRY(log(ConsoleLevel::Info,0x190,"%s %s END   %d",name,method,static_cast<std::int32_t>(timing.send_duration)));return HttpResult::Ok;
}
HttpResult HttpClient::get(bool& accepted){return send("GET",nullptr,nullptr,0,accepted);}
HttpResult HttpClient::post(const char* type,const void* body,std::size_t size,bool& accepted){
    return send("POST",type,body,size,accepted);
}
HttpResult HttpClient::put(const char* type,const void* body,std::size_t size,bool& accepted){
    return send("PUT",type,body,size,accepted);
}
HttpResult HttpClient::read_headers(std::uint32_t now){
    bool complete=false;
    while(error==HttpClientErrorId::None){
        std::uint32_t available=0;HTTP_TRY(effect(HttpEffect::Available,&available));if(!available)break;
        deadline=now+header_timeout_ms;std::uint32_t byte=0;HTTP_TRY(effect(HttpEffect::ReadByte,&byte));
        if(byte>255){HTTP_TRY(set_error(HttpClientErrorId::ReadError));break;}stalled=false;
        if(byte){
            if(buffer_frozen)HTTP_TRY(diagnose(0x9a,"!_isFrozen","Buffer.h"));
            else if(!buffer_remaining){if(!buffer_truncated){buffer_truncated=true;HTTP_TRY(buffer_log(0xa3,"Buffer truncated"));}}
            else{buffer[buffer_length++]=static_cast<std::uint8_t>(byte);--buffer_remaining;buffer[buffer_length]=0;}
        }
        if(buffer_truncated){HTTP_TRY(set_error(HttpClientErrorId::ResponseTooBig));break;}
        if(byte!=10)continue;
        if(buffer_frozen)HTTP_TRY(diagnose(0xc0,"!_isFrozen","Buffer.h"));
        while(buffer_length&&(buffer[buffer_length-1]==9||buffer[buffer_length-1]==10||buffer[buffer_length-1]==13||buffer[buffer_length-1]==32))buffer[--buffer_length]=0;
        if(!buffer_length){complete=true;break;}
        const char* line=reinterpret_cast<const char*>(buffer.data());
        if(trace)HTTP_TRY(log(ConsoleLevel::Info,0,"HDR = %s",line));
        if(starts(line,"HTTP/1.1 ")||starts(line,"HTTP/1.0 ")){
            std::array<char,4> status{};std::memcpy(status.data(),line+9,std::min<std::size_t>(3,buffer_length>9?buffer_length-9:0));response.status=decimal(status.data());
            if(response.status!=200){HTTP_TRY(log(ConsoleLevel::Error,0x3f0,"Got a non 200 status code from server"));HTTP_TRY(set_error(HttpClientErrorId::HttpStatusError));break;}
        }
        if(starts(line,"Content-Length: "))response.content_length=decimal(line+16);
        else if(starts(line,"Content-Type: ")){HttpResult copied=HttpResult::Ok;response.content_type_truncated=checked_copy(response.content_type,line+14,std::strlen(line+14),&services.diagnostics,copied);HTTP_TRY(copied);}
        clear_buffer();
    }
    if(!complete){std::uint32_t connected=0;HTTP_TRY(effect(HttpEffect::Connected,&connected));if(!connected){HTTP_TRY(effect(HttpEffect::SocketError,&socket_error));HTTP_TRY(log(ConsoleLevel::Error,0x406,"%s Premature connection close [errno = %d]",name,static_cast<std::int32_t>(socket_error)));HTTP_TRY(set_error(HttpClientErrorId::PrematureClose));}}
    if(error!=HttpClientErrorId::None)return transition(HttpState::Idle);
    if(!complete){if(!stalled)HTTP_TRY(log(ConsoleLevel::Warning,0x40f,"%s Client is stalled",name));stalled=true;if(deadline<now)HTTP_TRY(set_error(HttpClientErrorId::ReadingHeadersTimeout));return HttpResult::Ok;}
    if(!receiver.headers)return HttpResult::MissingService;
    const bool accepted=receiver.headers(receiver.context,response);if(!accepted)HTTP_TRY(set_error(HttpClientErrorId::ApplicationError));
    return transition(accepted?HttpState::Body:HttpState::Idle);
}
HttpResult HttpClient::tick(std::uint32_t now){
    std::uint32_t available=0;
    switch(state){
    case HttpState::WaitingReply:
        HTTP_TRY(effect(HttpEffect::Available,&available));if(available)return transition(HttpState::Headers);
        if(deadline<now){HTTP_TRY(set_error(HttpClientErrorId::WaitingReplyTimeout));return transition(HttpState::Idle);}break;
    case HttpState::Headers:return read_headers(now);
    case HttpState::Body:{
        HTTP_TRY(effect(HttpEffect::Available,&available));const std::int32_t count=std::min<std::int32_t>(static_cast<std::int32_t>(available),1024);
        if(count>0){
            HttpAction action(HttpEffect::ReadBytes);action.destination=buffer.data();action.size=static_cast<std::size_t>(count);buffer_frozen=true;HTTP_TRY(perform(action));
            if(!receiver.body)return HttpResult::MissingService;
            receiver.body(receiver.context,buffer.data(),static_cast<std::size_t>(count));content_received+=static_cast<std::uint32_t>(count);
            if(trace)HTTP_TRY(log(ConsoleLevel::Info,0x425,"%s IN Chunk [%d bytes]",name,count));
        }
        HTTP_TRY(effect(HttpEffect::Available,&available));std::uint32_t connected=1;
        if(!available)HTTP_TRY(effect(HttpEffect::Connected,&connected));
        if(!available&&!connected){
            if(trace)HTTP_TRY(log(ConsoleLevel::Info,0x429,"%s Connection closed",name));
            if(response.content_length>=0&&content_received!=static_cast<std::uint32_t>(response.content_length))
                HTTP_TRY(log(ConsoleLevel::Warning,0x42c,"Connection closed prematurely. Expected %d, Got %d",response.content_length,static_cast<std::int32_t>(content_received)));
            return transition(HttpState::Complete);
        }
        if(response.content_length>=0&&content_received>=static_cast<std::uint32_t>(response.content_length))return transition(HttpState::Complete);
        break;
    }
    case HttpState::Complete:
        if(trace)HTTP_TRY(log(ConsoleLevel::Info,0x435,"%s ContentReceived = %d bytes",name,static_cast<std::int32_t>(content_received)));
        if(!receiver.complete)return HttpResult::MissingService;
        receiver.complete(receiver.context);return transition(HttpState::Idle);
    default:break;
    }return HttpResult::Ok;
}
#undef HTTP_TRY
}} // namespace zseries::oem
