#include "zseries/display_console.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zseries { namespace oem {
#define DRAW(x) do { if(!(x)) return false; } while(false)
DisplayConsole::DisplayConsole(DisplayState& d,DisplayTextContext& c,bool& flag) noexcept
    : display(d),text_context(c),dirty(flag) {
    if(!text_context.font)display_text_font(text_context,*display_font(DisplayFontId::Font6x8),0);
}
bool DisplayConsole::flush(bool pause){DRAW(display_flush(display,io)==DisplayResult::Ok);return !pause||wait(100);}
bool DisplayConsole::wait(std::uint32_t ms){if(!delay.milliseconds)return false;delay.milliseconds(delay.context,ms);return true;}
bool DisplayConsole::scope(bool begin,const char* name,std::uint32_t line,const char* tag,const char* file){
    return services.test_scope&&services.test_scope(services.context,begin,tag,file,line,name);
}
bool DisplayConsole::font(DisplayFontId id,std::uint32_t spacing){return display_text_font(text_context,*display_font(id),spacing)==DisplayTextResult::Ok;}
bool DisplayConsole::bounds(const DisplayRect& r){return display_text_bounds(text_context,r)==DisplayTextResult::Ok;}
bool DisplayConsole::rect(const DisplayRect& r,std::uint8_t color,bool filled){return display_rectangle(display,r,color,filled)==DisplayResult::Ok;}
bool DisplayConsole::text(const char* value,std::int32_t row,std::uint32_t align,bool invert,std::int32_t y,std::uint32_t count){
    if(!value)return false;
    if(!*value){if(!diagnostics.assertion||!diagnostics.assertion(diagnostics.context,"Display.h",0x3bf,"text && len > 0 && strlen(text) > 0"))return false;return true;}
    DisplayTextOptions options;options.row=row;options.alignment=static_cast<DisplayTextAlignment>(align);options.invert=invert;options.offset_y=y;options.characters=count;
    return display_text(display,text_context,value,std::strlen(value)+1,options)==DisplayTextResult::Ok;
}
bool DisplayConsole::bitmap(UiBitmapId id,DisplayPoint p,const DisplayRect* r){
    const auto* asset=ui_bitmap(id);return asset&&display_bitmap(display,p,*asset,r?*r:asset->bounds)==DisplayResult::Ok;
}
bool DisplayConsole::information(){
    if(!diagnostics.log)return false;
    for(const char* value:{"Name = SSD1325","Width = 128","Height = 64","SupportsScreenShots = True"})
        DRAW(diagnostics.log(diagnostics.context,ConsoleLevel::Info,"DSP",nullptr,0,value));
    return true;
}
bool DisplayConsole::test_message(const char* value){
    display_clear(display);
    if(!value||!*value){
        DRAW(diagnostics.assertion&&diagnostics.assertion(diagnostics.context,
            "Graphics.h",0x4a,"!CString::isNullOrEmpty(text)"));
    }else{
        // The original TextModifier restores the persistent font, spacing and
        // bounds. Keep its one-line centered 6x8 context local to this draw.
        DisplayTextContext context;
        DRAW(display_text_font(context,*display_font(DisplayFontId::Font6x8),0)==DisplayTextResult::Ok);
        DRAW(display_text_bounds(context,{0,28,127,35})==DisplayTextResult::Ok);
        DisplayTextOptions options;options.alignment=DisplayTextAlignment::Center;
        DRAW(display_text(display,context,value,std::strlen(value)+1,options)==DisplayTextResult::Ok);
        dirty=true;
    }
    return flush();
}
bool DisplayConsole::screenshot(){
    if(!diagnostics.log)return false;
    DRAW(diagnostics.log(diagnostics.context,ConsoleLevel::Info,"SHOT","Display.h",0x213,"BEGIN"));
    static const char digits[]="0123456789abcdef";
    std::array<char,520> line{};std::memcpy(line.data(),"Line = ",7);
    for(std::size_t offset=0;offset<display.pixels.size();offset+=256){
        for(std::size_t i=0;i<256;++i){const auto value=display.pixels[offset+i];line[7+2*i]=digits[value>>4];line[8+2*i]=digits[value&15];}
        line[519]=0;DRAW(diagnostics.log(diagnostics.context,ConsoleLevel::Info,"DSP",nullptr,0,line.data()));
    }
    return diagnostics.log(diagnostics.context,ConsoleLevel::Info,"SHOT","Display.h",0x220,"END");
}
bool DisplayConsole::bitmap_test(UiBitmapId id){
    const auto* asset=ui_bitmap(id);if(!asset)return false;display_clear(display);
    const auto x=static_cast<std::int32_t>(DisplayWidth-asset->width),y=static_cast<std::int32_t>(DisplayHeight-asset->height);
    DRAW(bitmap(id,{x/2,y/2}));DRAW(bitmap(id,{0,0}));DRAW(bitmap(id,{0,y}));DRAW(bitmap(id,{x,0}));DRAW(bitmap(id,{x,y}));return flush();
}
bool DisplayConsole::corners(const DisplayRect& r,UiBitmapId id){
    DRAW(rect(r));const auto* asset=ui_bitmap(id);if(!asset)return false;
    const auto w=static_cast<std::int32_t>(asset->width/2),h=static_cast<std::int32_t>(asset->height/2);
    DisplayRect region{0,0,w-1,h-1};DRAW(bitmap(id,{r.left,r.top},&region));
    display_rect_translate(region,0,h);DRAW(bitmap(id,{r.left,r.bottom-h+1},&region));
    display_rect_translate(region,w,0);DRAW(bitmap(id,{r.right-w+1,r.bottom-h+1},&region));
    display_rect_translate(region,0,-h);return bitmap(id,{r.right-w+1,r.top},&region);
}
bool DisplayConsole::gradient(const DisplayRect& r,std::uint32_t direction,std::int32_t steps,bool white){
    DisplayRect edge=r;
    switch(direction){
    case 0:edge.bottom=edge.top+steps;return display_vertical_gradient(display,edge,false)==DisplayEffectResult::Ok;
    case 1:edge.right=edge.left+steps;return display_horizontal_gradient(display,edge,white,false)==DisplayEffectResult::Ok;
    case 2:edge.left=edge.right-steps;return display_horizontal_gradient(display,edge,white,true)==DisplayEffectResult::Ok;
    case 3:edge.top=edge.bottom-steps;return display_vertical_gradient(display,edge,true)==DisplayEffectResult::Ok;
    case 4:DRAW(gradient(r,0,steps,white));return gradient(r,3,steps,white);
    case 5:DRAW(gradient(r,1,steps,white));return gradient(r,2,steps,white);
    case 6:DRAW(gradient(r,0,steps,white));DRAW(gradient(r,3,steps,white));return gradient(r,5,steps,white);
    default:return true;
    }
}
bool DisplayConsole::alphabet(bool utf8){
    const char* name=utf8?"Display::drawText() UTF8":"Display::drawText() ASCII";const auto line=utf8?0x1c2u:0x1b4u;
    DRAW(scope(true,name,line));display_clear(display);DRAW(font(DisplayFontId::Font6x8));DRAW(bounds(DisplayBounds));
    std::array<char,255> characters{};std::size_t length=0;
    for(unsigned code=utf8?128:32;code<(utf8?255u:128u);++code){if(utf8){characters[length++]=static_cast<char>(0xc0|(code>>6));characters[length++]=static_cast<char>(0x80|(code&63));}else characters[length++]=static_cast<char>(code);}
    DisplayUtf8 cursor;DRAW(display_utf8_initialize(cursor,characters.data(),characters.size())==DisplayTextResult::Ok);
    std::uint32_t remaining=utf8?127:96,row=0;
    while(remaining){const auto count=std::min<std::uint32_t>(remaining,128/6);DRAW(text(characters.data()+cursor.cursor,row++,0,false,0,count));
        for(std::uint32_t i=0;i<count;++i){std::uint32_t code;DRAW(display_utf8_next(cursor,code)==DisplayTextResult::Ok);}remaining-=count;}
    DRAW(flush());return scope(false,name,line);
}
bool DisplayConsole::carets(){
    DRAW(scope(true,"Display::drawCaret()",0x138));display_clear(display);
    for(int origin=0,height=5;origin!=40;origin+=10,--height){
        for(int i=0;i<4;++i)DRAW(display_caret(display,{origin+10*i,origin},height,15,3-i)==DisplayEffectResult::Ok);
        for(int i=0;i<3;++i)DRAW(display_caret(display,{origin,origin+10*(i+1)},height,15,2-i)==DisplayEffectResult::Ok);
    }
    DRAW(flush());return scope(false,"Display::drawCaret()",0x138);
}
bool DisplayConsole::gradients(){
    DRAW(scope(true,"Display::drawGradient()",0x158));display_clear(display);DRAW(bounds(DisplayBounds));DRAW(text("Gradient",0,1));DRAW(rect({10,9,118,9}));
    for(int i=0;i<8;++i){const DisplayRect r{5+13*i,12,15+13*i,22};DRAW(rect(r));DRAW(gradient(r,i/2,i%2?5:9));}
    const DisplayRect rectangles[]={{6,24,25,44},{30,24,50,44},{54,24,74,44}};const std::uint32_t modes[]={5,4,6};
    for(unsigned i=0;i<3;++i){DRAW(rect(rectangles[i]));DRAW(gradient(rectangles[i],modes[i],6));}
    DRAW(text("TEST GRAD",0,0,false,45));DRAW(text("TEST GRAD",0,0,false,54));DRAW(gradient({0,46,54,55},0,6));
    DRAW(flush());return scope(false,"Display::drawGradient()",0x158);
}
bool DisplayConsole::invoke(std::uint32_t callback,const char* argument){
    switch(callback){
    case 0x400d7020:return information();
    case 0x400d1bfc:display_clear(display);return flush();
    case 0x400d1b4c:return display_invert(display,io,true)==DisplayResult::Ok;
    case 0x400d1b5c:return display_invert(display,io,false)==DisplayResult::Ok;
    case 0x400e780c:DRAW(bitmap(UiBitmapId::DiagnosticBitmap64x32,test_point));dirty=true;return true;
    case 0x400d0908:if(!argument)return false;test_point.x=std::atoi(argument);return true;
    case 0x400d0920:if(!argument)return false;test_point.y=std::atoi(argument);return true;
    case 0x400fce34:return screenshot();
    case 0x400e78b4:return bitmap_test(UiBitmapId::DiagnosticChecker16B);
    case 0x400e78c8:return bitmap_test(UiBitmapId::DiagnosticChecker16A);
    case 0x400e78dc:return bitmap_test(UiBitmapId::DiagnosticChecker18);
    case 0x400e78f0:return bitmap_test(UiBitmapId::DiagnosticGradient16);
    case 0x400e7db4:DRAW(text(argument,0,1));return flush();
    case 0x400e8510:return alphabet(false);
    case 0x400e85cc:return alphabet(true);
    case 0x400e8994:DRAW(rect(DisplayBounds,15,false));return flush();
    case 0x400e95b0:return carets();
    case 0x400ea520:return gradients();
    case 0x400eaf50:return test();
    case 0x400d09a4:test_toggle=!test_toggle;return true;
    default:return false;
    }
}
bool DisplayConsole::test(){
    const auto id=UiBitmapId::DiagnosticBitmap64x32;
    DRAW(scope(true,"drawBitmapRegionTest1",0x524));display_clear(display);DRAW(rect(DisplayBounds));
    const DisplayRect regions[]={{32,16,63,31},{32,16,62,31},{32,16,47,31},{32,16,46,31}};
    for(unsigned i=0;i<4;++i){const int x=i<2?0:64,y=(i%2)*32;DRAW(bitmap(id,{x,y},&regions[i]));DRAW(bitmap(id,{x+1,y+16},&regions[i]));}
    DRAW(flush());DRAW(scope(false,"drawBitmapRegionTest1",0x524));DRAW(wait(100));
    DRAW(scope(true,"drawBitmapRegionTest2",0x53c));display_clear(display);
    DisplayRect region{0,0,31,15};DRAW(bitmap(id,{0,0},&region));display_rect_translate(region,0,16);DRAW(bitmap(id,{0,48},&region));
    display_rect_translate(region,32,0);DRAW(bitmap(id,{96,48},&region));display_rect_translate(region,0,-16);DRAW(bitmap(id,{96,0},&region));
    region={16,8,48,24};DRAW(bitmap(id,{48,24},&region));DRAW(flush());DRAW(scope(false,"drawBitmapRegionTest2",0x53c));DRAW(wait(100));
    DRAW(scope(true,"drawBitmapRegionTest3",0x55a));region={0,0,31,15};
    while(region.right<64&&region.bottom<32){display_clear(display);DRAW(bitmap(id,{32,10},&region));DRAW(flush());DRAW(wait(10));display_rect_translate(region,2,1);}
    DRAW(scope(false,"drawBitmapRegionTest3",0x55a));DRAW(wait(100));
    DRAW(scope(true,"drawBitmapTest1",0x569));
    for(int x=-64;x!=144;x+=16)for(int y=-32;y!=72;y+=8){display_clear(display);DRAW(bitmap(id,{x,y}));DRAW(flush());}
    DRAW(scope(false,"drawBitmapTest1",0x569));DRAW(scope(true,"animateTest1",0x574));
    const unsigned motions[][3]={{3,0,1},{3,2,1},{2,0,1},{2,2,1},{0,1,0},{1,1,2}};
    for(const auto& motion:motions)DRAW(display_slide_aligned(display,io,delay,*ui_bitmap(id),motion[0],motion[1],motion[2],1,0)==DisplayEffectResult::Ok);
    DRAW(scope(false,"animateTest1",0x574));DRAW(scope(true,"drawRectTest1",0x583));
    region=DisplayBounds;std::uint8_t color=0;
    while(display_rect_height(region)>10){display_clear(display);DRAW(rect(region,color));color=(color+1)&15;++region.left;++region.top;--region.right;--region.bottom;DRAW(flush());DRAW(wait(10));}
    for(unsigned i=0;i<16;++i){display_clear(display);DRAW(rect({48,16,80,48},i));DRAW(flush());DRAW(wait(10));}
    DRAW(scope(false,"drawRectTest1",0x583));DRAW(wait(100));
    DRAW(scope(true,"drawRectWithCornersTest1",0x5a3));display_clear(display);DRAW(corners(DisplayBounds,UiBitmapId::DiagnosticEllipse10x8));
    DRAW(corners({10,10,118,54},UiBitmapId::DiagnosticEllipse10x8));DRAW(flush());DRAW(scope(false,"drawRectWithCornersTest1",0x5a3));DRAW(wait(100));
    DRAW(scope(true,"drawRectWithCornersTest2",0x5b0));region=DisplayBounds;
    while(display_rect_height(region)>8&&display_rect_width(region)>30){display_clear(display);DRAW(corners(region,UiBitmapId::DiagnosticEllipse30x8));++region.left;++region.top;--region.right;--region.bottom;DRAW(flush());DRAW(wait(10));}
    DRAW(scope(false,"drawRectWithCornersTest2",0x5b0));DRAW(wait(100));
    DRAW(scope(true,"drawTextTest1",0x5bf));display_clear(display);DRAW(text("ABCDEFGabcdefg012"));
    for(int x=1;x<4;++x){DRAW(bounds({x,0,127,63}));DRAW(text("ABCDEFGabcdefg012",x));}
    DRAW(flush());DRAW(scope(false,"drawTextTest1",0x5bf));DRAW(wait(100));
    DRAW(scope(true,"drawTextTest2",0x5d6));display_clear(display);
    for(int i=0;i<2;++i){for(unsigned a=0;a<3;++a)DRAW(text(a==0?"Left":a==1?"Center":"Right",4*i,a,i!=0));
        for(unsigned a=0;a<3;++a)DRAW(text("Very long string that should get truncated",4*i+a+1,a,i!=0));}
    DRAW(flush());DRAW(scope(false,"drawTextTest2",0x5d6));DRAW(wait(100));
    DRAW(scope(true,"drawTextTest3",0x5ec));display_clear(display);region={0,0,100,8};
    for(int step=0;region.bottom<64;++step){DRAW(bounds(region));DRAW(text("Left"));DRAW(text("Center",0,1));DRAW(text("Right",0,2));DRAW(flush());display_rect_translate(region,step,step*2);}
    DRAW(scope(false,"drawTextTest3",0x5ec));DRAW(wait(100));
    DRAW(scope(true,"setFontTest",0x5fd));display_clear(display);DRAW(bounds(DisplayBounds));
    DRAW(font(DisplayFontId::Font6x8));DRAW(text("6x8"));DRAW(text("small",0,2));
    DRAW(font(DisplayFontId::Font9x11));DRAW(text("9x11",1));DRAW(text("headline",1,2));
    DRAW(font(DisplayFontId::Font8x16));DRAW(text("8x16",2));DRAW(text("medium",2,2));DRAW(font(DisplayFontId::Font6x8));
    DRAW(flush());DRAW(scope(false,"setFontTest",0x5fd));DRAW(wait(100));DRAW(alphabet(false));DRAW(wait(100));DRAW(alphabet(true));DRAW(wait(100));DRAW(carets());DRAW(wait(100));return gradients();
}
bool DisplayConsole::text_modifier_test(){
    DRAW(scope(true,"TextModifier",0x655));display_clear(display);const auto saved=text_context;
    DRAW(font(DisplayFontId::Font6x8));DRAW(bounds(DisplayBounds));DRAW(text("m0-6x8"));
    DRAW(font(DisplayFontId::Font8x16));DRAW(text("m1-8x16",1));DRAW(font(DisplayFontId::Font9x11));DRAW(text("m2-9x13",2));
    DRAW(font(DisplayFontId::Font6x8));DRAW(text("m3-6x8",6));DRAW(font(DisplayFontId::Font8x16));DRAW(text("m1-7x14",1,2));
    DRAW(font(DisplayFontId::Font6x8));DRAW(text("m0-6x8",0,2));DRAW(flush());text_context=saved;return scope(false,"TextModifier",0x655);
}
bool DisplayConsole::graphics_title(const char* value,const DisplayRect& r,std::uint32_t vertical){
    const auto saved=text_context;std::uint32_t count=0;DRAW(display_utf8_length(value,std::strlen(value)+1,count)==DisplayTextResult::Ok);
    const auto id=count*9<=display_rect_width(r)?DisplayFontId::Font9x11:DisplayFontId::Font6x8;DRAW(font(id));
    const auto height=text_context.font->glyph_height;const auto offset=vertical==2?display_rect_height(r)-height:vertical==1?(display_rect_height(r)-height)/2:0;
    DRAW(bounds({r.left,r.top+static_cast<std::int32_t>(offset),r.right,r.top+static_cast<std::int32_t>(offset+height)-1}));DRAW(text(value,0,1));dirty=true;text_context=saved;return true;
}
bool DisplayConsole::graphics_lines(const char* const* values,std::uint32_t count,const DisplayRect& r,DisplayFontId id){
    if(!count)return false;
    const auto saved=text_context;const auto* f=display_font(id);const auto height=display_rect_height(r);
    const auto spacing=f->glyph_height*count<=height?(height-f->glyph_height*count)/(count+1):0;
    DRAW(font(id,spacing));const auto total=count*text_context.line_height-spacing;const auto top=r.top+static_cast<std::int32_t>((height-total)/2);
    DRAW(bounds({r.left,top,r.right,top+static_cast<std::int32_t>(total)-1}));
    for(std::uint32_t i=0;i<count;++i)DRAW(text(values[i],i,1));
    dirty=true;text_context=saved;return true;
}
bool DisplayConsole::graphics_duration(const DisplayRect& r,DisplayFontId tag,DisplayFontId number,std::uint32_t seconds){
    const auto saved=text_context;DRAW(rect(r,0));const auto* tf=display_font(tag);const auto* nf=display_font(number);
    const auto top=r.top+static_cast<std::int32_t>((display_rect_height(r)-tf->glyph_height-nf->glyph_height-1)/2);
    char label[16];::snprintf(label,sizeof(label),"%02u:%02u",static_cast<unsigned>(seconds/3600),static_cast<unsigned>((seconds/60)%60));
    const auto width=static_cast<std::int32_t>(std::strlen(label)*nf->glyph_width),left=static_cast<std::int32_t>((display_rect_width(r)-width)/2);
    DisplayRect area{left+2,top,left+width+1,top+static_cast<std::int32_t>(nf->glyph_height)-1},clipped{};
    DRAW(display_rect_intersection(area,r,clipped));DRAW(font(tag,1));DRAW(bounds(clipped));DRAW(text("HR"));
    display_rect_translate(area,nf->glyph_width*3,0);DRAW(display_rect_intersection(area,r,clipped));DRAW(bounds(clipped));DRAW(text("MIN"));
    DRAW(font(number,1));DRAW(bounds({left,top+static_cast<std::int32_t>(tf->glyph_height)+1,left+width-1,top+static_cast<std::int32_t>(tf->glyph_height+nf->glyph_height)}));
    DRAW(text(label));dirty=true;text_context=saved;return true;
}
bool DisplayConsole::graphics_test(){
    display_clear(display);DRAW(scope(true,"Graphics::ctor()",0x139,"GFX","Graphics.h"));DRAW(scope(false,"Graphics::ctor()",0x139,"GFX","Graphics.h"));
    DRAW(scope(true,"Graphics::drawTitle()",0x13d,"GFX","Graphics.h"));
    for(unsigned i=0;i<3;++i){DRAW(graphics_title("Short Title",{0,0,127,31},i));DRAW(graphics_title("~ Very Long Title ~",{0,32,127,63},i));if(i<2)DRAW(flush(true));}
    DRAW(scope(false,"Graphics::drawTitle()",0x13d,"GFX","Graphics.h"));DRAW(scope(true,"Graphics::drawTextLines()",0x157,"GFX","Graphics.h"));
    const DisplayFontId fonts[]={DisplayFontId::Font6x8,DisplayFontId::Font9x11,DisplayFontId::Font17x20};const unsigned counts[]={7,5,3};
    std::array<std::array<char,25>,7> lines{};std::array<const char*,7> pointers{};
    for(unsigned f=0;f<3;++f)for(unsigned n=0;n<counts[f];++n){::snprintf(lines[n].data(),lines[n].size(),"Line %u",n);pointers[n]=lines[n].data();display_clear(display);DRAW(graphics_lines(pointers.data(),n+1,DisplayBounds,fonts[f]));DRAW(flush(true));}
    DRAW(scope(false,"Graphics::drawTextLines()",0x157,"GFX","Graphics.h"));DRAW(scope(true,"Graphics::draw()",0x15f,"GFX","Graphics.h"));
    const char* body[]={"Line 1","Line 2","Line 3"};
    for(unsigned n=0;n<4;++n){const char* name=n%2?"~ Very Long Title ~":"Short Title";display_clear(display);DRAW(graphics_title(name,{0,8,127,55},0));
        const DisplayRect r{0,n%2?17:20,127,55};if(n>=2)DRAW(rect(r,15,false));DRAW(graphics_lines(body,3,r,DisplayFontId::Font6x8));DRAW(flush(true));}
    DRAW(scope(false,"Graphics::draw()",0x15f,"GFX","Graphics.h"));DRAW(scope(true,"Graphics::drawDuration()",0x17f,"GFX","Graphics.h"));
    display_clear(display);DRAW(graphics_duration(DisplayBounds,DisplayFontId::Font6x8,DisplayFontId::Font9x11,0x9ff9));DRAW(flush(true));
    DRAW(scope(false,"Graphics::drawDuration()",0x17f,"GFX","Graphics.h"));DRAW(scope(true,"Graphics::drawProgressBar()",0x186,"GFX","Graphics.h"));
    display_clear(display);for(unsigned progress=0;progress<16;++progress){DRAW(rect({0,22,127,42},0));for(unsigned i=0;i<15;++i)DRAW(rect({static_cast<int>(8*i),22,static_cast<int>(8*i+5),42},15,i<progress));DRAW(flush(true));}
    return scope(false,"Graphics::drawProgressBar()",0x186,"GFX","Graphics.h");
}
#undef DRAW
}} // namespace zseries::oem
