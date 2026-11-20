#include "zseries/z_backend.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace zseries { namespace oem {
namespace {
BackendResult converted(HttpResult r) {
    switch(r){case HttpResult::Ok:return BackendResult::Ok;
    case HttpResult::MissingService:return BackendResult::MissingService;
    case HttpResult::InvalidSpan:return BackendResult::InvalidSpan;
    default:return BackendResult::ServiceFailure;}
}
bool headers(void* p,const HttpResponse& response){auto& self=*static_cast<ZBackend*>(p);bool accepted=false;
    self.callback_result=self.receive_headers(response,accepted);return accepted&&self.callback_result==BackendResult::Ok;}
void body(void* p,const std::uint8_t* data,std::size_t size){auto& self=*static_cast<ZBackend*>(p);
    self.callback_result=self.receive_body(data,size);}
void complete(void* p){auto& self=*static_cast<ZBackend*>(p);self.callback_result=self.receive_complete();}
}
BackendResult ZBackend::diagnose(std::uint32_t line){if(!hooks.diagnostic)return BackendResult::MissingService;
    return hooks.diagnostic(hooks.context,line)?BackendResult::Ok:BackendResult::ServiceFailure;}
BackendResult ZBackend::check(std::uint32_t line){if(!hooks.assertion)return BackendResult::MissingService;
    return hooks.assertion(hooks.context,line)?BackendResult::Ok:BackendResult::ServiceFailure;}
BackendResult ZBackend::can_sync(bool& ready){ready=false;if(!hooks.network_ready)return BackendResult::MissingService;
    bool network=false;if(!hooks.network_ready(hooks.context,network))return BackendResult::ServiceFailure;
    ready=network&&state==0;return BackendResult::Ok;}
void ZBackend::clear_request() noexcept {uri.fill(0);uri_truncated=false;response.fill(0);response_length=0;
    response_truncated=false;response_frozen=false;created_session_id=0;payload.fill(0);}
void ZBackend::append_uri(const char* text) noexcept {if(!text)return;const auto used=std::strlen(uri.data());
    const auto count=std::strlen(text);const auto copied=std::min(count,uri.size()-used-1);
    std::memcpy(uri.data()+used,text,copied);uri[used+copied]=0;uri_truncated=uri_truncated||copied!=count;}
