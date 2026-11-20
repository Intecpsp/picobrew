#include "zseries/azure.hpp"
#include <cstdarg>
#include <cstdio>
namespace zseries { namespace oem {
namespace {
bool message(void* context,const char* topic,char* payload,std::size_t length){auto& self=*static_cast<AzureService*>(context);
    self.callback_result=self.receive(topic,payload,length);return self.callback_result==BackendResult::Ok;}
BackendResult trace(const AzureServices& services,const char* tag,const char* file,std::uint32_t line,const char* pattern,...){
    const auto& d=services.diagnostics;
    if(!d.context&&!d.assertion&&!d.log)return BackendResult::Ok;
    if(!d.log)return BackendResult::MissingService;
    char message[160];va_list arguments;va_start(arguments,pattern);
    const int size=::vsnprintf(message,sizeof message,pattern,arguments);va_end(arguments);
    if(size<0||static_cast<std::size_t>(size)>=sizeof message)return BackendResult::InvalidSpan;
    return d.log(d.context,ConsoleLevel::Info,tag,file,line,message)?BackendResult::Ok:BackendResult::ServiceFailure;
}
BackendResult backoff_advance(AzureService& service,std::uint32_t now){
    auto& timer=service.backoff;++timer.attempts;
    auto result=trace(service.services,"BTMR","Timer.h",0x53,"%s - Try #%d, Timeout in %d min","Azure.h",
        static_cast<std::int32_t>(timer.attempts),static_cast<std::int32_t>(timer.duration/60000));
    if(result!=BackendResult::Ok)return result;
    timer.deadline=now+timer.duration;timer.duration*=2;
    if(timer.duration>timer.maximum_duration)timer.duration=timer.maximum_duration;
    return BackendResult::Ok;
}
BackendResult backoff_start(AzureService& service,std::uint32_t now){
    auto result=trace(service.services,"BTMR","Timer.h",0x6b,"%s - Starting BackoffTimer","Azure.h");
    if(result!=BackendResult::Ok)return result;
    service.backoff.attempts=0;service.backoff.duration=service.backoff.initial_duration;service.backoff.active=true;
    return backoff_advance(service,now);
}
}
BackendResult AzureService::perform(const AzureAction& action,std::uint32_t* result){
    if(!services.perform)return BackendResult::MissingService;
    std::uint32_t returned=0;
    if(!services.perform(services.context,action,returned))return BackendResult::ServiceFailure;
    if(result)*result=returned;
    return BackendResult::Ok;
}
BackendResult AzureService::effect(AzureEffect value,std::uint32_t argument,std::uint32_t* result){AzureAction action(value);action.argument=argument;return perform(action,result);}
BackendResult AzureService::diagnostic(std::uint32_t line){return effect(AzureEffect::Diagnostic,line);}
BackendResult AzureService::assertion(std::uint32_t line){if(!services.assertion)return BackendResult::MissingService;
    return services.assertion(services.context,line)?BackendResult::Ok:BackendResult::ServiceFailure;}
BackendResult AzureService::transition(AzureState next){
    if(state==next)return BackendResult::Ok;
    if(!services.millis)return BackendResult::MissingService;
    const auto now=services.millis(services.context);
    auto result=trace(services,"AZ","Azure.h",0x1f5,"TRANSITION = %s -> %s",to_string(state),to_string(next));
    if(result!=BackendResult::Ok)return result;
    state=next;changed_ms=now;deadline=0;
    switch(next){case AzureState::Disabled:case AzureState::Listening:case AzureState::SessionRunning:return BackendResult::Ok;
    case AzureState::Connecting:result=backoff_start(*this,now);return result==BackendResult::Ok?attempt_connection():result;
    case AzureState::SessionLoaded:deadline=now+30000;return BackendResult::Ok;
    default:return diagnostic(0x209);}
}
BackendResult AzureService::connect(){enabled=true;return transition(AzureState::Connecting);}
BackendResult AzureService::recover(bool disabled){if(disabled){enabled=false;return BackendResult::Ok;}return connect();}
BackendResult AzureService::session_begin(){return enabled?transition(AzureState::SessionRunning):BackendResult::Ok;}
BackendResult AzureService::session_end(){return enabled?transition(AzureState::Listening):BackendResult::Ok;}
BackendResult AzureService::session_discard(){return enabled&&state==AzureState::SessionLoaded?transition(AzureState::Listening):BackendResult::Ok;}
BackendResult AzureService::attempt_connection(){
    if(!enabled){auto r=assertion(0x15c);if(r!=BackendResult::Ok)return r;}
    std::uint32_t present=0;auto result=effect(AzureEffect::AcquireClient,1,&present);if(result!=BackendResult::Ok)return result;
    if(!present)return diagnostic(0x161);
    result=diagnostic(0x165);if(result!=BackendResult::Ok)return result;
    result=effect(AzureEffect::BindClient);if(result!=BackendResult::Ok)return result;
    AzureAction server(AzureEffect::SetServer);server.text=host;server.argument=8883;result=perform(server);if(result!=BackendResult::Ok)return result;
    AzureAction callback(AzureEffect::SetCallback);callback.callback_context=this;callback.message=message;
    result=perform(callback);if(result!=BackendResult::Ok)return result;return mqtt_connect();
}
BackendResult AzureService::mqtt_connect(){
    std::uint32_t connected=0;auto result=effect(AzureEffect::Connected,0,&connected);if(result!=BackendResult::Ok||connected)return result;
    result=diagnostic(0xdc);if(result!=BackendResult::Ok)return result;
    AzureAction action(AzureEffect::Connect);action.text=device_id;action.second=username.text.data();action.third=password.text.data();
    result=perform(action,&connected);if(result!=BackendResult::Ok)return result;
    if(!connected)return diagnostic(0xe2);
    AzureAction subscribe(AzureEffect::Subscribe);subscribe.text=subscribe_topic.text.data();result=perform(subscribe);if(result!=BackendResult::Ok)return result;
    result=diagnostic(0xdf);if(result!=BackendResult::Ok)return result;
    result=trace(services,"BTMR","Timer.h",0x74,"%s - Stopping BackoffTimer","Azure.h");
    if(result!=BackendResult::Ok)return result;
    backoff.stop();return BackendResult::Ok;
}
BackendResult AzureService::tick(std::uint32_t now){
    std::uint32_t connected=0;auto result=effect(AzureEffect::Connected,0,&connected);if(result!=BackendResult::Ok)return result;
    if(!enabled){if(connected){result=effect(AzureEffect::Disconnect);if(result!=BackendResult::Ok)return result;}
        if(state!=AzureState::Disabled){result=transition(AzureState::Disabled);if(result!=BackendResult::Ok)return result;}}
    else if(connected){if(state==AzureState::Connecting){result=transition(AzureState::Listening);if(result!=BackendResult::Ok)return result;}
        result=effect(AzureEffect::Loop);if(result!=BackendResult::Ok)return result;}
    else if(state==AzureState::Listening){result=transition(AzureState::Connecting);if(result!=BackendResult::Ok)return result;}
    else if(backoff.expired(now)){result=backoff_advance(*this,now);if(result!=BackendResult::Ok)return result;result=attempt_connection();if(result!=BackendResult::Ok)return result;}
    if(callback_result!=BackendResult::Ok)return callback_result;
    // OEM repeats this command on every expired tick until Core changes state.
    if(deadline<=now&&state==AzureState::SessionLoaded){AzureCommand command;command.id=CoreCommandId::AzureWindowTimeout;commands.put(command);return diagnostic(0x289);}
    return BackendResult::Ok;
}
BackendResult AzureService::publish_state(){AzureAction action(AzureEffect::Publish);action.text=publish_topic.text.data();action.second="state";action.argument=0;return perform(action);}
BackendResult AzureService::generate_signature(const char* device,const char* expiry,BackendText<128>& output,int& crypto_result){
    crypto_result=0;
    if(!device||!*device){auto r=assertion(0x171);if(r!=BackendResult::Ok)return r;}
    if(!expiry||!*expiry){auto r=assertion(0x172);if(r!=BackendResult::Ok)return r;}
    BackendText<128> input;input.append(host);input.append("%2Fdevices%2F");input.append(device);input.append("\n");input.append(expiry);
    if(input.truncated){auto r=assertion(0x17a);if(r!=BackendResult::Ok)return r;}
    if(!services.hmac||!key)return BackendResult::MissingService;
    std::uint8_t hash[32];crypto_result=services.hmac(services.context,key,key_size,reinterpret_cast<const std::uint8_t*>(input.text.data()),input.length,hash);
    if(crypto_result!=0)return diagnostic(0x191);
    static const char base64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char encoded[45];std::size_t count=0;
    for(std::size_t i=0;i<32;i+=3){const auto value=(static_cast<std::uint32_t>(hash[i])<<16)|(i+1<32?static_cast<std::uint32_t>(hash[i+1])<<8:0)|(i+2<32?hash[i+2]:0);
        encoded[count++]=base64[(value>>18)&63];encoded[count++]=base64[(value>>12)&63];encoded[count++]=i+1<32?base64[(value>>6)&63]:'=';encoded[count++]=i+2<32?base64[value&63]:'=';}
    encoded[count]=0;BackendText<128> escaped;
    for(std::size_t i=0;i<count;++i){char one[2]={encoded[i],0};if(encoded[i]=='+')escaped.append("%2B");else if(encoded[i]=='/')escaped.append("%2F");else if(encoded[i]=='=')escaped.append("%3D");else escaped.append(one);}
    output=escaped;return BackendResult::Ok;
}
BackendResult AzureService::create_password(const char* expiry){
    password.clear();BackendText<128> escaped;int crypto_result=0;
    auto result=generate_signature(device_id,expiry,escaped,crypto_result);if(result!=BackendResult::Ok)return result;
    if(crypto_result!=0){result=diagnostic(0x1b2);return result==BackendResult::Ok?BackendResult::ServiceFailure:result;}
    password.append("SharedAccessSignature sr=");password.append(host);password.append("%2Fdevices%2F");password.append(device_id);
    password.append("&sig=");password.append(escaped.text.data());password.append("&se=");password.append(expiry);return BackendResult::Ok;
}
BackendResult AzureService::setup(){
    username.clear();username.append(host);username.append("/");username.append(device_id);username.append("/api-version=2016-11-14");
    subscribe_topic.clear();subscribe_topic.append("devices/");subscribe_topic.append(device_id);subscribe_topic.append("/messages/devicebound/#");
    publish_topic.clear();publish_topic.append("devices/");publish_topic.append(device_id);publish_topic.append("/messages/events/");
    if(key_size==0){auto r=assertion(0x237);if(r!=BackendResult::Ok)return r;}
    return create_password("2524636800");
}
}} // namespace zseries::oem
