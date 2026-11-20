#include "zseries/head_core_events.hpp"
#include "core_event_fixture.hpp"
#include <cstring>
#include <vector>
using namespace zseries::oem;
using Word=std::uint32_t;
namespace {
struct Context { std::vector<Word> actions;Word clock=0,counter=0,rinse=0; };
void string(std::vector<Word>& out,const char* p){const std::size_t size=p?std::strlen(p):0;out.push_back(size);for(std::size_t i=0;i<size;++i)out.push_back(static_cast<unsigned char>(p[i]));}
bool perform(void* p,const HeadCoreAction& action,std::uint32_t& result){
    auto& c=*static_cast<Context*>(p);auto& out=c.actions;
    out.insert(out.end(),{static_cast<Word>(action.effect),action.first,action.second,action.third});string(out,action.title);string(out,action.text);
    if(action.event){const auto bytes=core_event_fixture::save(*action.event);out.push_back(bytes.size()-4);for(std::size_t i=4;i<bytes.size();++i)out.push_back(bytes[i]);}else out.push_back(0);
    result=c.rinse;return true;
}
Word millis(void* p){return static_cast<Context*>(p)->clock;}
bool assertion(void* p,Word){++static_cast<Context*>(p)->counter;return true;}
}
extern "C" unsigned head_core_test(const Word* args,const Word* values,const unsigned char* raw,const char* token,const char* network,Word* result,Word* effects){
    HeadCoreEvents head;Context context;context.rinse=values[13];context.clock=values[14];context.counter=values[15];
    head.services.context=&context;head.services.perform=perform;head.services.millis=millis;head.services.assertion=assertion;head.language=static_cast<LanguageType>(args[1]);
    auto& o=head.observed;o.runtime=static_cast<RuntimeStateId>(values[0]);std::memcpy(&o.download_received,values+1,4);std::memcpy(&o.download_total,values+2,4);std::memcpy(&o.interrupted_sessions,values+3,4);std::memcpy(&o.maintenance_count,values+4,4);o.maintenance_limit=values[5];o.maintenance_date=values[6];o.registration_token=token;o.network_detail=network;
    auto& s=head.state;s.transient.active=values[7];s.transient.duration=values[8];s.transient.deadline=values[9];s.power_prompt_duration=values[10];s.update_progress_visible=values[11];s.display_dirty=values[12];
    CoreEventMailbox mailbox;core_event_fixture::load(mailbox.value,raw);mailbox.pending=args[2];HeadCoreResult status=HeadCoreResult::Ok;Word still_type=0,still_state=0;
    if(args[0]==2){PendingEvent<CoreStillEvent> still;still.value.type=static_cast<StillEventId>(static_cast<Word>(mailbox.value.type));still.value.state=static_cast<PicoStillStateId>(mailbox.value.argument);still.pending=args[2];const auto event=consume_head_still_event(still);mailbox.pending=still.pending;still_type=static_cast<Word>(event.type);still_state=static_cast<Word>(event.state);}
    else status=head.consume(mailbox);
    const Word state[]={s.transient.active,s.transient.duration,s.transient.deadline,s.update_progress_visible,s.display_dirty,s.assertion_line,context.counter,mailbox.pending,still_type,still_state,static_cast<Word>(context.actions.size())};
    std::copy(state,state+11,result);std::copy(context.actions.begin(),context.actions.end(),effects);return static_cast<unsigned>(status);
}
