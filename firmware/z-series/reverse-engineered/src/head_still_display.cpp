#include "zseries/head_still_display.hpp"
#include "zseries/head_presentation.hpp"
namespace zseries { namespace oem {
HeadWidgetResult HeadStillDisplay::draw(const DisplayRect& rect,bool imperial){
    HeadPresentation output(painter);HeadTextLines lines;
    const auto id=observed.runtime.still.id;
    const auto* title=ui_localized_text(UiTextCategory::RuntimeStillStateId,id,painter.language);
    if(id==7){const auto value=observed.still_samples.current.temperature[1];auto result=output.add(lines,"%s %5.1f \xc2\xb0%c","T2",imperial?value*1.8+32.0:value,imperial?'F':'C');if(result!=HeadWidgetResult::Ok)return result;
        result=output.add(lines,"%s",ui_localized_text(UiTextCategory::TextCommon,14,painter.language));if(result!=HeadWidgetResult::Ok)return result;
    }else if(id==8){auto result=output.add(lines,"Please remove the");if(result!=HeadWidgetResult::Ok)return result;result=output.add(lines,"pressure plug.");if(result!=HeadWidgetResult::Ok)return result;}
    return output.title_body(title,lines,rect,true);
}
}} // namespace zseries::oem
