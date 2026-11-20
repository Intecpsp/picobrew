#include "zseries/display_console.hpp"
#include <cassert>
#include <cstring>
#include <vector>
using namespace zseries::oem;
struct DisplayConsoleCapture {
    std::vector<std::uint8_t> events;
    std::uint32_t assertions=7;
    void byte(unsigned v){events.push_back(static_cast<std::uint8_t>(v));}
    void word(std::uint32_t v){for(unsigned i=0;i<4;++i)byte(v>>(i*8));}
    void text(const char* value){if(value)while(*value)byte(*value++);byte(0);}
};
extern "C" unsigned display_console_case(std::uint32_t* words,const char* argument,std::uint8_t* pixels,
    std::uint8_t* transfer,std::uint8_t* output,unsigned capacity){
    DisplayConsoleCapture c;DisplayState d;DisplayTextContext context;bool dirty=words[4]!=0;
    for(unsigned i=0;i<4096;++i){d.pixels[i]=static_cast<std::uint8_t>((i*73+17)^(i>>3));d.transfer[i]=static_cast<std::uint8_t>((i*73+64)^(i>>3));}
    display_text_font(context,*display_font(static_cast<DisplayFontId>(words[5])),words[6]);
    DisplayConsole console(d,context,dirty);console.test_point={static_cast<std::int32_t>(words[1]),static_cast<std::int32_t>(words[2])};console.test_toggle=words[3]!=0;
    console.io.context=&c;
    console.io.digital_write=[](void* v,std::uint8_t pin,std::uint8_t value){auto& c=*static_cast<DisplayConsoleCapture*>(v);c.byte(1);c.byte(pin);c.byte(value);};
    console.io.spi_byte=[](void* v,std::uint8_t value){auto& c=*static_cast<DisplayConsoleCapture*>(v);c.byte(2);c.byte(value);};
    console.io.spi_bytes=[](void* v,const std::uint8_t* bytes,std::size_t size){auto& c=*static_cast<DisplayConsoleCapture*>(v);c.byte(3);c.word(size);c.events.insert(c.events.end(),bytes,bytes+size);};
    console.delay.context=&c;console.delay.milliseconds=[](void* v,std::uint32_t time){auto& c=*static_cast<DisplayConsoleCapture*>(v);c.byte(4);c.word(time);};
    console.services.context=&c;console.services.test_scope=[](void* v,bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){auto& c=*static_cast<DisplayConsoleCapture*>(v);c.byte(5);c.byte(begin);c.text(tag);c.text(file);c.word(line);c.text(name);return true;};
    console.diagnostics.context=&c;
    console.diagnostics.log=[](void* v,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* message){auto& c=*static_cast<DisplayConsoleCapture*>(v);c.byte(6);c.byte(static_cast<unsigned>(level));c.text(tag);c.text(file);c.word(line);c.text(message);return true;};
    console.diagnostics.assertion=[](void* v,const char* file,std::uint32_t line,const char* expression){auto& c=*static_cast<DisplayConsoleCapture*>(v);c.byte(7);c.text(file);c.word(line);c.text(expression);c.word(c.assertions++);c.byte(4);c.word(200);return true;};
    bool result=false;
    if(words[0]==0x400e7fbc)result=console.text_modifier_test();
    else if(words[0]==0x400f1318)result=console.graphics_test();
    else if(words[0]==0x400ee634)result=console.test_message(argument);
    else result=console.invoke(words[0],argument);
    words[1]=static_cast<std::uint32_t>(console.test_point.x);words[2]=static_cast<std::uint32_t>(console.test_point.y);words[3]=console.test_toggle;words[4]=dirty;
    for(unsigned i=0;i<5;++i)if(context.font==display_font(static_cast<DisplayFontId>(i)))words[5]=i;
    words[6]=context.line_spacing;words[7]=c.assertions;words[8]=result?0:1;
    words[9]=context.bounds.left;words[10]=context.bounds.top;words[11]=context.bounds.right;words[12]=context.bounds.bottom;
    words[13]=context.width;words[14]=context.height;words[15]=context.cell_width;words[16]=context.line_height;words[17]=context.rows;words[18]=context.columns;
    std::memcpy(pixels,d.pixels.data(),4096);std::memcpy(transfer,d.transfer.data(),4096);
    assert(c.events.size()<=capacity);std::memcpy(output,c.events.data(),c.events.size());return c.events.size();
}
