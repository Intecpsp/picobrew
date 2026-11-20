#include "zseries/head_encoder.hpp"
#include <cstring>
using namespace zseries::oem;
namespace {
struct Context { std::uint32_t clock,*out,count; };
void record(Context& c,std::uint32_t a,std::uint32_t b=0,std::uint32_t d=0){c.out[c.count++]=a;c.out[c.count++]=b;c.out[c.count++]=d;}
bool perform(void* p,HeadEncoderEffect effect,std::uint32_t a,std::uint32_t b){record(*static_cast<Context*>(p),static_cast<std::uint32_t>(effect),a,b);return true;}
std::uint32_t millis(void* p){auto& c=*static_cast<Context*>(p);const auto now=c.clock;c.clock+=17;record(c,11,now);return now;}
std::int32_t sign(std::uint32_t v){std::int32_t x;std::memcpy(&x,&v,4);return x;}
}
extern "C" unsigned head_encoder_test(const std::uint32_t* in,std::uint32_t delta,std::uint32_t clock,std::uint32_t* result,std::uint32_t* effects){
    HeadEncoder encoder;auto& s=encoder.state;unsigned i=0;
    s.combined.enabled=in[i++];s.menu.enabled=in[i++];s.menu.selected=in[i++];s.menu.count=in[i++];s.menu.visible_rows=in[i++];s.menu.selection_row=in[i++];s.menu.scroll_offset=in[i++];
    s.slideshow.enabled=in[i++];s.slideshow.selected=in[i++];s.slideshow.count=in[i++];s.slideshow.timer.active=in[i++];s.slideshow.timer.duration=in[i++];s.slideshow.timer.deadline=in[i++];
    s.paragraph.enabled=in[i++];s.paragraph.offset=sign(in[i++]);s.paragraph.visible_lines=sign(in[i++]);s.paragraph.total_lines=sign(in[i++]);
    s.character.enabled=in[i++];s.character.selected=sign(in[i++]);s.character.count=sign(in[i++]);
    s.temperature.enabled=in[i++];s.temperature.value=sign(in[i++]);s.temperature.minimum=sign(in[i++]);s.temperature.maximum=sign(in[i++]);
    s.duration.enabled=in[i++];s.duration.seconds=in[i++];s.strength.enabled=in[i++];s.strength.value=sign(in[i++]);
    for(auto& q:s.quantities){q.enabled=in[i++];q.wrap=in[i++];q.selected=sign(in[i++]);q.count=sign(in[i++]);}
    s.diagnostic_line=in[i++];s.combined.selected=in[i++];s.combined.paragraph_count=in[i++];s.combined.menu_count=in[i++];
    s.transient_timer.active=in[i++];s.transient_timer.duration=in[i++];s.transient_timer.deadline=in[i++];s.display_dirty=in[i++];
    Context context{clock,effects,0};encoder.services.context=&context;encoder.services.perform=perform;encoder.services.millis=millis;encoder.services.assertion=[](void*,std::uint32_t){return true;};
    auto status=encoder.rotate(sign(delta));i=0;
    result[i++]=s.combined.enabled;result[i++]=s.menu.enabled;result[i++]=s.menu.selected;result[i++]=s.menu.count;result[i++]=s.menu.visible_rows;result[i++]=s.menu.selection_row;result[i++]=s.menu.scroll_offset;
    result[i++]=s.slideshow.enabled;result[i++]=s.slideshow.selected;result[i++]=s.slideshow.count;result[i++]=s.slideshow.timer.active;result[i++]=s.slideshow.timer.duration;result[i++]=s.slideshow.timer.deadline;
    result[i++]=s.paragraph.enabled;result[i++]=s.paragraph.offset;result[i++]=s.paragraph.visible_lines;result[i++]=s.paragraph.total_lines;
    result[i++]=s.character.enabled;result[i++]=s.character.selected;result[i++]=s.character.count;
    result[i++]=s.temperature.enabled;result[i++]=s.temperature.value;result[i++]=s.temperature.minimum;result[i++]=s.temperature.maximum;
    result[i++]=s.duration.enabled;result[i++]=s.duration.seconds;result[i++]=s.strength.enabled;result[i++]=s.strength.value;
    for(auto& q:s.quantities){result[i++]=q.enabled;result[i++]=q.wrap;result[i++]=q.selected;result[i++]=q.count;}
    result[i++]=s.diagnostic_line;result[i++]=s.combined.selected;result[i++]=s.combined.paragraph_count;result[i++]=s.combined.menu_count;
    result[i++]=s.transient_timer.active;result[i++]=s.transient_timer.duration;result[i++]=s.transient_timer.deadline;result[i++]=s.display_dirty;
    result[i++]=s.assertion_line;result[i++]=context.count;return static_cast<unsigned>(status);
}
