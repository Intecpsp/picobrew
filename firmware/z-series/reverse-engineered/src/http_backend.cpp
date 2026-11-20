#include "zseries/http_backend.hpp"
#include <cstdio>
namespace zseries { namespace oem {
namespace {
const char* state_name(std::uint32_t value){
    // Original state-name table at 0x3f41ff04.
    static const char* const names[]={"Idle","GetBackendStatePending","GetBackendStateDownloading",
        "GetMyRecipesPending","GetMyRecipesDownloading","GetZymaticRecipePending","GetZymaticRecipeDownloading",
        "GetZymaticRecipeRefPending","GetZymaticRecipeRefDownloading","GetBrewPenRecipePending","GetBrewPenRecipeDownloading",
        "GetPaksShippedListPending","GetPaksShippedListDownloading","GetPicoPakRecipePending","GetPicoPakRecipeDownloading",
        "GetPicoPakRecipeRefPending","GetPicoPakRecipeRefDownloading","GetCurrentZKitsListPending","GetCurrentZKitsListDownloading",
        "GetZKitSmartListPending","GetZKitSmartListDownloading","GetResumableSessionPending","GetResumableSessionDownloading"};
    return value<sizeof names/sizeof names[0]?names[value]:"<State::INVALID>";
}
BackendResult result_of(HttpResult r){switch(r){case HttpResult::Ok:return BackendResult::Ok;
    case HttpResult::MissingService:return BackendResult::MissingService;
    case HttpResult::InvalidSpan:return BackendResult::InvalidSpan;default:return BackendResult::ServiceFailure;}}
bool headers(void* context,const HttpResponse& value){auto& self=*static_cast<HttpBackend*>(context);bool accepted=false;
    self.callback_result=self.receive_headers(value,accepted);return accepted&&self.callback_result==BackendResult::Ok;}
void body(void* context,const std::uint8_t* bytes,std::size_t size){auto& self=*static_cast<HttpBackend*>(context);self.callback_result=self.receive_body(bytes,size);}
void complete(void*){} // Original 0x401bf22c / adjusted 0x401bf234 return without effects; tick parses it.
}
void BackendState::clear() noexcept {ready=false;error=false;update_address.clear();program_uri.clear();registration_token.clear();
    token_expired=false;registered=false;byte_146=false;error_text.clear();backend_error=ZBackendError::None;
    alias.clear();dirty_sessions=-1;last_session=SessionType::Unknown;resumable_session_id=-1;}
BackendResult HttpBackend::assertion(std::uint32_t line){if(!hooks.assertion)return BackendResult::MissingService;
    return hooks.assertion(hooks.context,line)?BackendResult::Ok:BackendResult::ServiceFailure;}
BackendResult HttpBackend::diagnostic(std::uint32_t line){if(!hooks.diagnostic)return BackendResult::MissingService;
    return hooks.diagnostic(hooks.context,line)?BackendResult::Ok:BackendResult::ServiceFailure;}
BackendResult HttpBackend::transition(std::uint32_t next){if(state==next)return diagnostic(0xaf);
    if(diagnostics.log||diagnostics.assertion){
        if(!diagnostics.log)return BackendResult::MissingService;
        char message[128];::snprintf(message,sizeof message,"TRANSITION = %s -> %s",state_name(state),state_name(next));
        if(!diagnostics.log(diagnostics.context,ConsoleLevel::Info,"HBE",nullptr,0,message))return BackendResult::ServiceFailure;
    }
    if(next<22&&(0x2aaaa8u&(1u<<next))){event.reset(CoreEventType::Loading);mailbox.put(event);}
    state=next;return BackendResult::Ok;}
BackendResult HttpBackend::can_connect(bool& ready){ready=false;if(!hooks.network_ready)return BackendResult::MissingService;
    if(!hooks.network_ready(hooks.context,ready))return BackendResult::ServiceFailure;
    if(!ready){event.reset(CoreEventType::NetworkMissingError);mailbox.put(event);}return BackendResult::Ok;}
BackendResult HttpBackend::registration(){if(state!=0)return diagnostic(0x3cb);bool ready=false;auto result=can_connect(ready);
    if(result!=BackendResult::Ok)return result;
    if(!ready)return diagnostic(0x3cd);
    return transition(1);}
BackendResult HttpBackend::request(const BackendCommand& value){
    if(state!=0)return diagnostic(0x397);
    bool ready=false;auto result=can_connect(ready);
    if(result!=BackendResult::Ok)return result;
    if(!ready)return diagnostic(0x399);
    offset=value.offset;search=value.search&&*value.search?value.search:nullptr;
    auto source=value.id;const auto id=static_cast<std::uint32_t>(source);std::uint32_t next=0;
    switch(id){case 4:next=17;break;case 5:next=19;break;case 3:case 11:next=id;break;
    case 6:case 8:case 9:next=id==6?5:id==8?9:13;break;case 7:next=7;break;case 10:next=15;break;case 50:next=21;break;
    default:diagnostic_text=to_string(value.id);result=diagnostic(0x3bf);source=CoreCommandId::None;break;}
    if(result!=BackendResult::Ok)return result;
    if(next){result=transition(next);if(result!=BackendResult::Ok)return result;}
    if(id==6||id==8||id==9){pending_recipe=value.recipe;
        const auto expected=id==6?0u:id==8?2u:1u;
        if(static_cast<std::uint32_t>(pending_recipe.kind)!=expected){result=assertion(id==6?0x3ac:id==8?0x3b1:0x3b6);if(result!=BackendResult::Ok)return result;}
        if(id==8){if(pending_recipe.id!=0){result=assertion(0x3b2);if(result!=BackendResult::Ok)return result;}pending_recipe.id=0;}
        if(id==9)parameters=value.parameters;
    }else if(id==7||id==10)reference_id=value.reference_id;
    else if(id==50){parameters=ProgramParameters{};parameters.resumed_session=true;}
    event.source=source;return BackendResult::Ok;
}
BackendResult HttpBackend::set_error(std::uint32_t failing,const char* text){
    if(failing==1||failing==2){backend.error_text.set(text);backend.error=true;backend.ready=true;}
    else{event.reset(CoreEventType::LoadingError);mailbox.put(event);}return BackendResult::Ok;}
BackendResult HttpBackend::endpoint(const char* type,bool controller,bool include_id,std::int32_t id){
    if(!type||!*type){auto result=assertion(controller?0x2e3:0x2ee);if(result!=BackendResult::Ok)return result;}
    uri.clear();uri.append(controller?"https://www.picobrew.com/Vendors/input.cshtml?ctl=":"https://www.picobrew.com/Vendors/input.cshtml?type=");
    uri.append(type);uri.append("&token=");uri.append(token);
    if(uri.truncated){auto result=assertion(controller?0x2e9:0x2f4);if(result!=BackendResult::Ok)return result;}
    if(include_id){char number[16];::snprintf(number,sizeof(number),"%d",static_cast<int>(id));uri.append("&id=");uri.append(number);
        if(uri.truncated)return assertion(0x2fe);}
    return BackendResult::Ok;
}
HttpReceiver HttpBackend::receiver() noexcept {HttpReceiver result;result.context=this;result.headers=headers;result.body=body;result.complete=complete;return result;}
BackendResult HttpBackend::start_request(std::uint32_t running){
    BackendResult result=BackendResult::Ok;
    switch(running){case 2:backend.clear();result=endpoint("ZState");break;
    case 4:case 12:case 18:case 20:result=endpoint("RecipeRefListController",true);break;
    case 8:case 16:result=endpoint("RecipeRef");break;
    case 6:result=endpoint("Recipe",false,true,static_cast<std::int32_t>(pending_recipe.id));break;
    case 14:result=endpoint("ZPicoRecipe",false,true,static_cast<std::int32_t>(pending_recipe.id));break;
    case 22:result=endpoint("ResumableSession",false,true,backend.resumable_session_id);break;
    case 10:
        if(!local_recipe.uri.text[0]){result=assertion(0x303);if(result!=BackendResult::Ok)return result;}
        uri.set(local_recipe.uri.text.data());if(uri.truncated)result=assertion(0x306);break;
    default:return diagnostic(0x31a);}
    if(result!=BackendResult::Ok)return result;
    bool accepted=false;callback_result=BackendResult::Ok;
    result=result_of(client.configure(uri.text.data(),receiver(),HttpClientClass::None,accepted));if(result!=BackendResult::Ok)return result;
    if(!accepted){result=diagnostic(0x32a);if(result!=BackendResult::Ok)return result;set_error(running,"HTTP init");return transition(0);}
    if(!authorization)return BackendResult::MissingService;
    client.set_authorization(authorization);
    const auto length=serialize_backend_request(payload.data(),payload.size(),running,firmware_version,boiler,offset,search,reference_id);
    if(running==2)result=result_of(client.put("application/json",payload.data(),length,accepted));
    else if(running==6||running==10||running==14||running==22)result=result_of(client.get(accepted));
    else result=result_of(client.post("application/json",payload.data(),length,accepted));
    if(result!=BackendResult::Ok)return result;
    if(!accepted){diagnostic_text=running==2?"PUT":running==6||running==10||running==14||running==22?"GET":"POST";result=diagnostic(0x350);if(result!=BackendResult::Ok)return result;set_error(running,"HTTP request");return transition(0);}
    return transition(running);
}
BackendResult HttpBackend::tick(std::uint32_t now){auto result=result_of(client.tick(now));if(result!=BackendResult::Ok)return result;
    if(callback_result!=BackendResult::Ok)return callback_result;
    if(state<22&&(state&1))return start_request(state+1);
    if(state==0||client.state!=HttpState::Idle)return BackendResult::Ok;
    return finish_response();
}
BackendResult HttpBackend::finish_response(){
    if(client.error!=HttpClientErrorId::None){char error[32];::snprintf(error,sizeof(error),"HTTP %d",static_cast<int>(client.response.status));set_error(state,error);}
    else{bool accepted=false;const auto result=parse_response(accepted);if(result!=BackendResult::Ok)return result;
        if(state==2){if(!accepted)backend.error=true;backend.ready=true;}
        else if(state==4||state==6||state==8||state==10||state==12||state==14||state==16||state==18||state==20||state==22){
            if(accepted)mailbox.put(event);else set_error(state,"Parsing");}}
    return transition(0);
}
BackendResult HttpBackend::receive_headers(const HttpResponse& value,bool& accepted){
    response.fill(0);response_length=0;response_frozen=false;response_truncated=false;accepted=false;
    if(std::strcmp(value.content_type.data(),"application/json")&&std::strcmp(value.content_type.data(),"application/json; charset=utf-8"))return diagnostic(0x425);
    if(value.content_length>4096)return diagnostic(0x429);
    accepted=true;return BackendResult::Ok;
}
BackendResult HttpBackend::receive_body(const std::uint8_t* bytes,std::size_t size){
    if(response_frozen){const auto result=assertion(0x5d);if(result!=BackendResult::Ok)return result;}
    if(!bytes&&size){const auto result=assertion(0x5e);if(result!=BackendResult::Ok)return result;}
    if(response_frozen||!bytes||!size)return BackendResult::Ok;
    const auto count=std::min(size,response.size()-response_length-1);
    if(count!=size){if(!response_truncated){response_truncated=true;const auto r=diagnostic(0x66);if(r!=BackendResult::Ok)return r;}
        else if(count){const auto r=assertion(0x68);if(r!=BackendResult::Ok)return r;}
        diagnostic_argument=static_cast<std::uint32_t>(size-count);const auto r=diagnostic(0x6a);if(r!=BackendResult::Ok)return r;}
    std::memcpy(response.data()+response_length,bytes,count);response_length+=count;response[response_length]=0;return BackendResult::Ok;
}
}} // namespace zseries::oem
