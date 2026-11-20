#include "zseries/head_dynamic.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
namespace zseries { namespace oem {
namespace {
const char* const pumps[]={"---","--K","-D-","-DK","H--","H-K","HD-","HDK"};
const char* const valves[]={"????","Home","Pass","Rest","Mash","Z1  ","Z2P1","P2  ","ZP3 ","ZP4 "};
const char* const locations[]={"????","Pass","Mash","Adj1","Adj2","Adj3","Adj4"};
void format(char* out,std::size_t size,const char* pattern,...){va_list args;va_start(args,pattern);::vsnprintf(out,size,pattern,args);va_end(args);}
const char* nonnull(const char* value){return value?value:"";}
}
void head_duration_text(char* out,std::size_t capacity,std::uint32_t seconds,bool omit_zero_hours){
    seconds=std::min<std::uint32_t>(3599999,seconds);const auto hours=seconds/3600,minutes=seconds%3600/60,remainder=seconds%60;
    if(!omit_zero_hours||hours)format(out,capacity,"%d:%02d:%02d",hours,minutes,remainder);
    else if(minutes)format(out,capacity,"%d:%02d",minutes,remainder);else format(out,capacity,"%d",remainder);
}
bool head_instruction_argument_text(char* out,std::size_t capacity,const Instruction& instruction){
    std::array<char,32> argument{};const auto op=static_cast<std::uint32_t>(instruction.opcode),value=instruction.argument;
    if(op==27)format(argument.data(),argument.size(),"%s",valves[static_cast<std::uint8_t>(value)-1u<9u?static_cast<std::uint8_t>(value):0]);
    else if(op==6){if(value>=7)return false;format(argument.data(),argument.size(),"%s",locations[value]);}
    else if(op==28||op==7){const auto index=op==28?(value&255u)>>4:value&15u;if(index>=8)return false;format(argument.data(),argument.size(),"%s",pumps[index]);}
    else if(op==29)format(argument.data(),argument.size(),"%s",value==1?"PPK":"ZYM");
    else if(op==25||op==2||op-35u<2u){const double temperature=value/256.0;format(argument.data(),argument.size(),"%3.1fC %3.1fF",temperature,temperature*1.8+32);}
    else if(op<27&&(0x5002038u&(1u<<op)))head_duration_text(argument.data(),argument.size(),value);
    else if(op-30u<2u)format(argument.data(),argument.size(),"%s",to_string(static_cast<ScreenId>(value)));
    else format(argument.data(),argument.size(),"0x%04x",value);
    format(out,capacity,"%s",argument.data());return true;
}
bool head_instruction_text(char* out,std::size_t capacity,const Instruction& instruction){
    std::array<char,32> argument{};if(!head_instruction_argument_text(argument.data(),argument.size(),instruction))return false;
    format(out,capacity,"%-13s %6s",to_string(instruction.opcode),argument.data());return true;
}
#define DYNAMIC_TRY(value) do{const auto result=(value);if(result!=HeadWidgetResult::Ok)return result;}while(false)
HeadWidgetResult HeadDynamicPainter::assertion(std::uint32_t line){painter.assertion_line=line;if(!painter.services.assertion)return HeadWidgetResult::MissingService;return painter.services.assertion(painter.services.context,line)?HeadWidgetResult::Ok:HeadWidgetResult::AssertionFailure;}
HeadWidgetResult HeadDynamicPainter::line(const DisplayRect& rect,DisplayFontId id,const char* value,std::int32_t row,DisplayTextAlignment alignment,std::int32_t offset_y){
    if(!value)return HeadWidgetResult::InvalidText;
    const auto* font=display_font(id);DisplayTextContext context;
    if(!font||display_text_font(context,*font,1)!=DisplayTextResult::Ok||display_text_bounds(context,rect)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidRectangle;
    DisplayTextOptions options;options.row=row;options.alignment=alignment;options.offset_y=offset_y;
    return display_text(painter.display,context,value,std::strlen(value)+1,options)==DisplayTextResult::Ok?HeadWidgetResult::Ok:HeadWidgetResult::InvalidText;
}
HeadWidgetResult HeadDynamicPainter::runtime_info(const DisplayRect& rect){
    if(display_rectangle(painter.display,rect,0,true)!=DisplayResult::Ok)return HeadWidgetResult::InvalidRectangle;
    if(observed.core.session_type==static_cast<SessionType>(6))DYNAMIC_TRY(line(rect,DisplayFontId::Font6x8,observed.session.name.data()));
    if(*nonnull(observed.runtime.step_name))DYNAMIC_TRY(line(rect,DisplayFontId::Font6x8,observed.runtime.step_name,1));
    std::array<char,32> text{};if(!head_instruction_text(text.data(),text.size(),observed.runtime.instruction))return HeadWidgetResult::InvalidText;
    DYNAMIC_TRY(line(rect,DisplayFontId::Font6x8,text.data(),2));painter.dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadDynamicPainter::runtime_state(){
    display_clear(painter.display);std::array<char,144> text{};std::array<char,32> buffer{};
    format(text.data(),text.size(),"PID: %s %3.0f%%  ",observed.heater.started?"On ":"Off",observed.heater.output_ticks);
    DYNAMIC_TRY(line(DisplayBounds,DisplayFontId::Font6x8,text.data(),0,DisplayTextAlignment::Left));
    const auto temperature=[&](double value){if(observed.imperial)format(buffer.data(),buffer.size(),"%3.0f\xc2\xb0" "F",value*1.8+32);else format(buffer.data(),buffer.size(),"%3.1f\xc2\xb0" "C",value);};
    temperature(observed.heater.target_celsius);DYNAMIC_TRY(line(DisplayBounds,DisplayFontId::Font6x8,buffer.data(),0,DisplayTextAlignment::Right));
    for(std::uint32_t i=0;i<4;++i){static const std::uint32_t indices[]={0,1,3,2};static const char* const labels[]={"H: ","W: ","A: ","D: "};temperature(observed.heater_sensors.temperatures[indices[i]].celsius);format(text.data(),text.size(),"%s%s",labels[i],buffer.data());DYNAMIC_TRY(line(DisplayBounds,DisplayFontId::Font6x8,text.data(),1+i/2,i%2?DisplayTextAlignment::Right:DisplayTextAlignment::Left));}
    if((observed.msp.pumps>>4)>=8)return HeadWidgetResult::InvalidText;
    format(text.data(),text.size(),"MSP: %02x %s %s%s %x",observed.msp.status_byte_3,observed.msp.moving_valve?"****":valves[observed.msp.valve_position-1u<9u?observed.msp.valve_position:0],pumps[observed.msp.pumps>>4],observed.msp.fan?"F":"-",observed.msp.errors);
    DYNAMIC_TRY(line(DisplayBounds,DisplayFontId::Font6x8,text.data(),3,DisplayTextAlignment::Left));
    format(text.data(),text.size(),"SF: %s",observed.safety.filter_missing?"Out":"In ");DYNAMIC_TRY(line(DisplayBounds,DisplayFontId::Font6x8,text.data(),4,DisplayTextAlignment::Left));
    std::array<char,32> remaining{};head_duration_text(buffer.data(),buffer.size(),observed.runtime.elapsed_seconds);head_duration_text(remaining.data(),remaining.size(),observed.runtime.instruction_left_seconds);
    format(text.data(),text.size(),"RT: %c %s %s",*to_string(observed.runtime.id),buffer.data(),remaining.data());DYNAMIC_TRY(line(DisplayBounds,DisplayFontId::Font6x8,text.data(),5,DisplayTextAlignment::Left));
    if(!head_instruction_text(buffer.data(),buffer.size(),observed.runtime.instruction))return HeadWidgetResult::InvalidText;
    format(text.data(),text.size(),"%2d %s   ",observed.runtime.instruction_count,buffer.data());DYNAMIC_TRY(line(DisplayBounds,DisplayFontId::Font6x8,text.data(),6,DisplayTextAlignment::Left));
    painter.dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadDynamicPainter::pause(const DisplayRect& rect,PauseReason pause_reason){
    const auto reason=static_cast<std::uint32_t>(pause_reason);const auto local=[&](std::uint32_t id){return ui_localized_text(UiTextCategory::TextPause,id,painter.language);};
    if(reason==0||reason-9u<3u||reason==18)return HeadWidgetResult::Ok;
    std::array<std::array<char,24>,3> temperatures{};for(std::uint32_t i=0;i<3;++i){double value=observed.heater_sensors.temperatures[i].celsius;if(observed.imperial)value=value*1.8+32;format(temperatures[i].data(),temperatures[i].size(),"%s%6.1f\xc2\xb0%c",local(i+1),value,observed.imperial?'F':'C');}
    std::array<char,65> code{};if(reason==15)format(code.data(),code.size(),"%s%d-%d)",local(0),reason,observed.msp.errors);else format(code.data(),code.size(),"%s%d)",local(0),reason);
    std::array<const char*,4> lines{{"","","",""}};std::uint32_t count=0;
    if(reason==1||reason==2){if(observed.session.name[0]&&std::strcmp(observed.session.name.data(),"<None>"))lines[count++]=observed.session.name.data();if(reason==1&&*nonnull(observed.runtime.step_name)){lines[count++]=observed.runtime.step_name;if(*nonnull(observed.runtime.substep_name))lines[count++]=observed.runtime.substep_name;}}
    else if(reason==3)lines[count++]=local(4);
    else if(reason==4||reason==5){for(auto& value:temperatures)lines[count++]=value.data();lines[count++]=code.data();}
    else if(reason==6||reason==7){lines[count++]=temperatures[reason==6?0:2].data();lines[count++]=temperatures[1].data();lines[count++]=code.data();}
    else if(reason==8){lines[count++]=temperatures[0].data();lines[count++]=code.data();}
    else if(reason==12){if(observed.still.id!=static_cast<PicoStillStateId>(19)&&!observed.still_connectivity_error&&!observed.still_samples.current.error[0])DYNAMIC_TRY(assertion(0x1a0));lines[count++]=observed.still_connectivity_error?"Connectivity Error":observed.still_samples.current.error[0]?observed.still_samples.current.error.data():observed.still.id==static_cast<PicoStillStateId>(19)?"General error":"";}
    else if(reason==13)lines[count++]=local(5);else if(reason-14u<3u)lines[count++]=code.data();else if(reason==17)lines[count++]=local(6);
    if(!display_rect_valid(rect)){DYNAMIC_TRY(assertion(0x86f));return HeadWidgetResult::InvalidRectangle;}
    if(display_rectangle(painter.display,rect,0,true)!=DisplayResult::Ok)return HeadWidgetResult::InvalidRectangle;
    const auto visible=11+(count?5+4+9*count:0);const auto center=(rect.top+rect.bottom+1)/2;const auto top=center-static_cast<std::int32_t>(visible/2);
    if(display_rect_height(rect)<visible-4)DYNAMIC_TRY(assertion(0x879));
    DYNAMIC_TRY(line(rect,DisplayFontId::Font9x11,ui_localized_text(UiTextCategory::PauseReason,reason,painter.language),0,DisplayTextAlignment::Center,top));
    if(count){const auto body_top=top+16-(count==4?3:0);auto y=body_top+2;std::uint32_t longest=0;
        for(std::uint32_t i=0;i<count;++i){std::uint32_t length=0;if(display_utf8_length(lines[i],std::strlen(lines[i])+1,length)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidText;longest=std::max(longest,length);DYNAMIC_TRY(line(rect,DisplayFontId::Font6x8,lines[i],0,DisplayTextAlignment::Center,y));y+=9;}
        const auto width=std::max<std::uint32_t>(84,8+longest*6);const auto cx=(rect.left+rect.right)/2;const DisplayRect box{cx-static_cast<std::int32_t>(width/2),body_top,cx+static_cast<std::int32_t>(width/2)-2,y+2};
        if(display_rectangle(painter.display,box,15,false)!=DisplayResult::Ok)return HeadWidgetResult::InvalidRectangle;
    }painter.dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadDynamicPainter::level(){
    const auto& acceleration=observed.auxiliary.acceleration;for(double value:acceleration)if(!std::isfinite(value))return HeadWidgetResult::InvalidText;
    std::array<char,64> text{};format(text.data(),text.size(),"% 1.2f % 1.2f % 1.2f",acceleration[0],acceleration[1],acceleration[2]);
    DisplayTextContext context;const auto* font=display_font(DisplayFontId::Font6x8);if(!font||display_text_font(context,*font,0)!=DisplayTextResult::Ok||display_text_bounds(context,DisplayBounds)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidRectangle;
    DisplayTextOptions options;options.alignment=DisplayTextAlignment::Center;if(display_text(painter.display,text_context?*text_context:context,text.data(),text.size(),options)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidText;
    const DisplayRect rect{39,12,87,60};if(display_rectangle(painter.display,rect,0,true)!=DisplayResult::Ok||display_rectangle(painter.display,rect,15,false)!=DisplayResult::Ok)return HeadWidgetResult::InvalidRectangle;
    const double x=std::max(-1.0,std::min(1.0,-10*acceleration[1])),y=std::max(-1.0,std::min(1.0,-10*acceleration[2]));
    double left=std::max(2.0,(1-x)*49*0.5-2),top=std::max(2.0,(y+1)*49*0.5-2);if(49<left+7)left=42;if(49<top+7)top=42;
    const auto px=static_cast<std::int32_t>(39+left),py=static_cast<std::int32_t>(12+top);if(display_rectangle(painter.display,{px,py,px+4,py+4},15,false)!=DisplayResult::Ok||display_rectangle(painter.display,{63,36,63,36},15,false)!=DisplayResult::Ok)return HeadWidgetResult::InvalidRectangle;
    painter.dirty=true;return HeadWidgetResult::Ok;
}
#undef DYNAMIC_TRY
}} // namespace zseries::oem
