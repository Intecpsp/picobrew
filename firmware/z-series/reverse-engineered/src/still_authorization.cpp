#include "zseries/still_authorization.hpp"
#include "zseries/pico_still.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace zseries { namespace oem {
namespace {
StillAuthorizationResult convert(HttpResult r){switch(r){case HttpResult::Ok:return StillAuthorizationResult::Ok;case HttpResult::MissingService:return StillAuthorizationResult::MissingService;case HttpResult::InvalidSpan:return StillAuthorizationResult::InvalidSpan;default:return StillAuthorizationResult::ServiceFailure;}}
bool headers(void* context,const HttpResponse& response){auto& self=*static_cast<StillAuthorization*>(context);bool accepted=false;self.callback_result=self.receive_headers(response,accepted);return accepted&&self.callback_result==StillAuthorizationResult::Ok;}
void body(void* context,const std::uint8_t* bytes,std::size_t count){auto& self=*static_cast<StillAuthorization*>(context);self.callback_result=self.receive_body(bytes,count);}
// Original receiver completion0x401bf1bc (adjusted0x401bf1c4) really returns.
void complete(void*){}
}
#define AUTH_TRY(expression) do { const auto result=(expression);if(result!=StillAuthorizationResult::Ok)return result; } while(false)
StillAuthorizationResult StillAuthorization::diagnose(const char* file,std::uint32_t line,const char* message){
    const auto& d=services.diagnostics;if(!d.log)return StillAuthorizationResult::MissingService;
    return d.log(d.context,std::strcmp(file,"Buffer.h")==0?ConsoleLevel::Warning:ConsoleLevel::Error,std::strcmp(file,"Buffer.h")==0?"BUF":"BSTIL",file,line,message)?StillAuthorizationResult::Ok:StillAuthorizationResult::ServiceFailure;
}
StillAuthorizationResult StillAuthorization::assertion(const char* file,std::uint32_t line,const char* expression){
    const auto& d=services.diagnostics;if(!d.assertion)return StillAuthorizationResult::MissingService;
    return d.assertion(d.context,file,line,expression)?StillAuthorizationResult::Ok:StillAuthorizationResult::ServiceFailure;
}
StillAuthorizationResult StillAuthorization::overflow(std::size_t dropped){
    char message[64];::snprintf(message,sizeof(message),"Dropping %u bytes",static_cast<unsigned>(dropped));
    return diagnose("Buffer.h",0x6a,message);
}
void StillAuthorization::reset_buffer() noexcept {buffer.fill(0);length=0;remaining=512;truncated=false;frozen=false;}
HttpReceiver StillAuthorization::receiver() noexcept {HttpReceiver r;r.context=this;r.headers=headers;r.body=body;r.complete=complete;return r;}
void StillAuthorization::observe(PicoStillObserved& value) const noexcept {value.authorization_pending=pending;value.authorized=record.authorized();value.cleaned=record.cleaned();}
StillAuthorizationResult StillAuthorization::request(const char* machine_uid,const char* still_uid,bool cleaned_ack){
    if(!authorization||!services.select_wifi_tls)return StillAuthorizationResult::MissingService;
    record=StillAuthorizationRecord{};
    if(pending)AUTH_TRY(assertion("PicoStill.h",0x93,"!_pending"));
    std::array<char,129> uri{};const char* base="https://www.picobrew.com/Vendors/input.cshtml?type=StillRequest&token=";
    const auto base_length=std::strlen(base);std::memcpy(uri.data(),base,base_length);
    const auto count=machine_uid?std::strlen(machine_uid):0;const auto copied=std::min(count,128-base_length);
    if(copied<count){AUTH_TRY(diagnose("Buffer.h",0x66,"Buffer truncated"));AUTH_TRY(overflow(count-copied));}
    if(copied)std::memcpy(uri.data()+base_length,machine_uid,copied);
    if(copied<count)AUTH_TRY(assertion("PicoStill.h",0x98,"!uri.isTruncated()"));
    bool accepted=false;callback_result=StillAuthorizationResult::Ok;
    AUTH_TRY(convert(client.configure(uri.data(),receiver(),HttpClientClass::WifiSecure,accepted)));
    if(!accepted){AUTH_TRY(diagnose("PicoStill.h",0x9b,"_hClient.init()"));pending=false;return StillAuthorizationResult::Ok;}
    if(!services.select_wifi_tls(services.context))return StillAuthorizationResult::ServiceFailure;
    client.set_authorization(authorization);
    AUTH_TRY(prepare_body(machine_uid,still_uid,cleaned_ack));
    AUTH_TRY(convert(client.post("application/json",buffer.data(),length,accepted)));
    if(!accepted)AUTH_TRY(diagnose("PicoStill.h",0xa6,"_hClient.POST()"));
    pending=accepted;return StillAuthorizationResult::Ok;
}
StillAuthorizationResult StillAuthorization::prepare_body(const char* machine_uid,const char* still_uid,bool cleaned_ack){
    reset_buffer();frozen=true;
    length=serialize_still_request_json(buffer.data(),512,machine_uid,still_uid,cleaned_ack);
    if(length>=512){AUTH_TRY(assertion("Buffer.h",0x46,"len < SIZE"));length=511;}
    buffer[length]=0;remaining=511-length;truncated=remaining==0;return StillAuthorizationResult::Ok;
}
StillAuthorizationResult StillAuthorization::receive_headers(const HttpResponse& response,bool& accepted){
    reset_buffer();accepted=false;
    if(std::strcmp(response.content_type.data(),"application/json")&&std::strcmp(response.content_type.data(),"application/json; charset=utf-8"))
        return diagnose("PicoStill.h",0xb2,"Unexpected content type");
    if(response.content_length>512)return diagnose("PicoStill.h",0xb6,"Not enough space to receive response");
    accepted=true;return StillAuthorizationResult::Ok;
}
StillAuthorizationResult StillAuthorization::receive_body(const std::uint8_t* bytes,std::size_t count){
    if(frozen)AUTH_TRY(assertion("Buffer.h",0x5d,"!_isFrozen"));
    if(!bytes&&count)AUTH_TRY(assertion("Buffer.h",0x5e,"data != nullptr || datalen == 0"));
    if(frozen||!bytes||!count)return StillAuthorizationResult::Ok;
    if(length>512||remaining>512-length)return StillAuthorizationResult::InvalidSpan;
    const auto copied=std::min(count,remaining);
    if(copied<count){
        if(!truncated){truncated=true;AUTH_TRY(diagnose("Buffer.h",0x66,"Buffer truncated"));}
        else if(copied)AUTH_TRY(assertion("Buffer.h",0x68,"count == 0"));
        AUTH_TRY(overflow(count-copied));
    }
    std::memcpy(buffer.data()+length,bytes,copied);length+=copied;remaining-=copied;buffer[length]=0;return StillAuthorizationResult::Ok;
}
StillAuthorizationResult StillAuthorization::copy_record(const StillAuthorizationJson& parsed){
    if(parsed.uid){const auto count=std::strlen(parsed.uid);const auto copied=std::min(count,std::size_t(16));
        if(copied<count){record.uid_truncated=true;AUTH_TRY(diagnose("Buffer.h",0x66,"Buffer truncated"));AUTH_TRY(overflow(count-copied));}
        std::memcpy(record.uid.data(),parsed.uid,copied);}
    record.profile_id=parsed.profile_id;
    if(parsed.cleaned_ack_date){const auto count=std::strlen(parsed.cleaned_ack_date);const auto copied=std::min(count,std::size_t(32));
        if(copied<count){record.cleaned_truncated=true;AUTH_TRY(diagnose("Buffer.h",0x66,"Buffer truncated"));AUTH_TRY(overflow(count-copied));}
        std::memcpy(record.cleaned_ack_date.data(),parsed.cleaned_ack_date,copied);}
    return StillAuthorizationResult::Ok;
}
StillAuthorizationResult StillAuthorization::parse(){
    record=StillAuthorizationRecord{};StillAuthorizationJson parsed;
    json_error=parse_still_authorization_json(buffer.data(),buffer.size(),parsed);
    if(json_error!=RecipeJsonError::Ok){
        static const char* names[]={"Ok","IncompleteInput","InvalidInput","NoMemory","NotSupported","TooDeep"};char message[112];
        ::snprintf(message,sizeof(message),"deserializeJson error %u %s",static_cast<unsigned>(json_error),names[static_cast<unsigned>(json_error)]);
        return diagnose("PicoStill.h",0x61,message);
    }
    return copy_record(parsed);
}
StillAuthorizationResult StillAuthorization::tick(){
    if(callback_result!=StillAuthorizationResult::Ok)return callback_result;
    if(pending&&client.state==HttpState::Idle){AUTH_TRY(parse());pending=false;}
    return StillAuthorizationResult::Ok;
}
#undef AUTH_TRY
}} // namespace zseries::oem
