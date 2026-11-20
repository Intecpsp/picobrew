#include "picoferm/http_server.hpp"
#include <cstdio>
#include <cstring>
#if defined(ESP8266)
#include <pgmspace.h>
#define FERM_HTTP_FLASH PROGMEM
#define FERM_HTTP_LITERAL(value) PSTR(value)
#else
#define FERM_HTTP_FLASH
#define FERM_HTTP_LITERAL(value) value
#endif
#define WRITE(stream,value) (stream).write_flash(FERM_HTTP_LITERAL(value),sizeof(value)-1)
#define LINE(stream,value) do{WRITE(stream,value);WRITE(stream,"\r\n");}while(false)

namespace picoferm { namespace oem {
namespace {
#include "http_assets.inc"
void text(HttpStream& stream,const char* value){if(value)stream.write(value,std::strlen(value));}
void paragraph(HttpStream& stream,const char* value){WRITE(stream,"<p>");text(stream,value);LINE(stream,"</p>");}
}
void HttpServer::page_header(HttpStream& stream){
    ports_.text("SRV","RESPONSE","HTTP_OK");
    WRITE(stream,"HTTP/1.0 200 OK\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n");
    stream.write_flash(page_prefix,sizeof(page_prefix)-1);text(stream,data_.uid.data());
    LINE(stream,"<br>Version: ");text(stream,data_.version.data());LINE(stream,"\n    </small>\n");
}
void HttpServer::page_footer(HttpStream& stream){WRITE(stream,"  </section>\n</body>\n</html>\n");stream.flush();ports_.yield();}
void HttpServer::bad_request(HttpStream& stream){WRITE(stream,"HTTP/1.0 400 Bad Request\r\n\r\n400 Bad Request");}
void HttpServer::setup_page(HttpStream& stream){
    page_header(stream);LINE(stream,"<h3>Wi-Fi Setup</h3>");
    const auto status=device_.calibration();const auto count=static_cast<std::int8_t>(device_.scan_count());
    if(count>0){
        LINE(stream,"<p>");
        LINE(stream,"  Now that you are connected to your PicoFerm, enter your home Wi-Fi network name (SSID)");
        LINE(stream,"  and password in the fields below to allow your PicoFerm to connect to your network.");
        LINE(stream,"</p>");LINE(stream,"<form method=\"post\" action=\"setSsid\">");LINE(stream,"<table><tbody>");
        LINE(stream,"  <tr>");LINE(stream,"    <td><label>SSID</label></td>");LINE(stream,"    <td><select id=\"ssid\" name=\"ssid\">");
        for(int index=0;index<count;++index){WRITE(stream,"        <option>");text(stream,device_.scan_ssid(static_cast<unsigned>(index)));LINE(stream,"</option>");}
        LINE(stream,"    </select></td>");LINE(stream,"  </tr>");LINE(stream,"  <tr>");
        LINE(stream,"    <td><label>Password</label></td>");
        LINE(stream,"    <td><input type=\"password\" id=\"pass\" name=\"pass\" placeholder=\"Password\"></td>");
        LINE(stream,"  </tr>");LINE(stream,"  <tr>");
        LINE(stream,"    <td colspan=\"2\"><label><input type=\"checkbox\" id=\"enable-show\" click=\"onclick\">Show Password</label></td>");
        LINE(stream,"  </tr>");LINE(stream,"</tbody></table>");
        LINE(stream,"<button class=\"btn text\" id=\"submit\" type=\"submit\">Submit</button>");LINE(stream,"</form>");
        LINE(stream,"<h3>Other Setup Options</h3>");
        if(status.can_calibrate||(status.message&&*status.message)){
            LINE(stream,"<em>");if(status.message&&*status.message)paragraph(stream,status.message);
            if(status.can_calibrate)LINE(stream,"<p>Click the 'Recalibrate Pressure' link below and follow the on-screen instructions.</p>");
            LINE(stream,"</em>");
        }
        LINE(stream,"<ul>");LINE(stream,"  <li><a href=\"/details\">Details</a></li>");
        LINE(stream,"  <li><a href=\"/factoryReset\">Factory Reset</a></li>");
        LINE(stream,"  <li><a href=\"/calibrate\">Recalibrate Pressure</a></li>");LINE(stream,"</ul>");
    }else LINE(stream,"<p>Could not find any network. Please make sure your home router is in range and turned on.</p>");
    page_footer(stream);
}
void HttpServer::reset_page(HttpStream& stream){
    page_header(stream);LINE(stream,"<h2>Factory Reset</h2>");LINE(stream,"<em>");LINE(stream,"  <p>WARNING!</p>");
    LINE(stream,"  <p>Do not factory reset unless you have WiFi credentials in hand. You will need to setup your PicoFerm again.</p>");
    LINE(stream,"  <p>This will stop tracking your current fermentation session if any.</p>");LINE(stream,"</em>");
    LINE(stream,"<p>To factory reset your PicoFerm type 'reset' in the box below and then click the reset button.</p>");
    LINE(stream,"  <form autocomplete=\"off\" method=\"post\" action=\"factoryResetComplete\">");
    LINE(stream,"    <input class=\"text\" id=\"trigger\" name=\"trigger\">");
    LINE(stream,"    <button class=\"btn text\" type=\"submit\">Reset</button>");
    LINE(stream,"  </form>");LINE(stream,"  <a href=\"/\">Back</a>");page_footer(stream);
}
void HttpServer::calibration_page(HttpStream& stream){
    page_header(stream);LINE(stream,"<h2>Calibrate Pressure</h2>");LINE(stream,"<em>");LINE(stream,"  <p>WARNING!</p>");
    LINE(stream,"  <p>Remove the PicoFerm from any Keg or Keg adapter.</p>");
    LINE(stream,"  <p>Remove the GRAY PicoFerm Fermentation Seal.</p>");LINE(stream,"</em>");
    LINE(stream,"<p>To calibrate your PicoFerm type 'calibrate' in the box below and then click the calibrate button..</p>");
    LINE(stream,"  <form autocomplete=\"off\" method=\"post\" action=\"calibrateComplete\">");
    LINE(stream,"    <input class=\"text\" id=\"trigger\" name=\"trigger\">");
    LINE(stream,"    <button class=\"btn text\" type=\"submit\">Calibrate</button>");
    LINE(stream,"  </form>");LINE(stream,"  <a href=\"/\">Back</a>");page_footer(stream);
}
void HttpServer::details_page(HttpStream& stream){
    page_header(stream);LINE(stream,"<h2>Details</h2>");WRITE(stream,"<p>");
    char count[16];::snprintf(count,sizeof(count),"%u",stored_.bytes[PersistentData::log_count_position]);text(stream,count);
    LINE(stream," pending Data Point</p>");
    if(stored_.bytes[PersistentData::log_count_position]){
        LINE(stream,"<table style=\"color: #ffffff;\"><tbody>");LINE(stream,"  <tr><td></td><td>Temperature</td><td>Pressure</td></tr>");
        for(LogIterator iterator(stored_);!iterator.done();iterator.advance()){
            Measurement value;
            if(!stored_.measurement(iterator.index,value)){diagnostic_assertion(ports_);break;}
            char temperature[64],pressure[64],row[192];
            formatter_.decimal(value.temperature,temperature,sizeof(temperature));formatter_.decimal(value.pressure,pressure,sizeof(pressure));
            ::snprintf(row,sizeof(row),"<tr><td>#%u</td><td>%s</td><td>%s</td></tr>",iterator.index,temperature,pressure);
            text(stream,row);WRITE(stream,"\r\n");
        }
        LINE(stream,"</tbody></table>");
    }
    LINE(stream,"  <a href=\"/\">Back</a>");page_footer(stream);
}
void HttpServer::configure_page(HttpStream& stream,HttpRequest& incoming){
    parse_wifi_form(incoming,form,ports_);
    if(incoming.error){diagnostic_warning(ports_);ports_.text("SRV","RESPONSE","HTTP_BAD_REQUEST");bad_request(stream);return;}
    char token[16]={0};device_.configure(token,sizeof(token),form.ssid.data(),form.password.data());page_header(stream);
    LINE(stream,"  <p>PicoFerm is connecting to your home Wi-Fi network</p>");
    LINE(stream,"  <p>In the meantime, follow the steps below to complete your registration:</p>");LINE(stream,"<ol>");
    LINE(stream,"  <li>Reconnect your mobile device or laptop to your home Wi-Fi network.</li>");
    WRITE(stream,"  <li>Once your mobile device or laptop to your home Wi-Fi network, <a href=\"http://");
    text(stream,data_.website.data());WRITE(stream,"/Setup-Preconnection/PicoFermConnectionAttempt.cshtml?uid=");
    text(stream,data_.uid.data());WRITE(stream,"&token=");text(stream,token);LINE(stream,"\">Continue</a>.</li>");
    LINE(stream,"</ol>");page_footer(stream);
}
bool HttpServer::validate_trigger(HttpStream& stream,HttpRequest& incoming,const char* expected){
    parse_trigger(incoming,trigger,expected,ports_);
    if(!incoming.error)return true;
    diagnostic_warning(ports_);ports_.text("SRV","RESPONSE","HTTP_BAD_REQUEST");bad_request(stream);return false;
}
void HttpServer::dispatch(HttpStream& stream,HttpRequest& incoming){
    if(incoming.error){
        ports_.text("SRV","ERROR",incoming.error);ports_.boolean("SRV","IS_VALID",incoming.headers_complete);
        if(incoming.headers_complete)bad_request(stream);
        return;
    }
    const char* uri=incoming.uri.data();
    if(string_equal(uri,"/"))setup_page(stream);
    else if(string_equal(uri,"/factoryReset"))reset_page(stream);
    else if(string_equal(uri,"/calibrate"))calibration_page(stream);
    else if(string_equal(uri,"/details"))details_page(stream);
    else if(string_equal(uri,"/setSsid"))configure_page(stream,incoming);
    else if(string_equal(uri,"/factoryResetComplete")){
        if(!validate_trigger(stream,incoming,"reset"))return;
        page_header(stream);LINE(stream,"<h2>We are resetting your PicoFerm.</h2>");
        LINE(stream,"  <p>When all 3 indicator lights are on and steady (not blinking) on your PicoFerm, reconnect your phone (or computer) to your PicoFerm Access Point.</p>");
        LINE(stream,"  <p>Once connected, <a href=\"/\">Continue</a>.</p>");page_footer(stream);
        runtime_.delay_ms(2000);device_.factory_reset();
    }else if(string_equal(uri,"/calibrateComplete")){
        if(!validate_trigger(stream,incoming,"calibrate"))return;
        page_header(stream);LINE(stream,"<h2>Pressure calibration is in progress</h2>");
        LINE(stream,"  <p>Your PicoFerm will automatically shutdown when done so that you can connect it to a Keg.</p>");
        page_footer(stream);runtime_.delay_ms(2000);device_.calibrate();
    }else{
        ports_.text("SRV","RESPONSE","HTTP_NOT_FOUND");WRITE(stream,"HTTP/1.0 404 Not Found\r\n\r\n404 Not Found");
    }
}
void HttpServer::poll(){
    listener_.process_dns();ports_.yield();if(!enabled)return;
    HttpStream* stream=listener_.accept();if(!stream)return;
    parse_http(*stream,request,hardware_,ports_,timeout);stream->flush();ports_.yield();
    dispatch(*stream,request);listener_.release(*stream);
}
}}
#undef WRITE
#undef LINE
#undef FERM_HTTP_FLASH
#undef FERM_HTTP_LITERAL
