#include "zseries/http_client.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace zseries { namespace oem {
namespace {
template<std::size_t N> bool copy(std::array<char,N>& out,const char* text,std::size_t length) noexcept {
    out.fill(0);if(text)std::memcpy(out.data(),text,std::min(length,N-1));return length>=N;
}
template<std::size_t N> bool copy(std::array<char,N>& out,const char* text) noexcept {return copy(out,text,text?std::strlen(text):0);}
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
    http=https=host_truncated=path_truncated=authorization_truncated=false;port=default_http_port;address=0;host.fill(0);path.fill(0);authorization.fill(0);
}
bool HttpRequest::parse(const char* uri) noexcept {
    reset();if(!uri||!*uri)return false;http=starts(uri,"http://");https=starts(uri,"https://");if(http==https)return false;
    port=https?default_https_port:default_http_port;const char* authority=uri+(https?8:7);const char* colon=std::strchr(authority,':');const char* slash=std::strchr(authority,'/');
    // The original subtracts these pointers and can read past malformed URIs
    // when a colon occurs in the path. Reject that undefined source span.
    if(colon&&slash&&colon>slash)return false;
    if(colon){
        host_truncated=copy(host,authority,static_cast<std::size_t>(colon-authority));std::array<char,17> number{};
        copy(number,colon+1,slash?static_cast<std::size_t>(slash-colon):std::strlen(colon+1));port=static_cast<std::uint16_t>(decimal(number.data()));
    }else host_truncated=copy(host,authority,slash?static_cast<std::size_t>(slash-authority):std::strlen(authority));
    if(slash)path_truncated=copy(path,slash);
    if(!ipv4(host.data(),address))address=0;
    return valid();
}
#define HTTP_TRY(expr) do { const HttpResult r=(expr);if(r!=HttpResult::Ok)return r; } while(false)
HttpResult HttpClient::perform(const HttpAction& action,std::uint32_t* result){
    if(!services.perform)return HttpResult::MissingService;
    std::uint32_t value=0;
    if(!services.perform(services.context,action,value))return HttpResult::ServiceFailure;
    if(result)*result=value;
    return HttpResult::Ok;
}
HttpResult HttpClient::effect(HttpEffect value,std::uint32_t* result){return perform(HttpAction(value),result);}
void HttpClient::clear_buffer() noexcept {buffer.fill(0);buffer_length=0;buffer_remaining=1024;buffer_truncated=buffer_frozen=false;}
void HttpClient::append(const char* text) noexcept {
    if(buffer_frozen){assertion_line=0x5d;return;}if(!text)return;
    const std::size_t length=std::strlen(text),count=std::min(length,buffer_remaining);
    if(length>buffer_remaining){if(buffer_truncated&&buffer_remaining!=0)assertion_line=0x68;buffer_truncated=true;}
    std::memcpy(buffer.data()+buffer_length,text,count);buffer_length+=count;buffer_remaining-=count;buffer[buffer_length]=0;
}
void HttpClient::reset() noexcept {
    if(state!=HttpState::Idle){assertion_line=0x349;return;}
    request.reset();response=HttpResponse{};clear_buffer();content_received=0;client_present=false;client_class=HttpClientClass::None;receiver=HttpReceiver{};
    error=HttpClientErrorId::None;socket_error=0;stalled=false;
}
void HttpClient::set_error(HttpClientErrorId value) noexcept {
    if(error!=HttpClientErrorId::None)assertion_line=0x256;
    if(static_cast<std::uint32_t>(value)>=HttpClientErrorIdCount)assertion_line=0x257;else if(value==HttpClientErrorId::None)assertion_line=0x258;
    error=value;
}
HttpResult HttpClient::transition(HttpState next){
    if(state==next)return HttpResult::Ok;
    if(!services.millis)return HttpResult::MissingService;
    state=next;state_date=services.millis(services.context);deadline=0;
    switch(next){
    case HttpState::Idle:
        if(client_present){HTTP_TRY(effect(HttpEffect::Stop));client_present=false;client_class=HttpClientClass::None;}break;
    case HttpState::WaitingReply:deadline=state_date+reply_timeout_ms;break;
    case HttpState::Headers:{const auto now=services.millis(services.context);if(timing.send_date==0)assertion_line=0x196;if(now<=timing.send_date)assertion_line=0x197;
        timing.headers_date=now;timing.reply_duration=now-timing.send_date;deadline=state_date+header_timeout_ms;break;}
    case HttpState::Body:{const auto now=services.millis(services.context);if(now<=timing.headers_date)assertion_line=0x19f;timing.body_date=now;break;}
    case HttpState::Complete:{const auto now=services.millis(services.context);if(timing.headers_date!=0){if(now<=timing.headers_date)assertion_line=0x1aa;timing.body_duration=now-timing.headers_date;}break;}
    default:break;
    }return HttpResult::Ok;
}
HttpResult HttpClient::cancel(){return state==HttpState::Idle?HttpResult::Ok:transition(HttpState::Idle);}
HttpResult HttpClient::configure(const char* uri,const HttpReceiver& sink,HttpClientClass supplied,bool& accepted){
    accepted=false;
    if(state!=HttpState::Idle){set_error(HttpClientErrorId::InvalidState);return HttpResult::Ok;}
    if(client_present)assertion_line=0x357;
    reset();request.parse(uri);
    if(!request.valid()){set_error(HttpClientErrorId::InvalidRequest);return HttpResult::Ok;}
    HttpClientClass selected=supplied;
    if(selected==HttpClientClass::None){HttpAction action(HttpEffect::AcquireClient);action.argument=request.https;action.acquired_class=&selected;std::uint32_t available=0;HTTP_TRY(perform(action,&available));
        if(!available||selected==HttpClientClass::None){set_error(HttpClientErrorId::SystemClientError);return HttpResult::Ok;}}
    client_present=true;client_class=selected;receiver=sink;HTTP_TRY(transition(HttpState::Initialized));if(error!=HttpClientErrorId::None)assertion_line=0x36e;accepted=true;return HttpResult::Ok;
}
bool HttpClient::set_authorization(const char* text) noexcept {
    if(state!=HttpState::Initialized)return false;
    request.authorization_truncated=copy(request.authorization,text);return true;
}
HttpResult HttpClient::flush_buffer(bool& written){
    written=false;if(buffer_truncated)return HttpResult::Ok;if(!buffer_length){written=true;return HttpResult::Ok;}
    HttpAction action(HttpEffect::Write);action.data=buffer.data();action.size=buffer_length;buffer_frozen=true;std::uint32_t count=0;HTTP_TRY(perform(action,&count));
    if(count==buffer_length){clear_buffer();written=true;}return HttpResult::Ok;
}
HttpResult HttpClient::transmit(const char* method,const char* type,const void* body,std::size_t size,HttpClientErrorId& failure){
    failure=HttpClientErrorId::BrokenInvariant;if(!client_present)assertion_line=0x273;if(client_class==HttpClientClass::None)assertion_line=0x274;
    if(!client_present||client_class==HttpClientClass::None)return HttpResult::Ok;
    if(socket_error)assertion_line=0x27f;
    std::uint32_t heap=0;HTTP_TRY(effect(HttpEffect::FreeHeap,&heap));if(heap<=60000)assertion_line=0x284;
    if(client_class==HttpClientClass::Ethernet){HttpAction action(HttpEffect::PrepareEthernet);action.argument=request.https;HTTP_TRY(perform(action));}
    else if(client_class!=HttpClientClass::Wifi&&client_class!=HttpClientClass::WifiSecure)return HttpResult::Ok;
    HttpAction connect(request.address?HttpEffect::ConnectAddress:HttpEffect::ConnectHost);connect.argument=request.port;connect.second=request.address;connect.text=request.address?nullptr:request.host.data();std::uint32_t connected=0;
    HTTP_TRY(perform(connect,&connected));
    if(client_class==HttpClientClass::WifiSecure&&!request.https)assertion_line=0x29a;
    if(client_class==HttpClientClass::Wifi&&request.https)assertion_line=0x2a1;
    if(connected!=1){HTTP_TRY(effect(HttpEffect::SocketError,&socket_error));failure=HttpClientErrorId::ConnectError;return HttpResult::Ok;}
    HTTP_TRY(effect(HttpEffect::FreeHeap,&heap));if(heap<=15000)assertion_line=0x2b6;
    clear_buffer();append(method);append(" ");append(request.path.data());append(" HTTP/1.1\r\nHost: ");append(request.host.data());append("\r\n");
    bool written=false;HTTP_TRY(flush_buffer(written));failure=HttpClientErrorId::SendError;if(!written)return HttpResult::Ok;
    if(type&&*type){append("Content-Type: ");append(type);append("\r\n");}
    if(request.authorization[0]){append("Authorization: ");append(request.authorization.data());append("\r\n");}
    if(!body)size=0;
    std::array<char,12> formatted{};std::array<char,8> length{};::snprintf(formatted.data(),formatted.size(),"%d",static_cast<std::int32_t>(size));copy(length,formatted.data());
    append("Content-Length: ");append(length.data());append("\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n");
    HTTP_TRY(flush_buffer(written));if(!written)return HttpResult::Ok;
    if(body&&size){HttpAction action(HttpEffect::Write);action.data=static_cast<const std::uint8_t*>(body);action.size=size;HTTP_TRY(perform(action));}
    HTTP_TRY(effect(HttpEffect::Flush));failure=HttpClientErrorId::None;return HttpResult::Ok;
}
HttpResult HttpClient::send(const char* method,const char* type,const void* body,std::size_t size,bool& accepted){
    accepted=false;if(!services.millis)return HttpResult::MissingService;
    timing=HttpTiming{};timing.send_date=services.millis(services.context);
    if(!body&&size){
        set_error(HttpClientErrorId::InvalidArg);HTTP_TRY(transition(HttpState::Idle));
        const auto now=services.millis(services.context);if(now<=timing.send_date)assertion_line=0x18e;timing.send_duration=now-timing.send_date;return HttpResult::Ok;
    }
    if(state!=HttpState::Initialized)assertion_line=0x2f7;
    if(error!=HttpClientErrorId::None)assertion_line=0x2f8;
    if(stalled)assertion_line=0x2f9;
    if(!client_present)assertion_line=0x2fa;
    if(client_class==HttpClientClass::None)assertion_line=0x2fb;
    if(!request.valid())assertion_line=0x2fc;
    if(state==HttpState::Initialized&&error==HttpClientErrorId::None&&client_present&&client_class!=HttpClientClass::None&&request.valid()){
        HttpClientErrorId failure;HTTP_TRY(transmit(method,type,body,size,failure));
        if(failure!=HttpClientErrorId::None)set_error(failure);
        HTTP_TRY(transition(failure==HttpClientErrorId::None?HttpState::WaitingReply:HttpState::Idle));accepted=failure==HttpClientErrorId::None;
    }
    const auto now=services.millis(services.context);if(now<=timing.send_date)assertion_line=0x18e;timing.send_duration=now-timing.send_date;return HttpResult::Ok;
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
        if(byte>255){set_error(HttpClientErrorId::ReadError);break;}stalled=false;
        if(byte){
            if(buffer_frozen)assertion_line=0x9a;
            else if(!buffer_remaining)buffer_truncated=true;
            else{buffer[buffer_length++]=static_cast<std::uint8_t>(byte);--buffer_remaining;buffer[buffer_length]=0;}
        }
        if(buffer_truncated){set_error(HttpClientErrorId::ResponseTooBig);break;}
        if(byte!=10)continue;
        if(buffer_frozen)assertion_line=0xc0;
        while(buffer_length&&(buffer[buffer_length-1]==9||buffer[buffer_length-1]==10||buffer[buffer_length-1]==13||buffer[buffer_length-1]==32))buffer[--buffer_length]=0;
        if(!buffer_length){complete=true;break;}
        const char* line=reinterpret_cast<const char*>(buffer.data());
        if(starts(line,"HTTP/1.1 ")||starts(line,"HTTP/1.0 ")){
            std::array<char,4> status{};std::memcpy(status.data(),line+9,std::min<std::size_t>(3,buffer_length>9?buffer_length-9:0));response.status=decimal(status.data());
            if(response.status!=200){set_error(HttpClientErrorId::HttpStatusError);break;}
        }
        if(starts(line,"Content-Length: "))response.content_length=decimal(line+16);
        else if(starts(line,"Content-Type: "))response.content_type_truncated=copy(response.content_type,line+14);
        clear_buffer();
    }
    if(!complete){std::uint32_t connected=0;HTTP_TRY(effect(HttpEffect::Connected,&connected));if(!connected){HTTP_TRY(effect(HttpEffect::SocketError,&socket_error));set_error(HttpClientErrorId::PrematureClose);}}
    if(error!=HttpClientErrorId::None)return transition(HttpState::Idle);
    if(!complete){stalled=true;if(deadline<now)set_error(HttpClientErrorId::ReadingHeadersTimeout);return HttpResult::Ok;}
    if(!receiver.headers)return HttpResult::MissingService;
    const bool accepted=receiver.headers(receiver.context,response);if(!accepted)set_error(HttpClientErrorId::ApplicationError);
    return transition(accepted?HttpState::Body:HttpState::Idle);
}
HttpResult HttpClient::tick(std::uint32_t now){
    std::uint32_t available=0;
    switch(state){
    case HttpState::WaitingReply:
        HTTP_TRY(effect(HttpEffect::Available,&available));if(available)return transition(HttpState::Headers);
        if(deadline<now){set_error(HttpClientErrorId::WaitingReplyTimeout);return transition(HttpState::Idle);}break;
    case HttpState::Headers:return read_headers(now);
    case HttpState::Body:{
        HTTP_TRY(effect(HttpEffect::Available,&available));const std::int32_t count=std::min<std::int32_t>(static_cast<std::int32_t>(available),1024);
        if(count>0){
            HttpAction action(HttpEffect::ReadBytes);action.destination=buffer.data();action.size=static_cast<std::size_t>(count);buffer_frozen=true;HTTP_TRY(perform(action));
            if(!receiver.body)return HttpResult::MissingService;
            receiver.body(receiver.context,buffer.data(),static_cast<std::size_t>(count));content_received+=static_cast<std::uint32_t>(count);
        }
        HTTP_TRY(effect(HttpEffect::Available,&available));std::uint32_t connected=1;
        if(!available)HTTP_TRY(effect(HttpEffect::Connected,&connected));
        if((!available&&!connected)||(response.content_length>=0&&content_received>=static_cast<std::uint32_t>(response.content_length)))return transition(HttpState::Complete);
        break;
    }
    case HttpState::Complete:
        if(!receiver.complete)return HttpResult::MissingService;
        receiver.complete(receiver.context);return transition(HttpState::Idle);
    default:break;
    }return HttpResult::Ok;
}
#undef HTTP_TRY
}} // namespace zseries::oem
