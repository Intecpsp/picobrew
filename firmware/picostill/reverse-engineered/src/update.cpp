#include "picostill/update.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits>
namespace picostill { namespace oem {
namespace {
void append(std::array<char,64>& buffer,std::uint32_t& length,std::uint32_t& remaining,const char* text,std::size_t size,Diagnostics& diagnostics){
    if(!text || !size)return;
    diagnostics.check(size<remaining,"datalen < _left Buffer not big enough");const auto copied=std::min<std::size_t>(size,remaining);std::memcpy(buffer.data()+length,text,copied);length+=copied;remaining-=copied;
}
std::string trim(const std::string& text){const auto begin=text.find_first_not_of(" \t\r\n\f\v");return begin==std::string::npos?std::string():text.substr(begin,text.find_last_not_of(" \t\r\n\f\v")-begin+1);}
std::int32_t decimal(const char* text){const long long number=::strtoll(text,nullptr,10);return static_cast<std::int32_t>(std::max<long long>(std::numeric_limits<std::int32_t>::min(),std::min<long long>(std::numeric_limits<std::int32_t>::max(),number)));}
}
void UpdateParameters::parse(const char* uri,Diagnostics& diagnostics) {
    host.fill(0);path.fill(0);host_length=path_length=0;host_remaining=path_remaining=63;valid=false;
    diagnostics.require(uri && *uri,"HttpUpdate.h",0x28,"uri != 0 && strlen(uri) > 0");
    diagnostics.require(string_starts_with(uri,"http://"),"HttpUpdate.h",0x29,"CString::startsWith(uri, \"http://\")");
    // Original continues into invalid pointer arithmetic for short/null input.
    if(!uri || std::strlen(uri)<7)return;
    const char* hostname=uri+7;const char* slash=std::strchr(hostname,'/');
    diagnostics.require(slash!=nullptr,"HttpUpdate.h",0x2d,"slash != 0");if(!slash)return;
    append(host,host_length,host_remaining,hostname,static_cast<std::size_t>(slash-hostname),diagnostics);
    append(path,path_length,path_remaining,slash,std::strlen(slash),diagnostics);valid=true;
}
void UpdateParameters::report(HttpRuntime& runtime) const{runtime.variable("OTAP","AreValid",valid?"True":"False");runtime.variable("OTAP","Host",host.data());runtime.variable("OTAP","Path",path.data());}
void FirmwareUpdate::begin(const char* host,const char* path) {
    content_length=0;binary_content=false;written=0;state=0;changed_at=runtime_.millis();
    if(!client_.connect_host(host,80)){diagnostics_.message("client.connect(host, 80) failed");return;}
    const std::string request=std::string("GET ")+path+" HTTP/1.1\r\nHost: "+host+"\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
    client_.write(request.data(),request.size());state=1;changed_at=runtime_.millis();
}
void FirmwareUpdate::poll() {
    if(state==0)return;
    const auto now=runtime_.millis();
    if(state==1){
        if(client_.available()!=0)state=2;
        else if(now>changed_at+5000u){diagnostics_.message("Client timeout");client_.stop();state=0;}
        else return;
    }else if(state==2){
        while(client_.available()!=0){
            const std::string line=trim(client_.read_line());if(line.empty())break;
            if(line.compare(0,8,"HTTP/1.1")==0 && line.find("200")==std::string::npos){flash_.console("Got a non 200 status code from server. Exiting OTA Update.",true);break;}
            if(line.compare(0,16,"Content-Length: ")==0){content_length=decimal(line.c_str()+16);runtime_.integer("OTA","Content-Length",static_cast<std::uint32_t>(content_length));}
            else if(line.compare(0,14,"Content-Type: ")==0){const std::string value=line.substr(14);runtime_.variable("OTA","Content-Type",value.c_str());if(value=="application/octet-stream")binary_content=true;}
        }
        if(content_length<1 || !binary_content){flash_.console("There was no content in the response",true);client_.flush();state=0;}else state=3;
    }else if(state==3){
        if(!flash_.begin(static_cast<std::uint32_t>(content_length))){diagnostics_.message("Not enough space to begin OTA");client_.flush();state=0;}
        else{written=0;state=4;}
    }else if(state==4){
        const int available=std::min(client_.available(),2048);
        if(available>0){
            // Preserve OEM requested-length accounting, including ignoring
            // short Client/Update return values. Modified policy is separate.
            client_.read_bytes(buffer.data(),static_cast<std::size_t>(available));flash_.write(buffer.data(),static_cast<std::size_t>(available));written+=static_cast<std::uint32_t>(available);flash_.console(".",false);
        }
        if(written<static_cast<std::uint32_t>(content_length))return;
        runtime_.integer("OTA","_written",written);state=5;
    }else if(state==5){
        if(!flash_.end()){diagnostics_.message("OTA Update.end() failed");runtime_.integer("OTA","ERROR",flash_.error());diagnostics_.message("OTA Update Error");state=0;}
        else if(!flash_.finished()){diagnostics_.message("OTA Update.isFinished() failed");runtime_.integer("OTA","ERROR",flash_.error());state=0;}
        else{runtime_.variable("OTA","STATE","COMPLETE");const std::string hash=flash_.md5();runtime_.variable("OTA","MD5",hash.c_str());state=6;}
    }else return;
    changed_at=runtime_.millis();
}
}} // namespace picostill::oem
