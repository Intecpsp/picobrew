#include "zseries/still_client.hpp"
#include "zseries/still_diagnostics.hpp"
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
namespace {
StillClientResult diagnostic_result(ConsoleResult result) noexcept {
    return result==ConsoleResult::Ok?StillClientResult::Ok:result==ConsoleResult::MissingService?StillClientResult::MissingService:StillClientResult::ServiceFailure;
}
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
std::int32_t still_form_encode(char* out,std::size_t capacity,const char* text,const ConsoleServices* services,ConsoleResult* result) noexcept {
    ConsoleResult status;std::uint32_t assertion=0;detail::StillDiagnosticSink diagnostics(services,status,assertion);
    if(result)*result=ConsoleResult::Ok;
    if(!out||capacity==0){
        diagnostics.assertion("CString.h",0xa9,"dest != nullptr && destsize > 0");if(result)*result=status;return -1;
    }
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
const char* still_error_text(const PicoStillState& state,std::uint32_t& assertion,const ConsoleServices* services,ConsoleResult* result) noexcept {
    ConsoleResult status;detail::StillDiagnosticSink diagnostics(services,status,assertion);
    if(!still_has_error(state)&&!diagnostics.assertion("IPicoStill.h",0x1a0,"hasError()")){if(result)*result=status;return nullptr;}
    if(result)*result=status;
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
StillClientResult StillClient::set_host(const char* text) noexcept {
    detail::StillDiagnosticSink diagnostics(&services.diagnostics,diagnostics_result,assertion_line);
    host.fill(0);host_truncated=false;
    if(!diagnostics.truncate(host_truncated,text?std::strlen(text):0,host.size()-1))return diagnostic_result(diagnostics_result);
    host_truncated=copy(host,text);
    if((host_truncated||host[0]==0)&&!diagnostics.assertion("PicoStill.h",0x2a3,"!_picoStillIp.isTruncated() && !_picoStillIp.isEmpty()"))return diagnostic_result(diagnostics_result);
    return StillClientResult::Ok;
}
StillClientResult StillClient::build_uri(Command command) noexcept {
    detail::StillDiagnosticSink diagnostics(&services.diagnostics,diagnostics_result,assertion_line);
    pending=command;uri_truncated=copy(uri,"http://");uri_truncated=append(uri,host.data())||uri_truncated;
    uri_truncated=append(uri,"/")||uri_truncated;uri_truncated=append(uri,to_string(command))||uri_truncated;
    diagnostics.print(ConsoleLevel::Info,"STILLH","PicoStill.h",0x23f,"Uri '%s'",uri.data());
    return diagnostic_result(diagnostics_result);
}
StillClientResult StillClient::mark_error() noexcept {error=true;retryable=http.error==10||http.socket_error==0x68||http.socket_error==0x71||http.socket_error==0x80;return StillClientResult::Ok;}
StillClientResult StillClient::tick() noexcept {
    detail::StillDiagnosticSink diagnostics(&services.diagnostics,diagnostics_result,assertion_line);
    if(!error&&http.error!=0){
        if(!diagnostics.log(ConsoleLevel::Error,"STILLH","PicoStill.h",0x286,"Underlying HttpClient has some error"))return diagnostic_result(diagnostics_result);
        return mark_error();
    }
    return StillClientResult::Ok;
}
StillClientResult StillClient::reset(){
    pending=Command::Empty;error=retryable=response_done=false;response=StillHttpResponse{};return effect(StillHttpEffect::Reset);
}
StillClientResult StillClient::command(Command value){
    detail::StillDiagnosticSink diagnostics(&services.diagnostics,diagnostics_result,assertion_line);
    if(pending!=Command::Empty){
        if(!diagnostics.log(ConsoleLevel::Error,"STILLH","PicoStill.h",0x2cb,"send() while hasPending() is true. Aborting previous command"))return diagnostic_result(diagnostics_result);
        CLIENT_TRY(effect(StillHttpEffect::Cancel));
    }
    CLIENT_TRY(reset());CLIENT_TRY(build_uri(value));
    if(uri_truncated&&!diagnostics.assertion("PicoStill.h",0x244,"!_uri.isTruncated()"))return diagnostic_result(diagnostics_result);
    if(http.state!=0&&!diagnostics.assertion("PicoStill.h",0x245,"_hClient.isDone()"))return diagnostic_result(diagnostics_result);
    StillHttpAction action(StillHttpEffect::Configure);action.uri=uri.data();std::uint32_t result=0;CLIENT_TRY(perform(action,&result));
    if(result){CLIENT_TRY(effect(StillHttpEffect::Get,&result));}
    if(!result)mark_error();
    return StillClientResult::Ok;
}
StillClientResult StillClient::configure(const WifiCredentials& credentials){
    detail::StillDiagnosticSink diagnostics(&services.diagnostics,diagnostics_result,assertion_line);
    if(pending!=Command::Empty){
        if(!diagnostics.log(ConsoleLevel::Error,"STILLH","PicoStill.h",0x2aa,"sendConfigure() while hasPending() is true. Aborting previous command"))return diagnostic_result(diagnostics_result);
        CLIENT_TRY(effect(StillHttpEffect::Cancel));
    }
    CLIENT_TRY(reset());CLIENT_TRY(build_uri(Command::configure));std::array<char,257> form{};std::array<char,64> encoded{};
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
    detail::StillDiagnosticSink diagnostics(&services.diagnostics,diagnostics_result,assertion_line);
    if(value.status<1&&!diagnostics.assertion("PicoStill.h",0x2da,"response.hasHttpStatus()"))return false;
    response=value;body.fill(0);body_length=0;body_truncated=false;return true;
}
StillClientResult StillClient::receive_body(const void* data,std::size_t size) noexcept {
    detail::StillDiagnosticSink diagnostics(&services.diagnostics,diagnostics_result,assertion_line);
    if(!data&&size){
        if(!diagnostics.assertion("Buffer.h",0x5e,"data != nullptr || datalen == 0"))return diagnostic_result(diagnostics_result);
        return StillClientResult::InvalidBody;
    }
    const auto count=std::min(size,body.size()-1-body_length);
    if(!diagnostics.truncate(body_truncated,size,body.size()-1-body_length))return diagnostic_result(diagnostics_result);
    if(count)std::memcpy(body.data()+body_length,data,count);
    body_length+=count;body[body_length]=0;return StillClientResult::Ok;
}
StillClientResult StillClient::complete(std::uint32_t now) noexcept {
    detail::StillDiagnosticSink diagnostics(&services.diagnostics,diagnostics_result,assertion_line);
    if(!diagnostics.print(ConsoleLevel::Info,"STILLH","",0,"RESPONSE = %s",body.data()))return diagnostic_result(diagnostics_result);
    if(pending==Command::Empty&&!diagnostics.assertion("PicoStill.h",0x2e4,"_pendingCmd != Command::None"))return diagnostic_result(diagnostics_result);
    response_done=true;error=response.status!=200;retryable=response.status==404;still.response_date=now;
    if(body_length<3||body[0]!='#'||body[body_length-1]!='#'){
        if(!diagnostics.log(ConsoleLevel::Error,"STIL","PicoStill.h",0x2f1,"Response from PicoStill"))return diagnostic_result(diagnostics_result);
        error=true;return StillClientResult::Ok;
    }
    if(pending==Command::stillip||pending==Command::version){
        auto& destination=pending==Command::stillip?still.ip:still.version;bool truncated=false;
        if(!diagnostics.truncate(truncated,std::strlen(body.data()+1),destination.size()-1))return diagnostic_result(diagnostics_result);
        copy(destination,body.data()+1);
        const auto length=std::strlen(destination.data());if(length)destination[length-1]=0;
    }else if(pending==Command::data){
        samples.assertion_line=assertion_line;samples.parse(now,body.data(),body_length,&services.diagnostics);
        assertion_line=samples.assertion_line;diagnostics_result=samples.diagnostics_result;
        if(diagnostics_result!=ConsoleResult::Ok)return diagnostic_result(diagnostics_result);
        samples.update_statistics(now,&services.diagnostics);assertion_line=samples.assertion_line;diagnostics_result=samples.diagnostics_result;
        if(diagnostics_result!=ConsoleResult::Ok)return diagnostic_result(diagnostics_result);
        still.sample_date=samples.current.timestamp;still.sample_delta_ms=samples.current.delta_ms;still.error=samples.current.error;
    }
    return StillClientResult::Ok;
}
void StillClient::observe(PicoStillObserved& out) const noexcept {
    out.pending=pending;out.client_error=error;out.client_retryable=retryable;out.client_done=response_done;
}
#undef CLIENT_TRY
}} // namespace zseries::oem
