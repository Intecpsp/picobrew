#include "zseries/head_numeric_inputs.hpp"
#include <cstring>
#include <vector>
using namespace zseries::oem;
using Word = std::uint32_t;
namespace {
struct Context{std::vector<Word> events;Word assertions=7;};
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
bool perform(void* context,HeadNumericEffect effect,const CoreCommand* command){
    Word temperature=0;if(command)std::memcpy(&temperature,&command->temperature,4);
    auto& out=static_cast<Context*>(context)->events;
    out.insert(out.end(),{static_cast<Word>(effect),command?static_cast<Word>(command->id):0,
        command?command->program_id:0,temperature,command?command->duration:0,command?command->location:0});return true;
}
}
extern "C" unsigned head_numeric_test(unsigned operation,const Word* initial,const Word* value,Word* result,Word* output){
    HeadNumericInputs head;auto& s=head.state;const Word* w=initial;
    s.manual.byte_00=*w++;s.manual.location=*w++;s.manual.temperature=read_quantity(w);s.manual.hold=read_quantity(w);s.manual.drain=read_quantity(w);
    for(auto& b:s.manual.bytes_38)b=*w++;
    read_parameters(w,s.parameters);read_parameters(w,s.coffee_defaults);s.abv_pending=*w++;s.ibu_pending=*w++;
    Context context;auto& events=context.events;head.services.context=&context;head.services.perform=perform;head.services.assertion=[](void* p,Word line){auto& c=*static_cast<Context*>(p);c.events.insert(c.events.end(),{5,line,0,0,0,0,6,200,0,0,0,0});++c.assertions;return true;};
    float temperature;std::memcpy(&temperature,value,4);std::int32_t strength;std::memcpy(&strength,value,4);
    HeadNumericResult status=HeadNumericResult::Ok;
    switch(operation){
    case 0:status=head.temperature(temperature);break;
    case 1:status=head.duration(value[0]);break;
    case 2:status=head.location(value[0]);break;
    case 3:{const Word* quantity=value+1;status=head.quantity(static_cast<HeadQuantityId>(value[0]),read_quantity(quantity));break;}
    case 4:status=head.strength(strength);break;
    default:return 99;
    }
    Word* out=result;*out++=s.manual.byte_00;*out++=s.manual.location;
    write_quantity(out,s.manual.temperature);write_quantity(out,s.manual.hold);write_quantity(out,s.manual.drain);
    for(auto b:s.manual.bytes_38)*out++=b;
    write_parameters(out,s.parameters);write_parameters(out,s.coffee_defaults);*out++=s.abv_pending;*out++=s.ibu_pending;
    *out++=s.assertion_line;*out++=events.size();*out++=context.assertions;
    for(unsigned i=0;i<events.size();++i)output[i]=events[i];
    return static_cast<unsigned>(status);
}
