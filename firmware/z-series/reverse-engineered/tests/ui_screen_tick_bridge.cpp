#include "zseries/ui_screen_tick.hpp"
using namespace zseries::oem;
namespace {
struct Context {std::uint32_t* out;unsigned count;};
bool perform(void* p,UiScreenTickEffect e,std::uint32_t a,std::uint32_t b){auto& c=*static_cast<Context*>(p);c.out[c.count++]=static_cast<unsigned>(e);c.out[c.count++]=a;c.out[c.count++]=b;return true;}
}
extern "C" unsigned ui_screen_tick_test(const std::uint32_t* in,std::uint32_t now,std::uint32_t* out,std::uint32_t* effects){
    UiScreenTick tick;auto& s=tick.state;
    s.combined=in[0];
    s.combined_caret.enabled=in[1];
    s.combined_caret.deadline=in[2];
    s.combined_caret.interval_ms=in[3];
    s.runtime_state.enabled=in[4];
    s.runtime_state.deadline=in[5];
    s.still_transient.enabled=in[6];
    s.still_transient.deadline=in[7];
    s.pause.enabled=in[8];
    s.pause.deadline=in[9];
    s.runtime_transient.enabled=in[10];
    s.runtime_transient.deadline=in[11];
    s.notification.enabled=in[12];
    s.notification.deadline=in[13];
    s.waiting.enabled=in[14];
    s.waiting.animate=in[15];
    s.waiting.frame=in[16];
    s.waiting.frame_count=in[17];
    s.waiting.deadline=in[18];
    s.level.enabled=in[19];
    s.level.deadline=in[20];
    s.paragraph_caret.enabled=in[21];
    s.paragraph_caret.deadline=in[22];
    s.paragraph_caret.interval_ms=in[23];
    Context context{effects,0};tick.services.context=&context;tick.services.perform=perform;
    auto result=tick.tick(now);
    out[0]=s.combined;
    out[1]=s.combined_caret.enabled;
    out[2]=s.combined_caret.deadline;
    out[3]=s.combined_caret.interval_ms;
    out[4]=s.runtime_state.enabled;
    out[5]=s.runtime_state.deadline;
    out[6]=s.still_transient.enabled;
    out[7]=s.still_transient.deadline;
    out[8]=s.pause.enabled;
    out[9]=s.pause.deadline;
    out[10]=s.runtime_transient.enabled;
    out[11]=s.runtime_transient.deadline;
    out[12]=s.notification.enabled;
    out[13]=s.notification.deadline;
    out[14]=s.waiting.enabled;
    out[15]=s.waiting.animate;
    out[16]=s.waiting.frame;
    out[17]=s.waiting.frame_count;
    out[18]=s.waiting.deadline;
    out[19]=s.level.enabled;
    out[20]=s.level.deadline;
    out[21]=s.paragraph_caret.enabled;
    out[22]=s.paragraph_caret.deadline;
    out[23]=s.paragraph_caret.interval_ms;
    out[24]=context.count;return static_cast<unsigned>(result);
}
