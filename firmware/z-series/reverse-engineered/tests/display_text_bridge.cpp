#include "zseries/display_text.hpp"
#include <algorithm>
#include <cstring>
using namespace zseries::oem;
extern "C" int display_text_test(unsigned operation,const std::uint32_t* args,const char* text,std::size_t capacity,
    std::uint8_t* pixels,std::uint32_t* result,std::uint32_t* codepoints){
    const DisplayFont* font=display_font(static_cast<DisplayFontId>(args[0]));if(!font)return -1;
    DisplayState state;std::copy(pixels,pixels+4096,state.pixels.begin());
    DisplayTextContext context;DisplayTextResult status=display_text_font(context,*font,args[1]);
    DisplayRect bounds;std::memcpy(&bounds,args+2,sizeof bounds);
    if(status==DisplayTextResult::Ok)status=display_text_bounds(context,bounds);
    if(status!=DisplayTextResult::Ok)return -2;
    if(operation==0){
        std::uint32_t length=0;status=display_utf8_length(text,capacity,length);result[2]=length;
        DisplayUtf8 decoder;if(status==DisplayTextResult::Ok)status=display_utf8_initialize(decoder,text,capacity);
        while(status==DisplayTextResult::Ok && decoder.cursor<decoder.length){
            std::uint32_t codepoint;status=display_utf8_next(decoder,codepoint);
            if(status==DisplayTextResult::Ok){if(result[1]>=512)return -3;codepoints[result[1]++]=codepoint;}
        }
        result[3]=decoder.cursor;result[4]=decoder.length;result[5]=decoder.characters;
    }else if(operation==1){
        DisplayPoint point;std::memcpy(&point,args+12,sizeof point);
        status=display_glyphs(state,*font,text,capacity,args[14],point,args[15]!=0);
    }else if(operation==2){
        DisplayTextOptions options;options.characters=args[6];std::memcpy(&options.row,args+7,4);
        options.invert=args[8]!=0;options.alignment=static_cast<DisplayTextAlignment>(args[9]);
        std::memcpy(&options.offset_x,args+10,4);std::memcpy(&options.offset_y,args+11,4);
        status=display_text(state,context,text,capacity,options);
    }else if(operation!=3)return -4;
    result[0]=static_cast<std::uint32_t>(status);result[6]=font->glyph_width;result[7]=font->glyph_height;result[8]=context.line_spacing;
    std::memcpy(result+9,&context.bounds,sizeof context.bounds);
    result[13]=context.width;result[14]=context.height;result[15]=context.cell_width;result[16]=context.line_height;result[17]=context.rows;result[18]=context.columns;
    std::copy(state.pixels.begin(),state.pixels.end(),pixels);return 0;
}
