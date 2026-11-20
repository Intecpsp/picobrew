#include "zseries/head_network_events.hpp"
#include <cstring>
#include <vector>
using namespace zseries::oem;
using Word=std::uint32_t;
namespace {
Quantity read_quantity(const Word*& w) {
    Quantity q;const std::uint64_t bits=std::uint64_t(w[0])|(std::uint64_t(w[1])<<32);
    std::memcpy(&q.value,&bits,8);q.unit=static_cast<Unit>(w[2]);w+=3;return q;
}
void write_quantity(Word*& w,Quantity q) {
    std::uint64_t bits;std::memcpy(&bits,&q.value,8);w[0]=bits;w[1]=bits>>32;w[2]=static_cast<Word>(q.unit);w+=3;
}
void read_parameters(const Word*& w,ProgramParameters& p){
    p.id=static_cast<ZProgramId>(*w++);std::memcpy(&p.abv,w++,4);std::memcpy(&p.ibu,w++,4);
    p.time=read_quantity(w);p.water=read_quantity(w);p.intensity=read_quantity(w);p.temperature=read_quantity(w);p.resumed_session=*w++;
}
void write_parameters(Word*& w,const ProgramParameters& p){
    *w++=static_cast<Word>(p.id);*w++=static_cast<Word>(p.abv);*w++=static_cast<Word>(p.ibu);
    write_quantity(w,p.time);write_quantity(w,p.water);write_quantity(w,p.intensity);write_quantity(w,p.temperature);*w++=p.resumed_session;
}

void string(std::vector<Word>& out,const char* text){
    const std::size_t size=text?std::strlen(text):0;out.push_back(size);
    for(std::size_t i=0;i<size;++i)out.push_back(static_cast<unsigned char>(text[i]));
}
bool perform(void* p,const HeadNetworkAction& action){
    auto& out=*static_cast<std::vector<Word>*>(p);
    out.insert(out.end(),{static_cast<Word>(action.effect),action.first,action.second,action.third});
    string(out,action.title);string(out,action.text);return true;
}
}
extern "C" unsigned head_network_test(const Word* args,const Word* state,const Word* payload,Word* result,Word* effects){
    ProgramParameters parameters;read_parameters(state,parameters);HeadNetworkEvents head(parameters);
    head.core=static_cast<CoreStateId>(args[1]);head.language=static_cast<LanguageType>(args[2]);
    std::vector<Word> actions;head.services.context=&actions;head.services.perform=perform;
    HeadNetworkResult status;
    if(args[0]==0){CoreWifiEvent event;event.type=static_cast<WifiEventType>(args[3]);event.argument=args[4];event.second_argument=args[5];status=head.wifi(event);}
    else{HeadAzureEvent event;event.type=static_cast<AzureEventType>(args[3]);event.program=static_cast<ZProgramId>(args[4]);read_parameters(payload,event.parameters);status=head.azure(event);}
    write_parameters(result,parameters);*result++=head.assertion_line;*result++=actions.size();
    for(std::size_t i=0;i<actions.size();++i)effects[i]=actions[i];
    return static_cast<unsigned>(status);
}
