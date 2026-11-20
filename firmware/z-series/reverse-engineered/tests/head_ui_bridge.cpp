#include "head_ui_fixture.hpp"
#include <cstring>
#include <vector>
using namespace zseries::oem;
using namespace head_ui_fixture;
namespace {
struct Context{std::vector<Word> actions;Word clock=0,counter=0;};
void list(std::vector<Word>& out,const std::vector<Word>& value){out.push_back(value.size());out.insert(out.end(),value.begin(),value.end());}
void bytes(std::vector<Word>& out,const std::vector<std::uint8_t>& value){out.push_back(value.size());for(auto b:value)out.push_back(b);}
void string(std::vector<Word>& out,const char* p){const auto size=p?std::strlen(p):0;out.push_back(size);for(std::size_t i=0;i<size;++i)out.push_back(static_cast<unsigned char>(p[i]));}
bool perform(void* p,const HeadUiAction& action){
    auto& c=*static_cast<Context*>(p);auto& out=c.actions;out.insert(out.end(),{static_cast<Word>(action.effect),action.first,action.second,action.third});string(out,action.title);string(out,action.text);
    std::vector<Word> info,command,search;std::vector<std::uint8_t> r,e;
    if(action.effect==HeadUiEffect::Screen){const auto& s=action.screen;info={s.layout,static_cast<Word>(s.command),static_cast<Word>(s.response),s.argument,s.selection,s.strength,s.quantity_id};quantity(info,s.quantity);info.push_back(s.input_kind);info.push_back(s.input_limit);}
    if(action.core_command){const auto& core=*action.core_command;const auto* data=action.data;command={static_cast<Word>(core.id),core.program_id,core.duration,core.still_id,data?data->index:0,data?data->flags:0u,data&&data->has_search?1u:0u};
        if(core.id==static_cast<CoreCommandId>(54))manual(command,core.manual);
        if(data&&(data->flags&8))parameters(command,data->parameters);
        if(data&&(data->flags&4))r=recipe(data->recipe);
        if(data&&data->has_search)for(char b:data->search){if(!b)break;search.push_back(static_cast<unsigned char>(b));}
    }
    if(action.recipe)r=recipe(*action.recipe);
    if(action.event)e=event(*action.event);
    list(out,info);list(out,command);list(out,search);bytes(out,r);bytes(out,e);return true;
}
Word millis(void* p){return static_cast<Context*>(p)->clock;}
bool assertion(void* p,Word){++static_cast<Context*>(p)->counter;return true;}
}
extern "C" unsigned head_ui_test(const Word* args,const Word* values,const Word* params,const Word* step,const unsigned char* selected,const unsigned char* payload,const char* search,const char* name,Word* result,Word* effects){
    HeadNumericState numeric;HeadTextState text;HeadTimer timer;HeadUiCommands head(numeric,text,timer);Context context;context.clock=values[13];context.counter=values[14];
    head.services.context=&context;head.services.perform=perform;head.services.millis=millis;head.services.assertion=assertion;head.language=static_cast<LanguageType>(args[1]);
    head.observed.core=static_cast<CoreStateId>(values[0]);head.observed.session=static_cast<SessionType>(values[1]);head.observed.session_name_length=values[2];head.observed.session_name=name;head.observed.service_mode=values[3];head.observed.t1_monitor=values[4];head.observed.runtime_location=values[5];
    timer.active=values[6];timer.duration=values[7];timer.deadline=values[8];head.state.runtime_menu_duration=values[9];head.state.display_suspended=values[10];numeric.abv_pending=values[11];numeric.ibu_pending=values[12];
    text.kit_length=values[15];text.kit_truncated=values[16];std::strncpy(text.kit_code.data(),search,16);parameters(numeric.parameters,params);manual(numeric.manual,step);recipe(head.state.recipe,selected);
    PendingEvent<HeadUiEvent> mailbox;mailbox.pending=args[4];auto& input=mailbox.value;input.command=static_cast<UiCommand>(args[0]);input.flags=args[2];input.index=args[3];recipe(input.recipe,payload);parameters(input.parameters,params+16);
    const auto status=head.consume(mailbox);std::vector<Word> out={timer.active,timer.duration,timer.deadline,head.state.display_suspended,numeric.abv_pending,numeric.ibu_pending,head.state.assertion_line,context.counter,mailbox.pending,text.kit_length,text.kit_truncated};
    parameters(out,numeric.parameters);manual(out,numeric.manual);for(auto b:text.kit_code)out.push_back(static_cast<unsigned char>(b));for(auto b:recipe(head.state.recipe))out.push_back(b);out.push_back(context.actions.size());
    std::copy(out.begin(),out.end(),result);std::copy(context.actions.begin(),context.actions.end(),effects);return static_cast<unsigned>(status);
}
