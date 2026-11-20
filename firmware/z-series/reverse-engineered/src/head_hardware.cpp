#include "zseries/head_hardware.hpp"
#include "zseries/head_presentation.hpp"
namespace zseries { namespace oem {
HeadWidgetResult HeadHardwarePainter::draw(const DisplayRect& rect,bool metric,std::uint32_t extra){
    HeadPresentation output(painter);HeadTextLines lines;
    if(extra==1){
        if(observed.runtime.t1_drop_watch.triggered){const auto result=output.add(lines,"T1 has dropped!");if(result!=HeadWidgetResult::Ok)return result;}
        const auto current=observed.still_samples.current.temperature[0],maximum=observed.runtime.t1_drop_watch.maximum;
        auto result=output.add(lines,"Current: %5.1f \xc2\xb0%c",metric?current:current*1.8+32.0,metric?'C':'F');if(result!=HeadWidgetResult::Ok)return result;
        result=output.add(lines,"Max:     %5.1f \xc2\xb0%c",metric?maximum:maximum*1.8+32.0,metric?'C':'F');if(result!=HeadWidgetResult::Ok)return result;
    }
    return output.lines(lines,rect);
}
}} // namespace zseries::oem
