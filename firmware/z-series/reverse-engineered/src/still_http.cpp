#include "zseries/still_http.hpp"
namespace zseries { namespace oem {
namespace {
HttpResult client_result(StillClientResult result){
    switch(result){
    case StillClientResult::Ok:return HttpResult::Ok;
    case StillClientResult::MissingService:return HttpResult::MissingService;
    case StillClientResult::ServiceFailure:return HttpResult::ServiceFailure;
    default:return HttpResult::InvalidSpan;
    }
}
void observe(StillHttpBinding& binding){
    binding.still.http.state=static_cast<std::uint32_t>(binding.http.state);binding.still.http.error=static_cast<std::uint32_t>(binding.http.error);binding.still.http.socket_error=binding.http.socket_error;
}
bool headers(void* context,const HttpResponse& response){
    auto& binding=*static_cast<StillHttpBinding*>(context);StillHttpResponse value;value.status=response.status;value.content_length=response.content_length;value.content_type=response.content_type;
    const bool accepted=binding.still.receive_headers(value);
    if(!accepted)binding.last_result=binding.still.diagnostics_result==ConsoleResult::MissingService?HttpResult::MissingService:HttpResult::ServiceFailure;
    return accepted;
}
void body(void* context,const std::uint8_t* data,std::size_t count){
    auto& binding=*static_cast<StillHttpBinding*>(context);
    if(binding.last_result==HttpResult::Ok)binding.last_result=client_result(binding.still.receive_body(data,count));
}
void complete(void* context){
    auto& binding=*static_cast<StillHttpBinding*>(context);
    if(binding.last_result!=HttpResult::Ok)return;
    if(!binding.http.services.millis){binding.last_result=HttpResult::MissingService;return;}
    binding.last_result=client_result(binding.still.complete(binding.http.services.millis(binding.http.services.context)));
}
bool perform(void* context,const StillHttpAction& action,std::uint32_t& result){
    auto& binding=*static_cast<StillHttpBinding*>(context);bool accepted=true;binding.last_result=HttpResult::Ok;
    HttpResult operation=HttpResult::Ok;
    switch(action.effect){
    case StillHttpEffect::Reset:operation=binding.http.reset();break;
    case StillHttpEffect::Cancel:operation=binding.http.cancel();break;
    case StillHttpEffect::Configure:{HttpReceiver receiver;receiver.context=&binding;receiver.headers=headers;receiver.body=body;receiver.complete=complete;
        operation=binding.http.configure(action.uri,receiver,static_cast<HttpClientClass>(action.client_class),accepted);break;}
    case StillHttpEffect::Get:operation=binding.http.get(accepted);break;
    case StillHttpEffect::Post:operation=binding.http.post(action.content_type,action.body,action.body_length,accepted);break;
    }
    if(binding.last_result==HttpResult::Ok)binding.last_result=operation;
    observe(binding);
    result=accepted;return binding.last_result==HttpResult::Ok;
}
}
StillHttpServices still_http_services(StillHttpBinding& binding) noexcept {StillHttpServices services;services.context=&binding;services.perform=perform;return services;}
HttpResult tick_still_http(StillHttpBinding& binding,std::uint32_t now){
    binding.last_result=HttpResult::Ok;const auto result=binding.http.tick(now);
    if(binding.last_result==HttpResult::Ok)binding.last_result=result;
    observe(binding);return binding.last_result;
}
}} // namespace zseries::oem
