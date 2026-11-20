#include "zseries/display.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace {
struct Event { std::uint32_t kind,a,b; };
struct Capture { std::vector<Event> events; std::uint8_t* transmitted; };
void emit(void* context,std::uint32_t kind,std::uint32_t a=0,std::uint32_t b=0){
    static_cast<Capture*>(context)->events.push_back(Event{kind,a,b});
}
DisplayRect rect(const std::uint32_t* values){
    DisplayRect result;std::memcpy(&result,values,sizeof result);return result;
}
}
extern "C" int display_test(unsigned operation,const std::uint32_t* arguments,
    std::uint8_t* pixels,std::uint8_t* transfer,std::uint8_t* transmitted,
    std::uint32_t* result,Event* events,std::size_t capacity){
    DisplayState state;std::copy(pixels,pixels+4096,state.pixels.begin());std::copy(transfer,transfer+4096,state.transfer.begin());
    state.chip_select=arguments[0];state.data_command=arguments[1];state.reset=arguments[2];
    Capture capture{{},transmitted};DisplayIo io;io.context=&capture;
    io.pin_mode=[](void* p,std::uint8_t pin,std::uint8_t value){emit(p,0,pin,value);};
    io.digital_write=[](void* p,std::uint8_t pin,std::uint8_t value){emit(p,1,pin,value);};
    io.spi_byte=[](void* p,std::uint8_t value){emit(p,2,value);};
    io.spi_bytes=[](void* p,const std::uint8_t* data,std::size_t size){
        emit(p,3,size);if(size<=4096)std::copy(data,data+size,static_cast<Capture*>(p)->transmitted);
    };
    DisplaySetupServices services;services.context=&capture;
    services.report_information=[](void* p){emit(p,4);};services.register_commands=[](void* p){emit(p,5);};
    DisplayResult status=DisplayResult::Ok;
    switch(operation){
        case 0:display_clear(state);break;
        case 1:status=display_command(state,io,arguments[3]);break;
        case 2:status=display_flush(state,io);break;
        case 3:status=display_clear_and_flush(state,io);break;
        case 4:status=display_invert(state,io,arguments[3]!=0);break;
        case 5:status=display_setup(state,io,services);break;
        case 6:status=display_rectangle(state,rect(arguments+5),arguments[3],arguments[4]!=0);break;
        case 7:{
            const std::uint32_t width=arguments[15],height=arguments[16],stride=arguments[17];
            if(stride*height>65536)return -2;
            std::vector<std::uint8_t> data(stride*height);
            for(std::size_t i=0;i<data.size();++i)data[i]=static_cast<std::uint8_t>((i*37+arguments[20]+11)^(i>>3));
            DisplayBitmap bitmap{width,height,stride,DisplayRect{0,0,static_cast<std::int32_t>(width-1),static_cast<std::int32_t>(height-1)},data.data(),data.size()};
            DisplayPoint point;std::memcpy(&point,arguments+18,sizeof point);
            status=display_bitmap(state,point,bitmap,rect(arguments+5),arguments[3]!=0);break;
        }
        case 8:{
            DisplayRect a=rect(arguments+5),b=rect(arguments+9),intersection;
            result[2]=display_rect_width(a);result[3]=display_rect_height(a);result[4]=static_cast<std::uint32_t>(display_rect_center_y(a));
            result[5]=display_rect_valid(a);result[6]=display_rect_intersects(a,b);result[7]=display_rect_contains(a,b);
            result[8]=display_rect_intersection(a,b,intersection);
            if(result[8])std::memcpy(result+9,&intersection,sizeof intersection);
            std::int32_t delta[2];std::memcpy(delta,arguments+13,sizeof delta);display_rect_translate(a,delta[0],delta[1]);
            std::memcpy(result+13,&a,sizeof a);break;
        }
        default:return -3;
    }
    result[0]=static_cast<std::uint32_t>(status);result[1]=capture.events.size();
    if(capture.events.size()>capacity)return -4;
    std::copy(capture.events.begin(),capture.events.end(),events);
    std::copy(state.pixels.begin(),state.pixels.end(),pixels);std::copy(state.transfer.begin(),state.transfer.end(),transfer);
    return 0;
}
