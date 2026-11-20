#include "picostill/backend.hpp"
#include <cstring>
#include <stdio.h>
namespace picostill { namespace oem {
void Backend::append(const char* value) {
    if(!value || !*value)return;
    const auto size=std::strlen(value);
    diagnostics_.check(size<request_remaining,"datalen < _left Buffer not big enough");
    const std::size_t copied=size<request_remaining?size:request_remaining;
    std::memcpy(request.data()+request_length,value,copied);request_length+=copied;request_remaining-=copied;
}
void Backend::reset(){status_.done=false;has_error=false;update_address.fill(0);status_.has_update_address=false;diagnostics_.check(true,"!needsUpdate()");}
void Backend::fail(const char* message){reading=false;if(!error){diagnostics_.message(message);error=message;}}
void Backend::sync(std::uint32_t command,const char* action,const char* key,const char* value) {
    diagnostics_.check(pending_command==0,"canSync()");if(pending_command==0){reset();send(action,key,value);pending_command=command;}
}
void Backend::send(const char* action,const char* key,const char* value) {
    diagnostics_.check(action && *action,"strlen(cmd) > 0");request.fill(0);request_length=0;request_remaining=511;
    append("GET /API/PicoStill/");append(action);append("?uid=");append(data_.uid.data());append("&");append(key);append("=");append(value);append(" HTTP/1.0\n");
    runtime_.variable("CC","SEND",request.data());diagnostics_.check(!pending,"!_pending");
    if(pending){diagnostics_.message("Ignoring second request");return;}
    reading=false;error=nullptr;inside=false;received=0;response.fill(0);deadline=runtime_.millis()+timeout;pending=true;
    if(!client_.connect_ip(status_.host_ip,80)){fail("Failed to connect");return;}
    client_.write(request.data(),std::strlen(request.data()));client_.write(extra_headers.data(),std::strlen(extra_headers.data()));reading=true;
}
void Backend::complete() {
    diagnostics_.check(pending_command!=0,"_pendingCmd != None");if(!pending_command)return;
    if(!error){
        if(pending_command==1){
            // Preserve the OEM's two independent character comparisons.
            if(response[0]!='-' && response[1]!='1'){
                std::memcpy(update_address.data(),response.data(),update_address.size());
                // Original data may be 128 non-NUL bytes. Diagnostics and the
                // later URL consumer use a bounded owned view in that domain.
                std::array<char,129> printable={{0}};std::memcpy(printable.data(),update_address.data(),update_address.size());
                runtime_.variable("BEND","UpdateAddress",printable.data());
            }
        }else if(pending_command!=2)return;
    }else has_error=true;
    status_.has_update_address=update_address[0]!=0;status_.done=true;pending_command=0;
    runtime_.variable("BEND","IsSyncDone","True");runtime_.variable("BEND","HasError",has_error?"True":"False");
}
void Backend::poll() {
    if(!pending)return;
    bool consumed=false;
    while(reading){
        runtime_.cooperate();const int available=client_.available();
        if(available<1){
            const auto now=runtime_.millis();if(!client_.connected())fail("Client no more connected");else if(deadline<now)fail("Client read timeout");
            if(reading){if(consumed)deadline=runtime_.millis()+timeout;return;}break;
        }
        const auto value=static_cast<std::uint32_t>(client_.read());
        if(value<256){
            if(value=='#'){if(inside){reading=false;response[received]=0;}inside=!inside;}
            else if(inside){if(received<255)response[received++]=static_cast<char>(value);else fail("Read buffer is too small!");}
            consumed=true;
        }else{runtime_.integer("HTTP","client.read()",value);runtime_.integer("HTTP","available",static_cast<std::uint32_t>(available));}
    }
    char size[16];snprintf(size,sizeof(size),"%u",received);runtime_.variable("BEND","RECVLEN",size);pending=false;complete();
}
}} // namespace picostill::oem