BackendResult ZBackend::endpoint(const char* type,const char* request_token,std::int32_t id){
    if(!type)return BackendResult::InvalidSpan;
    if(!*type){auto r=check(0x7e);if(r!=BackendResult::Ok)return r;}
    clear_request();append_uri("https://www.picobrew.com/Vendors/input.cshtml?type=");append_uri(type);
    if(request_token&&*request_token){append_uri("&token=");append_uri(request_token);}
    if(id>0){char number[16];::snprintf(number,sizeof(number),"%d",static_cast<int>(id));append_uri("&id=");append_uri(number);}
    return BackendResult::Ok;
}
HttpReceiver ZBackend::receiver() noexcept {HttpReceiver value;value.context=this;value.headers=headers;value.body=body;value.complete=complete;return value;}
BackendResult ZBackend::configure(){if(!authorization)return BackendResult::MissingService;
    completed=false;callback_result=BackendResult::Ok;bool accepted=false;
    auto result=converted(client.configure(uri.data(),receiver(),HttpClientClass::None,accepted));
    if(result!=BackendResult::Ok)return result;
    client.set_authorization(authorization);return BackendResult::Ok;
}
BackendResult ZBackend::session(const SessionSnapshot& value,bool ending){
    bool ready=false;auto result=can_sync(ready);if(result!=BackendResult::Ok)return result;
    if(!ready){result=check(ending?0x108:0xfc);if(result!=BackendResult::Ok)return result;}
    if(!ending&&value.type==SessionType::Unknown){result=check(0xfd);if(result!=BackendResult::Ok)return result;}
    state=ending?3:1;result=endpoint("ZSession",token,ending?static_cast<std::int32_t>(value.session_id):-1);
    if(result!=BackendResult::Ok)return result;
    if(!hooks.assertion)return BackendResult::MissingService;
    const auto encoded=serialize_session_json(payload.data(),payload.size(),value,hooks);
    if(encoded.result!=BackendResult::Ok)return encoded.result;
    result=configure();if(result!=BackendResult::Ok)return result;bool accepted=false;
    return converted(ending?client.put("application/json",payload.data(),std::strlen(payload.data()),accepted):
        client.post("application/json",payload.data(),std::strlen(payload.data()),accepted));
}
BackendResult ZBackend::create(const SessionSnapshot& value){return session(value,false);}
BackendResult ZBackend::end(const SessionSnapshot& value){return session(value,true);}
BackendResult ZBackend::log(const SessionLogSnapshot& value){
    bool ready=false;auto result=can_sync(ready);if(result!=BackendResult::Ok)return result;
    if(!ready){result=check(0x113);if(result!=BackendResult::Ok)return result;}
    state=2;result=endpoint("ZSessionLog",token,-1);if(result!=BackendResult::Ok)return result;
    if(!value.step_name||!*value.step_name){result=diagnose(0x11c);if(result!=BackendResult::Ok)return result;}
    if(value.pause_reason>17){result=check(0x17);if(result!=BackendResult::Ok)return result;}
    serialize_session_log_json(payload.data(),payload.size(),value,client.timing,hooks);
    result=configure();if(result!=BackendResult::Ok)return result;bool accepted=false;
    return converted(client.post("application/json",payload.data(),std::strlen(payload.data()),accepted));
}
BackendResult ZBackend::tick(std::uint32_t now){auto result=converted(client.tick(now));if(result!=BackendResult::Ok)return result;
    if(callback_result!=BackendResult::Ok)return callback_result;
    if(state==0||client.state!=HttpState::Idle)return BackendResult::Ok;
    if(client.error!=HttpClientErrorId::None){result=diagnose(0xf4);if(result!=BackendResult::Ok)return result;}
    state=0;return BackendResult::Ok;
}
BackendResult ZBackend::cancel(){return converted(client.cancel());}
BackendResult ZBackend::receive_headers(const HttpResponse& value,bool& accepted){
    clear_request();accepted=false;
    if(std::strcmp(value.content_type.data(),"application/json")&&std::strcmp(value.content_type.data(),"application/json; charset=utf-8"))return diagnose(0x161);
    if(value.content_length>1024)return diagnose(0x164);
    accepted=true;return BackendResult::Ok;
}
BackendResult ZBackend::receive_body(const std::uint8_t* bytes,std::size_t length){
    if(response_frozen){auto r=check(0x5d);if(r!=BackendResult::Ok)return r;}
    if(length&&!bytes){auto r=check(0x5e);if(r!=BackendResult::Ok)return r;}
    if(response_frozen||!bytes||!length)return BackendResult::Ok;
    const auto count=std::min(length,response.size()-response_length-1);
    if(count!=length){
        if(!response_truncated){response_truncated=true;auto r=diagnose(0x66);if(r!=BackendResult::Ok)return r;}
        else if(count){auto r=check(0x68);if(r!=BackendResult::Ok)return r;}
        diagnostic_argument=static_cast<std::uint32_t>(length-count);auto r=diagnose(0x6a);if(r!=BackendResult::Ok)return r;
    }
    if(count)std::memcpy(response.data()+response_length,bytes,count);
    response_length+=count;response[response_length]=0;
    return BackendResult::Ok;
}
BackendResult ZBackend::receive_complete(){completed=true;
    if(state==1){response_frozen=true;json_error=parse_created_session_json(response.data(),1024,created_session_id);
        if(json_error!=RecipeJsonError::Ok){const auto result=diagnose(0x178);if(result!=BackendResult::Ok)return result;}}
    state=0;return BackendResult::Ok;
}
}} // namespace zseries::oem
