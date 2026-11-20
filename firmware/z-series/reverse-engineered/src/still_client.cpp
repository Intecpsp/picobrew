#include "zseries/still_client.hpp"
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
namespace {
template<std::size_t N> bool copy(std::array<char,N>& out,const char* text) noexcept {
    out.fill(0);if(!text)return false;const auto length=std::strlen(text);
    std::memcpy(out.data(),text,std::min(length,N-1));return length>=N;
}
template<std::size_t N> bool append(std::array<char,N>& out,const char* text) noexcept {
    if(!text)return false;
    const auto length=std::strlen(out.data()),count=std::strlen(text),room=N-1-length;
    std::memcpy(out.data()+length,text,std::min(count,room));return count>room;
}
}
std::int32_t still_form_encode(char* out,std::size_t capacity,const char* text) noexcept {
    if(!out||capacity==0)return -1;
    const std::size_t maximum=capacity-1;std::size_t written=0;
    for(std::size_t i=0;text&&text[i]&&written<maximum;++i){
        const unsigned char c=static_cast<unsigned char>(text[i]);
        const bool escaped=(c>=32&&c<=47)||(c>=58&&c<=64)||(c>=91&&c<=96)||(c>=123&&c<=126)||c==13||c==10;
        if(escaped){
            if(maximum<=written+3)return -2;
            static const char hex[]="0123456789ABCDEF";out[written]='%';
            if(c>=16){out[written+1]=hex[c>>4];out[written+2]=hex[c&15];out[written+3]=0;}
            else{out[written+1]=hex[c];out[written+2]=0;}
            written+=3;
        }else out[written++]=static_cast<char>(c);
    }
    out[written]=0;return 0;
}
bool still_has_error(const PicoStillState& state) noexcept {
    return state.id==PicoStillStateId::Error||state.polling_stopped||state.error[0]!=0;
}
const char* still_error_text(const PicoStillState& state,std::uint32_t& assertion) noexcept {
    if(!still_has_error(state))assertion=0x1a0;
    if(state.polling_stopped)return "Connectivity Error";
    if(state.error[0])return state.error.data();
    return state.id==PicoStillStateId::Error?"General error":"";
}
#define CLIENT_TRY(expr) do { const StillClientResult r=(expr);if(r!=StillClientResult::Ok)return r; } while(false)
StillClientResult StillClient::perform(const StillHttpAction& action,std::uint32_t* result){
    if(!services.perform)return StillClientResult::MissingService;
    std::uint32_t value=0;
    if(!services.perform(services.context,action,value))return StillClientResult::ServiceFailure;
    if(result)*result=value;
    return StillClientResult::Ok;
}
StillClientResult StillClient::effect(StillHttpEffect value,std::uint32_t* result){return perform(StillHttpAction(value),result);}
void StillClient::set_host(const char* text) noexcept {
    host_truncated=copy(host,text);if(host_truncated||host[0]==0)assertion_line=0x2a3;
}
void StillClient::build_uri(Command command) noexcept {
    pending=command;uri_truncated=copy(uri,"http://");uri_truncated=append(uri,host.data())||uri_truncated;
    uri_truncated=append(uri,"/")||uri_truncated;uri_truncated=append(uri,to_string(command))||uri_truncated;
}
void StillClient::mark_error() noexcept {error=true;retryable=http.error==10||http.socket_error==0x68||http.socket_error==0x71||http.socket_error==0x80;}
void StillClient::tick() noexcept {if(!error&&http.error!=0)mark_error();}
StillClientResult StillClient::reset(){
    pending=Command::Empty;error=retryable=response_done=false;response=StillHttpResponse{};return effect(StillHttpEffect::Reset);
}
StillClientResult StillClient::command(Command value){
    if(pending!=Command::Empty)CLIENT_TRY(effect(StillHttpEffect::Cancel));
    CLIENT_TRY(reset());build_uri(value);
    if(uri_truncated)assertion_line=0x244;
    if(http.state!=0)assertion_line=0x245;
    StillHttpAction action(StillHttpEffect::Configure);action.uri=uri.data();std::uint32_t result=0;CLIENT_TRY(perform(action,&result));
    if(result){CLIENT_TRY(effect(StillHttpEffect::Get,&result));}
    if(!result)mark_error();
    return StillClientResult::Ok;
}
StillClientResult StillClient::configure(const WifiCredentials& credentials){
    if(pending!=Command::Empty)CLIENT_TRY(effect(StillHttpEffect::Cancel));
    CLIENT_TRY(reset());build_uri(Command::configure);std::array<char,257> form{};std::array<char,64> encoded{};
    copy(form,"ssid=");
    if(still_form_encode(encoded.data(),encoded.size(),credentials.ssid.data())!=0)return StillClientResult::InvalidCredentialsEncoding;
    append(form,encoded.data());append(form,"&pass=");encoded.fill(0);
    if(still_form_encode(encoded.data(),encoded.size(),credentials.password.data())!=0)return StillClientResult::InvalidCredentialsEncoding;
    append(form,encoded.data());StillHttpAction action(StillHttpEffect::Configure);action.uri=uri.data();std::uint32_t result=0;CLIENT_TRY(perform(action,&result));
    if(!result){error=true;return StillClientResult::Ok;}
    action.effect=StillHttpEffect::Post;action.uri=nullptr;action.content_type="application/x-www-form-urlencoded";
    action.body=form.data();action.body_length=std::strlen(form.data());CLIENT_TRY(perform(action,&result));if(!result)mark_error();return StillClientResult::Ok;
}
bool StillClient::receive_headers(const StillHttpResponse& value) noexcept {
    if(value.status<1)assertion_line=0x2da;
    response=value;body.fill(0);body_length=0;body_truncated=false;return true;
}
StillClientResult StillClient::receive_body(const void* data,std::size_t size) noexcept {
    if(!data&&size){assertion_line=0x5e;return StillClientResult::InvalidBody;}
    const auto count=std::min(size,body.size()-1-body_length);body_truncated=body_truncated||count<size;
    if(count)std::memcpy(body.data()+body_length,data,count);
    body_length+=count;body[body_length]=0;return StillClientResult::Ok;
}
void StillClient::complete(std::uint32_t now) noexcept {
    if(pending==Command::Empty)assertion_line=0x2e4;
    response_done=true;error=response.status!=200;retryable=response.status==404;still.response_date=now;
    if(body_length<3||body[0]!='#'||body[body_length-1]!='#'){error=true;return;}
    if(pending==Command::stillip||pending==Command::version){
        auto& destination=pending==Command::stillip?still.ip:still.version;copy(destination,body.data()+1);
        const auto length=std::strlen(destination.data());if(length)destination[length-1]=0;
    }else if(pending==Command::data){
        samples.assertion_line=assertion_line;samples.parse(now,body.data(),body_length);samples.update_statistics(now);assertion_line=samples.assertion_line;
        still.sample_date=samples.current.timestamp;still.sample_delta_ms=samples.current.delta_ms;still.error=samples.current.error;
    }
}
void StillClient::observe(PicoStillObserved& out) const noexcept {
    out.pending=pending;out.client_error=error;out.client_retryable=retryable;out.client_done=response_done;
}
#undef CLIENT_TRY
}} // namespace zseries::oem
