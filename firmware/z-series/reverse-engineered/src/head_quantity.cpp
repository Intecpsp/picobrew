#include "zseries/head_quantity.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace zseries { namespace oem {
bool head_quantity_text(char* out,std::size_t capacity,const Quantity& quantity,bool metric,std::int32_t precision){
    if(!out||!capacity||!std::isfinite(quantity.value)||precision>9)return false;
    const auto unit=static_cast<std::uint32_t>(quantity.unit);double value=quantity.value;
    if(unit<2){if(!quantity_in(quantity,metric?Unit::Liter:Unit::Gallon,value))return false;::snprintf(out,capacity,"%3.*f %s",precision<0?1:precision,value,metric?"L":"Gal");}
    else if(unit-2<2){if(!quantity_in(quantity,metric?Unit::Gram:Unit::Ounce,value))return false;if(metric){if(value<1000)::snprintf(out,capacity,"%3.0f grams",value);else ::snprintf(out,capacity,"%3.2f kg",value/1000);}else{const auto pounds=static_cast<std::int32_t>(value/16);::snprintf(out,capacity,"%d lb %d Oz",pounds,static_cast<std::int32_t>(std::round(value-pounds*16)));}}
    else if(unit-4<2){if(!quantity_in(quantity,metric?Unit::Celsius:Unit::Fahrenheit,value))return false;::snprintf(out,capacity,"%3.*f%s",precision<0?0:precision,value,metric?"\xc2\xb0" "C":"\xc2\xb0" "F");}
    else if(unit==6){if(value<0||value>4294967295.0)return false;const auto seconds=static_cast<std::uint32_t>(value);if(seconds<60)::snprintf(out,capacity,"%d sec",static_cast<std::int32_t>(seconds));else if(seconds<3600){if(seconds%60)::snprintf(out,capacity,"%d min %d sec",seconds/60,seconds%60);else ::snprintf(out,capacity,"%d min",seconds/60);}else ::snprintf(out,capacity,"%d hr %d min",seconds/3600,seconds%3600/60);}
    else ::snprintf(out,capacity,"%3.*f",precision<0?1:precision,value);
    return true;
}
#define QUANTITY_TRY(value) do{const auto result=(value);if(result!=HeadQuantityResult::Ok)return result;}while(false)
HeadQuantityResult HeadQuantity::assertion(std::uint32_t line){painter.assertion_line=line;if(!painter.services.assertion)return HeadQuantityResult::MissingService;return painter.services.assertion(painter.services.context,line)?HeadQuantityResult::Ok:HeadQuantityResult::AssertionFailure;}
HeadQuantityResult HeadQuantity::text(const DisplayRect& rect,DisplayFontId font_id,const char* value,DisplayTextAlignment alignment,std::int32_t row){DisplayTextContext context;const auto* font=display_font(font_id);if(!font||display_text_font(context,*font,1)!=DisplayTextResult::Ok||display_text_bounds(context,rect)!=DisplayTextResult::Ok)return HeadQuantityResult::RenderFailure;DisplayTextOptions options;options.alignment=alignment;options.row=row;return display_text(painter.display,context,value,std::strlen(value)+1,options)==DisplayTextResult::Ok?HeadQuantityResult::Ok:HeadQuantityResult::RenderFailure;}
HeadQuantityResult HeadQuantity::initialize(const DisplayRect& rect,HeadQuantityId id,Quantity input,bool metric){
    if(!display_rect_valid(rect)||!std::isfinite(input.value))return HeadQuantityResult::InvalidState;
    state.rect=rect;state.id=id;state.metric=metric;const auto number=static_cast<std::uint32_t>(id);double maximum=0,current=input.value;
    if(number>=8)QUANTITY_TRY(assertion(0x5e0));
    if(number==1||number==5){state.unit=metric?Unit::Celsius:Unit::Fahrenheit;state.minimum=metric?38:100;maximum=metric?93:200;state.step=1;if(!quantity_in(input,state.unit,current))QUANTITY_TRY(assertion(0x7d));}
    else if(number==4){state.unit=Unit::Second;state.minimum=60;maximum=5400;state.step=60;}
    else if(number==6||number==7){state.unit=Unit::Second;state.minimum=0;maximum=number==6?18000:3600;state.step=60;}
    else if(number==3){state.unit=Unit::Intensity;state.minimum=1;maximum=15;state.step=1;}
    else if(number==0){state.unit=metric?Unit::Liter:Unit::Gallon;state.minimum=metric?4:1;maximum=metric?15.5:4.2;state.step=metric?0.5:0.1;if(!quantity_in(input,state.unit,current))QUANTITY_TRY(assertion(0x7d));}
    else return HeadQuantityResult::InvalidState;
    if(current<state.minimum||current>maximum)QUANTITY_TRY(assertion(0x4ba));
    current=std::max(state.minimum,std::min(maximum,current));selection.count=static_cast<std::int32_t>((maximum-state.minimum)/state.step)+1;selection.selected=static_cast<std::int32_t>((current-state.minimum)/state.step);selection.wrap=false;
    if(selection.enabled)QUANTITY_TRY(assertion(0x4d4));
    selection.enabled=true;return draw();
}
HeadQuantityResult HeadQuantity::draw(){
    if(!display_rect_valid(state.rect)||selection.selected<0||selection.count<=0||selection.selected>=selection.count)return HeadQuantityResult::InvalidState;
    const auto number=static_cast<std::uint32_t>(state.id);const auto quantity=value();
    if(number==4||number==6||number==7){if(quantity.value<0||quantity.value>4294967295.0)return HeadQuantityResult::InvalidState;return painter.duration(state.rect,static_cast<std::uint32_t>(quantity.value))==HeadWidgetResult::Ok?HeadQuantityResult::Ok:HeadQuantityResult::RenderFailure;}
    if(display_rectangle(painter.display,state.rect,0,true)!=DisplayResult::Ok)return HeadQuantityResult::RenderFailure;
    DisplayRect area=state.rect,clipped{};display_rect_translate(area,0,10);if(!display_rect_intersection(area,state.rect,clipped))return HeadQuantityResult::InvalidState;
    std::array<char,32> label{};
    if(number==1||number==5){
        if(!head_quantity_text(label.data(),label.size(),quantity,state.metric,0))return HeadQuantityResult::InvalidState;
        QUANTITY_TRY(text(clipped,DisplayFontId::Font14x16,label.data(),DisplayTextAlignment::Center));
        display_rect_translate(area,0,26);if(!display_rect_intersection(area,state.rect,clipped))return HeadQuantityResult::InvalidState;
        if(!head_quantity_text(label.data(),label.size(),quantity,!state.metric,1))return HeadQuantityResult::InvalidState;
        return text(clipped,DisplayFontId::Font6x8,label.data(),DisplayTextAlignment::Center);
    }
    if(number==0){
        if(!head_quantity_text(label.data(),label.size(),quantity,state.metric))return HeadQuantityResult::InvalidState;
        QUANTITY_TRY(text(clipped,DisplayFontId::Font9x11,label.data(),DisplayTextAlignment::Center));
        display_rect_translate(area,0,21);if(!display_rect_intersection(area,state.rect,clipped))return HeadQuantityResult::InvalidState;
        double liters=0;if(!quantity_in(quantity,Unit::Liter,liters))return HeadQuantityResult::InvalidState;const Quantity water{liters/0.88,Unit::Liter},coffee{water.value*60,Unit::Gram};
        QUANTITY_TRY(text(clipped,DisplayFontId::Font6x8,"Start water",DisplayTextAlignment::Left));if(!head_quantity_text(label.data(),label.size(),water,state.metric))return HeadQuantityResult::InvalidState;
        QUANTITY_TRY(text(clipped,DisplayFontId::Font6x8,label.data(),DisplayTextAlignment::Right));
        QUANTITY_TRY(text(clipped,DisplayFontId::Font6x8,"Coffee",DisplayTextAlignment::Left,1));if(!head_quantity_text(label.data(),label.size(),coffee,state.metric))return HeadQuantityResult::InvalidState;
        return text(clipped,DisplayFontId::Font6x8,label.data(),DisplayTextAlignment::Right,1);
    }
    if(number==3){
        ::snprintf(label.data(),label.size(),"%d",static_cast<std::int32_t>(quantity.value));QUANTITY_TRY(text(clipped,DisplayFontId::Font9x11,label.data(),DisplayTextAlignment::Center));
        const auto width=display_rect_width(clipped)-10;DisplayRect bar{static_cast<std::int32_t>((display_rect_width(clipped)-width)/2),clipped.top+21,static_cast<std::int32_t>((display_rect_width(clipped)-width)/2+width-1),clipped.top+29};
        if(display_rectangle(painter.display,bar,0,true)!=DisplayResult::Ok)return HeadQuantityResult::RenderFailure;
        const auto box_width=(display_rect_width(bar)-28)/15;
        if(box_width<3)return HeadQuantityResult::InvalidState;
        for(std::uint32_t i=0;i<15;++i){const auto left=bar.left+static_cast<std::int32_t>(i*(box_width+2));if(display_rectangle(painter.display,{left,bar.top,left+static_cast<std::int32_t>(box_width)-1,bar.bottom},15,i<static_cast<std::uint32_t>(quantity.value))!=DisplayResult::Ok)return HeadQuantityResult::RenderFailure;}
        return HeadQuantityResult::Ok;
    }
    return HeadQuantityResult::InvalidState;
}
HeadQuantityResult HeadQuantity::changed(){QUANTITY_TRY(draw());painter.dirty=true;return HeadQuantityResult::Ok;}
HeadQuantityResult HeadQuantity::button(){if(!selection.enabled)return HeadQuantityResult::Ok;if(!services.submit)return HeadQuantityResult::MissingService;return services.submit(services.context,state.id,value())?HeadQuantityResult::Ok:HeadQuantityResult::ServiceFailure;}
#undef QUANTITY_TRY
}} // namespace zseries::oem
