#include "zseries/head_remote_session.hpp"
#include <cstring>
#include <vector>
using namespace zseries::oem;using Word=std::uint32_t;
namespace {
struct Context{Word now=0,count=7;std::vector<std::vector<Word>> events;};
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

std::vector<Word> parameters(const ProgramParameters& value){std::vector<Word> out(16);Word* data=out.data();write_parameters(data,value);return out;}
bool assertion(void* p,const char* file,Word line,const char* expression){auto& c=*static_cast<Context*>(p);std::vector<Word> row{0,line};for(const auto* value:{file,expression}){const auto size=std::strlen(value);row.push_back(size);for(std::size_t i=0;i<size;++i)row.push_back(static_cast<unsigned char>(value[i]));}c.events.push_back(row);++c.count;c.events.push_back({1,200});return true;}
bool azure(void* p,const HeadAzureEvent& event){auto& c=*static_cast<Context*>(p);std::vector<Word> row{2,static_cast<Word>(event.type),static_cast<Word>(event.program)};auto data=parameters(event.parameters);row.insert(row.end(),data.begin(),data.end());c.events.push_back(row);return true;}
bool core(void* p,const HeadUiCoreEnvelope& event){auto& c=*static_cast<Context*>(p);const auto& recipe=event.data.recipe;std::vector<Word> row{3,static_cast<Word>(event.command.id),event.data.flags,recipe.id,static_cast<Word>(recipe.kind)};auto data=parameters(event.data.parameters);row.insert(row.end(),data.begin(),data.end());const auto length=std::strlen(recipe.name.data());row.push_back(length);for(std::size_t i=0;i<length;++i)row.push_back(static_cast<unsigned char>(recipe.name[i]));c.events.push_back(row);return true;}
}
extern "C" unsigned head_remote_test(const Word* args,const Word* values,const char* name,Word* output){
 AzureCommand command;command.id=static_cast<CoreCommandId>(args[0]);command.session.program=static_cast<ZProgramId>(args[1]);command.session.type=static_cast<SessionType>(args[2]);command.session.recipe_id=args[3];std::strncpy(command.session.name.data(),name,64);read_parameters(values,command.session.parameters);Context context;context.now=args[4];HeadRemoteServices services;services.context=&context;services.millis=[](void* p){return static_cast<Context*>(p)->now;};services.assertion=assertion;services.post_azure=azure;services.post_core=core;services.discard=[](void* p){static_cast<Context*>(p)->events.push_back({4});return true;};const auto result=head_remote_session(command,args[5],args[6],services);*output++=context.count;*output++=context.events.size();for(const auto& row:context.events){*output++=row.size();for(auto value:row)*output++=value;}return static_cast<unsigned>(result);
}
