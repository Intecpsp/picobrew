#include "zseries/head_text_input.hpp"
using namespace zseries::oem;
namespace {
struct Context {std::uint32_t* out;unsigned count;};
bool perform(void* p,HeadTextEffect type,const HeadTextCommand* command){
    auto& c=*static_cast<Context*>(p);auto* out=c.out+c.count;c.count+=120;
    for(unsigned i=0;i<120;i++)out[i]=0;
    out[0]=static_cast<unsigned>(type);if(!command)return true;
    out[1]=static_cast<unsigned>(command->id);out[2]=command->recipe_reference;out[3]=command->kit_code!=nullptr;
    unsigned i=4;
    if(command->credentials){for(char x:command->credentials->ssid)out[i++]=static_cast<unsigned char>(x);for(char x:command->credentials->password)out[i++]=static_cast<unsigned char>(x);out[i++]=command->credentials->byte_80;}
    i=103;if(command->kit_code)for(unsigned j=0;j<17;j++)out[i++]=static_cast<unsigned char>(command->kit_code[j]);
    return true;
}
}
extern "C" unsigned head_text_test(std::uint32_t command,const std::uint32_t* input,const std::uint32_t* text,std::uint32_t* out,std::uint32_t* effects){
    HeadTextInput head;auto& s=head.state;unsigned i=0;
    s.awaiting_wifi_password=input[i++];for(auto& x:s.credentials.ssid)x=static_cast<char>(input[i++]);for(auto& x:s.credentials.password)x=static_cast<char>(input[i++]);s.credentials.byte_80=input[i++];
    for(auto& x:s.kit_code)x=static_cast<char>(input[i++]);
    s.kit_length=input[i++];s.kit_truncated=input[i++];
    std::array<char,65> value{};for(i=0;i<65;i++)value[i]=static_cast<char>(text[i]);
    Context context{effects,0};head.services.context=&context;head.services.perform=perform;
    const auto result=head.input(static_cast<UiCommand>(command),value);i=0;
    out[i++]=s.awaiting_wifi_password;for(auto x:s.credentials.ssid)out[i++]=static_cast<unsigned char>(x);for(auto x:s.credentials.password)out[i++]=static_cast<unsigned char>(x);out[i++]=s.credentials.byte_80;
    for(auto x:s.kit_code)out[i++]=static_cast<unsigned char>(x);
    out[i++]=s.kit_length;out[i++]=s.kit_truncated;
    out[i]=context.count;return static_cast<unsigned>(result);
}
