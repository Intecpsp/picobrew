// Original quantity editor, unit formatting and discrete selection model.
#pragma once
#include "zseries/head_widgets.hpp"
#include "zseries/head_encoder.hpp"
#include "zseries/head_numeric_inputs.hpp"
namespace zseries { namespace oem {
bool head_quantity_text(char*,std::size_t,const Quantity&,bool metric,std::int32_t precision=-1);
struct HeadQuantityState {
    DisplayRect rect=DisplayBounds;
    HeadQuantityId id=HeadQuantityId::CoffeeBatchSize;
    Unit unit=Unit::None;
    bool metric=true;
    double minimum=0,step=1;
};
enum class HeadQuantityResult {Ok,MissingService,ServiceFailure,AssertionFailure,InvalidState,RenderFailure};
struct HeadQuantityServices {
    void* context=nullptr;
    bool (*submit)(void*,HeadQuantityId,Quantity)=nullptr;
};
class HeadQuantity {
public:
    HeadQuantity(HeadWidgetPainter& value,EncoderQuantity& selected)noexcept:painter(value),selection(selected){}
    HeadWidgetPainter& painter;
    EncoderQuantity& selection;
    HeadQuantityState state;
    HeadQuantityServices services;
    HeadQuantityResult initialize(const DisplayRect&,HeadQuantityId,Quantity,bool metric); // 0x400eb5b8
    HeadQuantityResult draw(); // 0x400eb500
    HeadQuantityResult changed(); // 0x400eb578
    HeadQuantityResult button(); // 0x400e6aac
    Quantity value()const noexcept{return {state.minimum+state.step*selection.selected,state.unit};}
private:
    HeadQuantityResult assertion(std::uint32_t);
    HeadQuantityResult text(const DisplayRect&,DisplayFontId,const char*,DisplayTextAlignment,std::int32_t row=0);
};
}} // namespace zseries::oem
