#include "picostill/http_server.hpp"
#include "picostill/web_assets.hpp"
#include <cstring>
#include <stdio.h>

namespace picostill { namespace oem {
namespace {
void send(HttpStream& stream,const char* text){if(text)stream.write(text,std::strlen(text));}
void line(HttpStream& stream,const char* text){send(stream,text);send(stream,"\r\n");}
void end_headers(HttpStream& stream){send(stream,"Connection: close\r\n\r\n");}
void ok(HttpStream& stream,bool json=false){send(stream,"HTTP/1.0 200 OK\r\n");if(json)send(stream,"Content-Type: application/json\r\n");end_headers(stream);}
void framed(HttpStream& stream,const char* text){send(stream,"#");send(stream,text);send(stream,"#");}
void ip(HttpStream& stream,std::uint32_t value){char text[16];snprintf(text,sizeof(text),"%u.%u.%u.%u",unsigned(value&255),unsigned((value>>8)&255),unsigned((value>>16)&255),unsigned(value>>24));send(stream,text);}
void key(HttpStream& stream,const char* name){send(stream,"\"");send(stream,name);send(stream,"\":");}
void field(HttpStream& stream,const char* name,const char* value){key(stream,name);send(stream,"\"");send(stream,value);send(stream,"\"");}
void boolean_field(HttpStream& stream,const char* name,bool value){field(stream,name,value?"1":"0");}
void decimal_field(HttpStream& stream,const char* name,float value){key(stream,name);send(stream,"\"");stream.decimal(value,2);send(stream,"\"");}
void javascript(HttpStream& stream,const char* name,const char* value,bool quoted){send(stream,name);send(stream," : ");if(quoted)send(stream,"\"");send(stream,value);if(quoted)send(stream,"\"");send(stream,",\r\n");}
const char page_head[]=" <!DOCTYPE html>\n <html lang=\"en\">\n <head>\n    <meta http-equiv=\"Content-Type\" content=\"text/html; charset=windows-1252\">\n    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no\">\n    <title>PicoStill</title>\n";
}
void HttpServer::write_state(HttpStream& stream){
    send(stream,"{");field(stream,"version",device_.version());send(stream,",");
    if(device_.has_station_ip()){key(stream,"ipRouter");send(stream,"\"");ip(stream,device_.station_ip());send(stream,"\"");}
    else field(stream,"ipRouter","null");
    send(stream,",");field(stream,"networkSsid",device_.ssid());send(stream,",");field(stream,"programName",device_.program_name());
    send(stream,",");boolean_field(stream,"isProgramRunning",device_.is_program_running());send(stream,",");boolean_field(stream,"isWifiConnected",device_.wifi_connected());
    send(stream,",");boolean_field(stream,"sensors",device_.sensors_enabled());send(stream,",");boolean_field(stream,"fan",device_.fan_enabled());send(stream,"}");
}
void HttpServer::write_browser_state(HttpStream& stream){
    send(stream,"var picostill = {\r\n");javascript(stream,"version",device_.version(),true);
    if(device_.has_station_ip()){send(stream,"ipRouter : \"");ip(stream,device_.station_ip());send(stream,"\",\r\n");}
    else javascript(stream,"ipRouter","null",false);
    if(device_.has_ssid())javascript(stream,"networkSsid",device_.ssid(),true);else javascript(stream,"networkSsid","null",false);
    javascript(stream,"programName",device_.program_name(),true);javascript(stream,"isProgramRunning",device_.is_program_running()?"true":"false",false);
    javascript(stream,"isWifiConnected",device_.wifi_connected()?"true":"false",false);javascript(stream,"sensors",device_.sensors_enabled()?"true":"false",false);
    javascript(stream,"fan",device_.fan_enabled()?"true":"false",false);send(stream,"};\r\n");
}
void HttpServer::write_scan_options(HttpStream& stream){
    const int count=device_.scan_count();send(stream,"const ssid_options = [");
    for(int index=0;index<count;++index){send(stream,"\"");send(stream,device_.scan_ssid(static_cast<unsigned>(index)&255));if(index<count-1)line(stream,"\",");else send(stream,"\"");}
    send(stream,"];\r\n");
}
void HttpServer::write_page(HttpStream& stream,bool wifi){
    runtime_.variable("SRV",wifi?"SEND SSID ROOT":"SEND CONTROL ROOT","True");line(stream,page_head);line(stream,"<style>\n");
    line(stream,wifi?web::wifi_css:web::data_css);line(stream,"</style>\n");line(stream,"<script>\n");write_browser_state(stream);
    if(wifi)write_scan_options(stream);
    line(stream,wifi?web::wifi_js:web::data_js);line(stream,"</script>\n");line(stream,"</head>\n");line(stream,"<body>\r\n<section class=\"view-section\">\r\n");
    if(wifi && device_.scan_count()<=0)line(stream,"<p>Could not find any networks. Please make sure your home router is in range and turned on.</p>");
    else line(stream,wifi?web::wifi_fragment:web::data_fragment);
    line(stream,"</section>\r\n</body>\r\n");line(stream,"</html>");stream.flush();runtime_.cooperate();
}
void HttpServer::write_factory_result(HttpStream& stream,const FactoryResult& result){
    send(stream,"{");field(stream,"Error",to_string(result.test.error));send(stream,",\r\n");key(stream,"Duration");send(stream,"\"");stream.unsigned_number(static_cast<std::uint32_t>(result.test.runtime));send(stream,"\",\r\n");
    const char* names[]={"T1","T2","T3","T4","P","T1ReadSuccessRate","T2ReadSuccessRate","T3ReadSuccessRate","T4ReadSuccessRate","PReadSuccessRate"};
    for(unsigned i=0;i<10;++i){decimal_field(stream,names[i],i<5?result.test.values[i]:result.test.success_rates[i-5]);send(stream,",\r\n");}
    key(stream,"PressurePoints");send(stream," [");const auto count=result.pressure_count>200?200:result.pressure_count;
    for(unsigned i=0;i<count;++i){stream.decimal(result.pressure[i],2);if(i+1<count)send(stream,", ");}send(stream,"]\r\n}\r\n");
}
bool HttpServer::dispatch(HttpStream& stream){
    const char* uri=request.uri.data();
    if(string_equal(uri,"/")||string_equal(uri,"/home")||string_equal(uri,"/wifi")){ok(stream);write_page(stream,true);}
    else if(string_equal(uri,"/data-page")){ok(stream);write_page(stream,false);}
    else if(string_equal(uri,"/ping")){ok(stream);send(stream,"#T#");}
    else if(string_starts_with(uri,"/configure")){
        parse_wifi_form(request,form,diagnostics_);ok(stream);
        if(!request.error){send(stream,"#T#");stream.flush();device_.configure(form.ssid.data(),form.password.data());}
        else {send(stream,"#F#");runtime_.message(request.error);}
    } else if(string_starts_with(uri,"/stillip")){
        ok(stream);const auto address=device_.station_ip();send(stream,"#");ip(stream,address);send(stream,"#");stream.flush();
        char text[16];snprintf(text,sizeof(text),"%u.%u.%u.%u",unsigned(address&255),unsigned((address>>8)&255),unsigned((address>>16)&255),unsigned(address>>24));runtime_.variable("SERV","LocalIP",text);
    } else if(string_starts_with(uri,"/start")){
        if(device_.is_program_running()){runtime_.message("/start cancelled: Program is already running");ok(stream);send(stream,"#F#");return false;}
        ok(stream);send(stream,"#T#");runtime_.variable("SRV","START","True");device_.start_program(ProgramId::PicoManual);
    } else if(string_starts_with(uri,"/end")){
        if(!device_.is_program_running()){runtime_.message("/end cancelled: No Program running");ok(stream);send(stream,"#F#");return false;}
        ok(stream);send(stream,"#T#");runtime_.variable("SRV","END","True");device_.stop_program();
    } else if(string_starts_with(uri,"/needsupdate")){ok(stream);send(stream,device_.needs_update()?"#T#":"#F#");}
    else if(string_starts_with(uri,"/connecttolan")){ok(stream);send(stream,"#T#");device_.connect_to_lan();}
    else if(string_starts_with(uri,"/data")){ok(stream);framed(stream,device_.sensor_csv());}
    else if(string_starts_with(uri,"/version")){ok(stream);framed(stream,device_.version());}
    else if(string_starts_with(uri,"/hardwareError")){ok(stream);framed(stream,device_.hardware_error());}
    else if(string_equal(uri,"/factoryResult")){ok(stream,true);FactoryResult result;device_.factory_result(result);write_factory_result(stream,result);}
    else if(string_equal(uri,"/fanon")||string_equal(uri,"/fanoff")){ok(stream);device_.set_fan(string_equal(uri,"/fanon"));send(stream,"#T#");}
    else if(string_equal(uri,"/pumpon")||string_equal(uri,"/pumpoff")){ok(stream);device_.set_pump(string_equal(uri,"/pumpon"));send(stream,"#T#");}
    else if(string_equal(uri,"/fantoggle")){ok(stream);device_.set_fan(!device_.fan_enabled());send(stream,"#T#");}
    else if(string_equal(uri,"/fanstate")){ok(stream);send(stream,device_.fan_enabled()?"#T#":"#F#");}
    else if(string_equal(uri,"/sensors")){ok(stream);device_.set_sensors(true);send(stream,"#T#");}
    else if(string_equal(uri,"/clean")){diagnostics_.check(!device_.is_program_running(),"!_picoStill.IsProgramRunning()");ok(stream);send(stream,"#T#");runtime_.variable("SRV","START CLEAN","True");device_.start_program(ProgramId::RequestedCleaning);}
    else if(string_equal(uri,"/runningprogram")){ok(stream);framed(stream,device_.program_name());}
    else if(string_equal(uri,"/factoryreset")){ok(stream);send(stream,"#T#");device_.factory_reset();}
    else if(string_equal(uri,"/stillstate")){ok(stream,true);write_state(stream);}
    else {runtime_.variable("SERV","URI",uri);send(stream,"HTTP/1.0 404 NOT FOUND\r\n");end_headers(stream);}
    return true;
}
void HttpServer::poll(){
    HttpStream* stream=listener_.accept();if(!stream)return;
    if(stream->connected()){
        parse_http(*stream,request,runtime_,timeout);
        if(request.error)runtime_.message(request.error);
        else if(dispatch(*stream))stream->flush();
    }
    listener_.release(*stream);
}
}} // namespace picostill::oem
