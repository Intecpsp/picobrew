#pragma once
#include "zseries/head_dynamic.hpp"
namespace zseries { namespace oem {
class HeadHardwarePainter {
public:
    HeadHardwarePainter(HeadWidgetPainter& output,const HeadDynamicObserved& values)noexcept:painter(output),observed(values){}
    HeadWidgetPainter& painter;
    const HeadDynamicObserved& observed;
    HeadWidgetResult draw(const DisplayRect&,bool metric,std::uint32_t extra);
};
}} // namespace zseries::oem
