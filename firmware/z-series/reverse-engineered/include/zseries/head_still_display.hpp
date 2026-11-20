#pragma once
#include "zseries/head_dynamic.hpp"
namespace zseries { namespace oem {
class HeadStillDisplay {
public:
    HeadStillDisplay(HeadWidgetPainter& output,const HeadDynamicObserved& values)noexcept:painter(output),observed(values){}
    HeadWidgetPainter& painter;
    const HeadDynamicObserved& observed;
    HeadWidgetResult draw(const DisplayRect&,bool imperial);
};
}} // namespace zseries::oem
