#include "zseries/head_slideshow.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
namespace zseries { namespace oem {
namespace {
constexpr double HectopascalsPerPsi=68.9475729317831;
const char* present(const char* text){return text?text:"";}
double temperature(double value,bool imperial){return imperial?value*1.8+32.0:value;}
double deviation(const PicoStillStatistics& stats){return std::sqrt(stats.count>1?stats.squared_deviations/static_cast<double>(stats.count-1):0.0);}
HeadWidgetResult glyphs(HeadWidgetPainter& painter,DisplayFontId font,const char* value,std::int32_t x,std::int32_t y){
    return display_glyphs(painter.display,*display_font(font),value,std::strlen(value)+1,0xffffffffu,{x,y})==DisplayTextResult::Ok?HeadWidgetResult::Ok:HeadWidgetResult::InvalidText;
}
void stop(HeadSlideshowEntry& entry){entry.enabled=false;entry.timer.active=false;entry.timer.deadline=0;}
#define SLIDE_TRY(call) do{const auto result=(call);if(result!=HeadWidgetResult::Ok)return result;}while(false)
HeadWidgetResult stability(HeadPresentation& output,char* text,std::size_t capacity,double value,double first,double second,bool number){
    if(first>second)SLIDE_TRY(output.assertion("UiSlideshow.h",0x22,"threshold1 <= threshold2"));
    if(first>second)std::swap(first,second);
    const auto* glyph=value<first?"\xc2\x90":value<second?"\xc2\x91":"\xc2\x92";
    char original[11];const auto length=number ? ::snprintf(original,sizeof(original),"%s %6.3f",glyph,value) : ::snprintf(original,sizeof(original),"%s",glyph);
    if(length<0)return HeadWidgetResult::InvalidText;
    if(length>=10)SLIDE_TRY(output.log(ConsoleLevel::Warning,"BUF","Buffer.h",0x8b,"Buffer truncated"));
    if(!capacity)return HeadWidgetResult::InvalidText;
    ::snprintf(text,capacity,"%s",original);return HeadWidgetResult::Ok;
}
HeadWidgetResult trend(HeadPresentation& output,const char*& glyph,double value,double first,double second){
    if(first>second)SLIDE_TRY(output.assertion("UiSlideshow.h",0x196,"threshold1 <= threshold2"));
    if(first>second)std::swap(first,second);
    if(std::fabs(value)<first)glyph="\xc2\x88";
    else if(std::fabs(value)<second)glyph=value>0?"\xc2\x8b":"\xc2\x8c";
    else glyph=value>0?"\xc2\x85":"\xc2\x86";
    return HeadWidgetResult::Ok;
}
}
HeadSlideshow::HeadSlideshow(HeadWidgetPainter& output,const HeadDynamicObserved& values,EncoderSlideshow& state)noexcept:painter(output),observed(values),encoder(state){encoder.selected=0xffffffffu;}
HeadWidgetResult HeadSlideshow::assertion(std::uint32_t line,const char* expression){HeadPresentation out(painter,&services.diagnostics);return out.assertion("UiSlideshow.h",line,expression);}
HeadWidgetResult HeadSlideshow::title(HeadSlideshowEntry& entry,std::uint32_t type){
    entry.type=type;entry.title.fill(0);const auto* text=ui_localized_text(UiTextCategory::InfoScreenType,type,painter.language);const auto length=std::strlen(text);
    if(length>20){HeadPresentation output(painter,&services.diagnostics);SLIDE_TRY(output.log(ConsoleLevel::Warning,"BUF","Buffer.h",0x66,"Buffer truncated"));char message[48];::snprintf(message,sizeof(message),"Dropping %u bytes",static_cast<unsigned>(length-20));SLIDE_TRY(output.log(ConsoleLevel::Warning,"BUF","Buffer.h",0x6a,message));}
    std::memcpy(entry.title.data(),text,std::min<std::size_t>(20,length));return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadSlideshow::populate(std::uint32_t mode){
    observed_pid=observed.runtime.pid_active;observed_pump=observed.still_samples.current.pump_running;observed_network=observed.network!=0;std::uint32_t count=0;
    const auto append=[&](std::uint32_t type){if(count>=entries.size())return HeadWidgetResult::InvalidText;return title(entries[count++],type);};
    if(!observed.core.offline&&!observed_network)SLIDE_TRY(append(18));
    switch(mode){
    case 4:case 13:SLIDE_TRY(append(7));SLIDE_TRY(append(8));SLIDE_TRY(append(3));break;
    case 11:
        if(!observed_pump)SLIDE_TRY(append(17));
        if(observed.core.program_id!=7){SLIDE_TRY(append(7));SLIDE_TRY(append(14));}
        SLIDE_TRY(append(12));SLIDE_TRY(append(13));if(show_detail)SLIDE_TRY(append(15));SLIDE_TRY(append(6));SLIDE_TRY(append(3));SLIDE_TRY(append(11));SLIDE_TRY(append(16));break;
    default:
        SLIDE_TRY(append(7));
        if(mode>=6&&mode<=9){SLIDE_TRY(append(1));if(!observed.runtime.flag_b5)SLIDE_TRY(append(2));}else SLIDE_TRY(append(2));
        if(observed_pid)SLIDE_TRY(append(4));
        if(observed.core.service_mode||show_detail){SLIDE_TRY(append(8));SLIDE_TRY(append(9));SLIDE_TRY(append(10));}break;
    }
    encoder.count=count;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadSlideshow::initialize(std::uint32_t mode,UiCommand next,bool use_imperial,bool detail,bool stationary){
    if(!services.millis)return HeadWidgetResult::MissingService;
    for(auto& entry:entries)stop(entry);
    encoder.enabled=true;argument=mode;command=next;imperial=use_imperial;show_detail=detail;static_cycle=stationary;
    SLIDE_TRY(populate(mode));SLIDE_TRY(select(0));encoder.timer.active=!static_cycle;
    if(encoder.timer.active){encoder.timer.duration=4000;head_timer_reset(encoder.timer,services.millis(services.context));}else encoder.timer.deadline=0;
    return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadSlideshow::initialize_all(){
    if(!services.millis)return HeadWidgetResult::MissingService;
    for(auto& entry:entries)stop(entry);
    encoder.enabled=true;command=UiCommand::None;encoder.count=0;
    SLIDE_TRY(assertion(0x3ef,"count >= static_cast<int>(InfoScreenType::_EnumCount)"));
    for(std::uint32_t type=1;type<12;++type)SLIDE_TRY(title(entries[encoder.count++],type));
    SLIDE_TRY(select(0));encoder.timer.duration=4000;encoder.timer.active=true;head_timer_reset(encoder.timer,services.millis(services.context));painter.dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadSlideshow::reset(){
    encoder.timer.active=false;encoder.timer.deadline=0;for(auto& entry:entries)stop(entry);encoder.enabled=false;encoder.count=0;encoder.selected=0xffffffffu;
    display_clear(painter.display);painter.dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadSlideshow::select(std::uint32_t index){
    if(!services.millis)return HeadWidgetResult::MissingService;
    const auto now=services.millis(services.context);
    if(static_cast<std::int32_t>(index)<0)SLIDE_TRY(assertion(0x306,"index >= 0"));
    if(index>=entries.size()){SLIDE_TRY(assertion(0x307,"index < ARRAY_LENGTH(_infoScreens)"));index=0;}
    if(encoder.selected<entries.size())stop(entries[encoder.selected]);
    encoder.timer.duration=4000;if(encoder.timer.active)head_timer_reset(encoder.timer,services.millis(services.context));
    HeadPresentation output(painter,&services.diagnostics);SLIDE_TRY(output.rectangle(title_rect,0));SLIDE_TRY(output.rectangle(body_rect,15,false));
    if(encoder.count>1){const auto top=title_rect.top+2;const auto left=title_rect.left,right=left+static_cast<std::int32_t>(display_rect_width(title_rect))-4;
        for(std::int32_t i=0;i<4;++i){SLIDE_TRY(output.rectangle({left+i,top+3-i,left+i,top+3+i},15));SLIDE_TRY(output.rectangle({right+i,top+i,right+i,top+6-i},15));}}
    auto& entry=entries[index];const DisplayRect content{body_rect.left+2,body_rect.top+2,body_rect.right-2,body_rect.bottom-2};
    SLIDE_TRY(output.text(DisplayBounds,output.select_font(entry.title.data(),content),entry.title.data()));entry.enabled=true;
    if(entry.type==3){entry.counter=observed.runtime.elapsed_seconds;entry.increment=1;head_timer_start(entry.timer,1000,services.millis(services.context));}
    else if(entry.type==2||entry.type==7){entry.counter=entry.type==2?observed.runtime.total_left_seconds:observed.runtime.instruction_left_seconds;entry.increment=-1;if(entry.counter)head_timer_start(entry.timer,1000,services.millis(services.context));}
    else{entry.counter=0;entry.increment=0;head_timer_start(entry.timer,500,services.millis(services.context));}
    SLIDE_TRY(render(entry,now));encoder.selected=index;painter.dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadSlideshow::tick(std::uint32_t now){
    if(!encoder.enabled||!encoder.count)return HeadWidgetResult::Ok;
    if(!services.millis)return HeadWidgetResult::MissingService;
    if(encoder.count>entries.size())return HeadWidgetResult::InvalidText;
    if(observed_pid!=observed.runtime.pid_active||observed_pump!=observed.still_samples.current.pump_running||observed_network!=(observed.network!=0)){
        for(auto& entry:entries)stop(entry);
        encoder.enabled=true;SLIDE_TRY(populate(argument));SLIDE_TRY(select(0));encoder.timer.active=!static_cycle;
        if(encoder.timer.active){encoder.timer.duration=4000;head_timer_reset(encoder.timer,services.millis(services.context));}else encoder.timer.deadline=0;return HeadWidgetResult::Ok;
    }
    for(std::uint32_t i=0;i<encoder.count;++i){auto& entry=entries[i];if(entry.enabled&&entry.timer.active&&entry.timer.deadline<=now){head_timer_reset(entry.timer,services.millis(services.context));entry.counter+=static_cast<std::uint32_t>(entry.increment);SLIDE_TRY(render(entry,now));}}
    if(encoder.timer.active&&encoder.timer.deadline<=now)SLIDE_TRY(select((encoder.selected+1)%encoder.count));
    return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadSlideshow::button(){
    if(!encoder.enabled)return HeadWidgetResult::Ok;
    if(!services.post)return HeadWidgetResult::MissingService;
    if(!services.post(services.context,command))return HeadWidgetResult::AssertionFailure;
    return reset();
}
HeadWidgetResult HeadSlideshow::draw(std::uint32_t type,std::uint32_t counter){HeadSlideshowEntry entry;entry.type=type;entry.counter=counter;return render(entry,0);}
HeadWidgetResult HeadSlideshow::render(HeadSlideshowEntry& entry,std::uint32_t){
    HeadPresentation output(painter,&services.diagnostics);const DisplayRect rect{body_rect.left+2,body_rect.top+2,body_rect.right-2,body_rect.bottom-2};SLIDE_TRY(output.rectangle(rect,0));
    auto& lines=entry.lines;lines.count=0;const auto& sample=observed.still_samples.current;const auto& ten=observed.still_samples.seconds10;const auto& runtime=observed.runtime;
    const char unit=imperial?'F':'C';const char* pressure_unit=imperial?"psi":"hPa";
    const auto local=[&](UiTextCategory category,std::uint32_t id){return ui_localized_text(category,id,painter.language);};
    const auto add=[&](const char* text){return output.add(lines,"%s",present(text));};
    char buffer[64],first[11],second[11],third[11];const char* arrow1="",*arrow2="";
    switch(entry.type){
    case 0:case 19:SLIDE_TRY(output.log(ConsoleLevel::Error,"HEAD","UiSlideshow.h",0x2d1,"Invalid info screen type"));break;
    case 1:{
        std::string name=observed.session.name.data();std::uint32_t count=0;if(display_utf8_length(name.c_str(),name.size()+1,count)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidText;
        const auto columns=display_rect_width(rect)/6;
        if(columns-1<count){DisplayUtf8 decoder;if(display_utf8_initialize(decoder,name.c_str(),name.size()+1)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidText;for(std::uint32_t i=0;i<columns-2;++i){std::uint32_t point;if(display_utf8_next(decoder,point)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidText;}name.resize(decoder.cursor);name+="\xc2\x84";}
        SLIDE_TRY(output.centered(name.c_str(),rect));break;}
    case 2:case 3:{
        const auto seconds=entry.counter,hours=seconds/3600,minutes=(seconds/60)%60;
        const auto top=display_rect_center_y(rect)-10;std::uint32_t extra=hours>=10?18:hours?9:0;if(hours)extra+=5;
        std::int32_t left=72-static_cast<std::int32_t>((52+extra+3)/2);if(hours>=10)left-=3;
        if(hours){char expanded[12];::snprintf(expanded,sizeof(expanded),"%u",static_cast<unsigned>(hours));char value[3]={expanded[0],expanded[1],0};SLIDE_TRY(glyphs(painter,DisplayFontId::Font17x20,value,left,top));left+=static_cast<std::int32_t>(std::strlen(value))*17;
            const auto* bitmap=ui_bitmap(UiBitmapId::TimeSeparatorLarge);if(!bitmap||display_bitmap(painter.display,{left,top},*bitmap,bitmap->bounds,true)!=DisplayResult::Ok)return HeadWidgetResult::BitmapFailure;left+=5;}
        char value[3];::snprintf(value,sizeof(value),"%02u",static_cast<unsigned>(minutes));SLIDE_TRY(glyphs(painter,DisplayFontId::Font17x20,value,left,top));left+=34;
        const auto* bitmap=ui_bitmap(UiBitmapId::TimeSeparatorSmall);if(!bitmap||display_bitmap(painter.display,{left,top+1},*bitmap,bitmap->bounds,true)!=DisplayResult::Ok)return HeadWidgetResult::BitmapFailure;left+=3;
        ::snprintf(value,sizeof(value),"%02u",static_cast<unsigned>(seconds%60));SLIDE_TRY(glyphs(painter,DisplayFontId::Font9x11,value,left,top+1));break;}
    case 4:case 5:case 6:{
        const auto center=display_rect_center_y(rect);const auto paint_temperature=[&](const char* label,double value,std::int32_t x){std::uint32_t length=0;if(display_utf8_length(label,std::strlen(label)+1,length)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidText;
            const auto y=center-11;auto result=glyphs(painter,DisplayFontId::Font6x8,label,x-static_cast<std::int32_t>(length*6/2),y);if(result!=HeadWidgetResult::Ok)return result;
            ::snprintf(buffer,sizeof(buffer),"%.0f\xc2\xb0%c",temperature(value,imperial),unit);if(display_utf8_length(buffer,sizeof(buffer),length)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidText;
            return glyphs(painter,DisplayFontId::Font9x11,buffer,x-static_cast<std::int32_t>(length*9/2),y+11);};
        const auto wort=observed.heater_sensors.temperatures[1].celsius,target=observed.heater.target_celsius;
        if(target<0.1||!observed.heater.started)SLIDE_TRY(paint_temperature(local(UiTextCategory::TextInfo,1),wort,64));
        else{SLIDE_TRY(paint_temperature(local(UiTextCategory::TextInfo,1),wort,32));SLIDE_TRY(output.rectangle({64,center-13,65,center+12},15));SLIDE_TRY(paint_temperature(local(UiTextCategory::TextInfo,11),target,96));}break;}
    case 7:
        if(*present(runtime.substep_name))SLIDE_TRY(add(runtime.substep_name));
        if(entry.counter&&!(runtime.total_left_seconds-1<=entry.counter&&entry.counter<=runtime.total_left_seconds+1)){
            if(entry.counter<3599999)head_duration_text(buffer,sizeof(buffer),entry.counter,true);else ::snprintf(buffer,sizeof(buffer),"--:--");SLIDE_TRY(add(buffer));}
        SLIDE_TRY(output.title_body(present(runtime.step_name),lines,rect,false));break;
    case 8:{const char* labels[]={"Wort:   ","Hex:    ","Drain:  ","Ambient:"};const unsigned index[]={1,0,2,3};for(unsigned i=0;i<4;++i)SLIDE_TRY(output.add(lines,"%s %5.1f \xc2\xb0%c",labels[i],temperature(observed.heater_sensors.temperatures[index[i]].celsius,imperial),unit));SLIDE_TRY(output.lines(lines,rect));break;}
    case 9:{
        auto altitude=(1.0-std::pow(observed.auxiliary.pressure_pa/static_cast<double>(observed.reference_pressure_pa),0.1903))*44330.0;if(imperial)altitude*=3.28083989501312;
        SLIDE_TRY(output.add(lines,"%s: %5.2f atm",local(UiTextCategory::TextInfo,3),observed.auxiliary.pressure_pa*0.00000986923266716013));
        SLIDE_TRY(output.add(lines,"%s: %5.0f %c  ",local(UiTextCategory::TextInfo,0),altitude,imperial?'f':'m'));
        SLIDE_TRY(output.add(lines,"%s: %5.1f %c  ",local(UiTextCategory::TextInfo,13),temperature(observed.heater_sensors.estimated_boil_celsius-2,imperial),unit));SLIDE_TRY(output.lines(lines,rect));break;}
    case 10:
        SLIDE_TRY(output.add(lines,"%s %6.2f \xc2\xb0%c/min","Wort:",observed.heater_sensors.wort_rate*(imperial?1.8:1),unit));
        SLIDE_TRY(output.add(lines,"%s %6.2f \xc2\xb0%c/min","Hex: ",observed.heater_sensors.hex_rate*(imperial?1.8:1),unit));SLIDE_TRY(output.lines(lines,rect));break;
    case 11:
        SLIDE_TRY(output.add(lines,"T1 %5.1f            ",temperature(sample.temperature[0],imperial)));
        SLIDE_TRY(output.add(lines,"T2 %5.1f    TW %5.1f",temperature(sample.temperature[1],imperial),temperature(observed.heater_sensors.temperatures[1].celsius,imperial)));
        SLIDE_TRY(output.add(lines,"T3 %5.1f    TT %5.1f",temperature(sample.temperature[2],imperial),temperature(observed.heater.target_celsius,imperial)));
        SLIDE_TRY(output.add(lines,"P  %5.1f            ",sample.pressure*(imperial?1:HectopascalsPerPsi)));
        SLIDE_TRY(output.add(lines,"(\xc2\xb0%c and %s) ",unit,pressure_unit));SLIDE_TRY(output.lines(lines,rect));break;
    case 12:
        SLIDE_TRY(trend(output,arrow1,sample.speed[4],1,5));SLIDE_TRY(trend(output,arrow2,ten.speed[2],0.2,0.6));SLIDE_TRY(stability(output,first,sizeof(first),deviation(observed.still_samples.pressure),0.02,0.06,false));
        SLIDE_TRY(output.add(lines," Pressure:    %s",first));SLIDE_TRY(output.add(lines,"  %s %6.1f %s",arrow1,sample.pressure*(imperial?1:HectopascalsPerPsi),pressure_unit));SLIDE_TRY(add(" Temperature:"));
        SLIDE_TRY(output.add(lines,"    %5.1f \xc2\xb0%c",temperature(sample.temperature[2],imperial),unit));SLIDE_TRY(output.add(lines,"  %s %5.1f \xc2\xb0%c/min",arrow2,ten.speed[2]*(imperial?1.8:1),unit));SLIDE_TRY(output.lines(lines,rect,DisplayFontId::Font6x8,DisplayTextAlignment::Left));break;
    case 13:
        SLIDE_TRY(trend(output,arrow1,ten.speed[0],0.2,0.8));SLIDE_TRY(trend(output,arrow2,ten.speed[1],0.2,0.8));SLIDE_TRY(stability(output,first,sizeof(first),deviation(observed.still_samples.t1),0.2,0.8,false));SLIDE_TRY(stability(output,second,sizeof(second),deviation(observed.still_samples.t2),0.2,0.8,false));
        SLIDE_TRY(output.add(lines," T1: %s  %5.1f \xc2\xb0%c",first,temperature(sample.temperature[0],imperial),unit));SLIDE_TRY(output.add(lines,"     %s  %5.1f \xc2\xb0%c/min",arrow1,ten.speed[0]*(imperial?1.8:1),unit));
        SLIDE_TRY(output.add(lines," T2: %s  %5.1f \xc2\xb0%c",second,temperature(sample.temperature[1],imperial),unit));SLIDE_TRY(output.add(lines,"     %s  %5.1f \xc2\xb0%c/min",arrow2,ten.speed[1]*(imperial?1.8:1),unit));
        SLIDE_TRY(output.add(lines," Delta: %5.1f \xc2\xb0%c",(sample.temperature[0]-sample.temperature[1])*(imperial?1.8:1),unit));SLIDE_TRY(output.lines(lines,rect,DisplayFontId::Font6x8,DisplayTextAlignment::Left));break;
    case 14:
        SLIDE_TRY(add(" Heat Loss:"));
        if(runtime.still.reference_date)SLIDE_TRY(output.add(lines," %5.1f \xc2\xb0%c (@%5.1f)",runtime.still.heat_loss*(imperial?1.8:1),unit,runtime.still.reference_wort*(imperial?1.8:1)));
        else SLIDE_TRY(output.add(lines," ---.- \xc2\xb0%c ",unit));
        SLIDE_TRY(add(" Boiling Point:"));
        if(runtime.still.boil_date)SLIDE_TRY(output.add(lines," %5.1f \xc2\xb0%c %6.1f %s",temperature(runtime.still.boil_t3,imperial),unit,runtime.still.boil_scalar_978*(imperial?1:HectopascalsPerPsi),pressure_unit));
        else SLIDE_TRY(output.add(lines," ---.- \xc2\xb0%c  ---.- %s",unit,pressure_unit));
        SLIDE_TRY(output.lines(lines,rect,DisplayFontId::Font6x8,DisplayTextAlignment::Left));break;
    case 15:
        SLIDE_TRY(stability(output,first,sizeof(first),deviation(observed.still_samples.pressure),0.02,0.06,true));SLIDE_TRY(stability(output,second,sizeof(second),deviation(observed.still_samples.t1),0.2,0.8,true));SLIDE_TRY(stability(output,third,sizeof(third),deviation(observed.still_samples.t2),0.2,0.8,true));
        SLIDE_TRY(output.add(lines,"P:  %s",first));SLIDE_TRY(output.add(lines,"T1: %s",second));SLIDE_TRY(output.add(lines,"T2: %s",third));SLIDE_TRY(output.lines(lines,rect));break;
    case 16:
        SLIDE_TRY(add(runtime.t1_drop_watch.triggered?"T1 has dropped!":runtime.t1_drop_watch.enabled?"T1: Active":"T1: Inactive"));
        if(runtime.t1_drop_watch.triggered||runtime.t1_drop_watch.enabled){SLIDE_TRY(output.add(lines,"Current: %5.1f \xc2\xb0%c",temperature(sample.temperature[0],imperial),unit));SLIDE_TRY(output.add(lines,"Max:     %5.1f \xc2\xb0%c",temperature(runtime.t1_drop_watch.maximum,imperial),unit));}
        SLIDE_TRY(output.lines(lines,rect));break;
    case 17:
        SLIDE_TRY(trend(output,arrow1,sample.speed[4],1,5));SLIDE_TRY(trend(output,arrow2,ten.speed[1],0.2,0.6));
        SLIDE_TRY(output.add(lines,"T2: %3.1f\xc2\xb0%c %s",temperature(sample.temperature[1],imperial),unit,arrow2));SLIDE_TRY(output.add(lines,"P:  %6.1f %s  %s",sample.pressure*(imperial?1:HectopascalsPerPsi),pressure_unit,arrow1));
        SLIDE_TRY(output.title_body(local(UiTextCategory::TextInfo,12),lines,rect,false));break;
    case 18:
        SLIDE_TRY(add(local(UiTextCategory::TextCommon,5)));SLIDE_TRY(add(to_string(observed.wifi.id)));SLIDE_TRY(output.title_body(local(UiTextCategory::TextCommon,4),lines,rect,false));break;
    default:break;
    }
    painter.dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadSlideshow::self_test(){
    if(!services.scope||!services.diagnostics.log)return HeadWidgetResult::MissingService;
    if(!services.scope(services.context,true,"UIS","UiSlideshow.h",0x49,"StabilityText"))return HeadWidgetResult::AssertionFailure;
    HeadPresentation output(painter,&services.diagnostics);const double values[]={0.1,0.3,0.5,1.234};
    const char* expected[]={"\xc2\x90  0.100","\xc2\x91  0.300","\xc2\x92  0.500","\xc2\x92  1.234"};
    const char* expressions[]={"CString::areEquals(UTF8_PULSE_SMALL \"  0.100\", text.get())","CString::areEquals(UTF8_PULSE_MEDIUM \"  0.300\", text.get())","CString::areEquals(UTF8_PULSE_LARGE \"  0.500\", text.get())","CString::areEquals(UTF8_PULSE_LARGE \"  1.234\", text.get())"};
    const std::uint32_t assertion_lines[]={0x4b,0x4f,0x53,0x57},log_lines[]={0x4c,0x50,0x54,0x58};
    for(unsigned i=0;i<4;++i){char text[11];SLIDE_TRY(stability(output,text,sizeof(text),values[i],0.2,0.4,true));if(std::strcmp(text,expected[i]))SLIDE_TRY(assertion(assertion_lines[i],expressions[i]));SLIDE_TRY(output.log(ConsoleLevel::Warning,"UIS","UiSlideshow.h",log_lines[i],text));}
    return services.scope(services.context,false,"UIS","UiSlideshow.h",0x49,"StabilityText")?HeadWidgetResult::Ok:HeadWidgetResult::AssertionFailure;
}
#undef SLIDE_TRY
}} // namespace zseries::oem
