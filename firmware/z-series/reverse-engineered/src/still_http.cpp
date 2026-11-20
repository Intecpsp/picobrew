#include "zseries/still_http.hpp"
namespace zseries { namespace oem {
namespace {
void observe(StillHttpBinding& binding){
    binding.still.http.state=static_cast<std::uint32_t>(binding.http.state);binding.still.http.error=static_cast<std::uint32_t>(binding.http.error);binding.still.http.socket_error=binding.http.socket_error;
}
bool headers(void* context,const HttpResponse& response){
    auto& binding=*static_cast<StillHttpBinding*>(context);StillHttpResponse value;value.status=response.status;value.content_length=response.content_length;value.content_type=response.content_type;
    return binding.still.receive_headers(value);
}
void body(void* context,const std::uint8_t* data,std::size_t count){static_cast<StillHttpBinding*>(context)->still.receive_body(data,count);}
void complete(void* context){
    auto& binding=*static_cast<StillHttpBinding*>(context);
    if(!binding.http.services.millis){binding.last_result=HttpResult::MissingService;return;}
    binding.still.complete(binding.http.services.millis(binding.http.services.context));
}
bool perform(void* context,const StillHttpAction& action,std::uint32_t& result){
    auto& binding=*static_cast<StillHttpBinding*>(context);bool accepted=true;binding.last_result=HttpResult::Ok;
    switch(action.effect){
    case StillHttpEffect::Reset:binding.http.reset();break;
    case StillHttpEffect::Cancel:binding.last_result=binding.http.cancel();break;
    case StillHttpEffect::Configure:{HttpReceiver receiver;receiver.context=&binding;receiver.headers=headers;receiver.body=body;receiver.complete=complete;
        binding.last_result=binding.http.configure(action.uri,receiver,static_cast<HttpClientClass>(action.client_class),accepted);break;}
    case StillHttpEffect::Get:binding.last_result=binding.http.get(accepted);break;
    case StillHttpEffect::Post:binding.last_result=binding.http.post(action.content_type,action.body,action.body_length,accepted);break;
    }
    observe(binding);
    result=accepted;return binding.last_result==HttpResult::Ok;
}
}
StillHttpServices still_http_services(StillHttpBinding& binding) noexcept {StillHttpServices services;services.context=&binding;services.perform=perform;return services;}
HttpResult tick_still_http(StillHttpBinding& binding,std::uint32_t now){binding.last_result=binding.http.tick(now);observe(binding);return binding.last_result;}
}} // namespace zseries::oem
