#include "picoferm/backend.hpp"
#include <stdio.h>
namespace picoferm { namespace oem {
void Backend::set_headers(const char* website){extra_headers.clear();extra_headers.append("Host: ",ports_);extra_headers.append(website,ports_);extra_headers.append("\nConnection: close\n\n",ports_);}
void Backend::request_prefix(const char* action){
    if(!action||!*action)diagnostic_assertion(ports_);
    request.clear();request.append("GET /API/PicoFerm/",18,ports_);request.append(action,ports_);request.append("?uid=",5,ports_);request.append(data_.uid.data(),ports_);
}
void Backend::argument(const char* key,const char* value){request.append("&",ports_);request.append(key,ports_);request.append("=",ports_);request.append(value,ports_);}
void Backend::fail(const char* message){reading=false;if(!transport_error){diagnostic_warning(ports_);transport_error=message;}}
void Backend::send(const char* text,const char* headers){
    ports_.text("CC","SEND",text);
    if(pending){diagnostic_assertion(ports_);diagnostic_warning(ports_);return;}
    reading=false;transport_error=nullptr;inside=false;received=0;response.fill(0);deadline=hardware_.millis()+timeout;pending=true;
    for(unsigned failed=0;;){
        if(client_.connect_ip(host_ip,80))break;
        diagnostic_warning(ports_);ports_.integer("CC","FAILED_COUNT",++failed);
        if(failed==4){fail("Failed to connect");return;}runtime_.delay_ms(500);
    }
    if(text)client_.write(text,std::strlen(text));
    if(headers)client_.write(headers,std::strlen(headers));
    reading=true;
}
void Backend::finish_request(std::uint32_t command){request.append(" HTTP/1.0\n",ports_);send(request.c_str(),extra_headers.c_str());pending_command=command;}
void Backend::sync(std::int32_t minutes,float battery,bool logs){
    if(minutes<1)diagnostic_assertion(ports_);
    if(!(battery>=0.0f))diagnostic_assertion(ports_);
    if(pending_command){diagnostic_assertion(ports_);return;}
    if(stored_.bytes[PersistentData::log_count_position]==0&&logs){ports_.integer("BEND","_eeprom.NumLogs",0);logs=false;}
    ports_.boolean("BEND","sendLogs",logs);status.started=true;send_logs=logs;rate=minutes;voltage=battery;
    status.done=false;status.error=false;status.known=false;status.registered=false;status.needs_update=false;status.update_address.fill(0);status.session=Session{};status.data_sent=false;
    request_prefix("isRegistered");argument("token",data_.token.data());finish_request(1);
}
void Backend::request_state(){request_prefix("getState");finish_request(4);}
void Backend::request_logs(){
    request_prefix("logDataSet");char text[64];snprintf(text,sizeof(text),"%d",rate);argument("rate",text);
    formatter_.decimal(voltage,text,sizeof(text));argument("voltage",text);request.append("&data=[",ports_);bool first=true;
    for(LogIterator iterator(stored_);!iterator.done();iterator.advance()){
        Measurement measurement;if(!stored_.measurement(iterator.index,measurement)){diagnostic_assertion(ports_);break;}
        if(!first)request.append(",",ports_);
        request.append("{\"s1\":",ports_);formatter_.decimal(measurement.temperature,text,sizeof(text));request.append(text,ports_);
        request.append(",\"s2\":",ports_);formatter_.decimal(measurement.pressure,text,sizeof(text));request.append(text,ports_);request.append("}",ports_);first=false;
    }
    request.append("]",ports_);finish_request(5);
}
void Backend::receive_session(){
    ports_.text("BEND","STATE",response.data());parse_session(status.session,response.data(),ports_);
    ports_.text("BEND","Session.State",session_name(status.session.state));ports_.integer("BEND","Session.ErrorFlags",status.session.error_flags);
    ports_.boolean("BEND","Session.HasFermentationError",status.session.has_fermentation_error());
}
void Backend::complete(){
    if(!pending_command){diagnostic_assertion(ports_);return;}
    if(transport_error)status.error=true;
    else switch(pending_command){
    case 1:
        status.registered=response[0]=='1';status.known=status.registered||response[0]=='0';
        ports_.boolean("BEND","IsRegistered",status.registered);ports_.boolean("BEND","IsKnownPicoFerm",status.known);
        request_prefix("checkFirmware");argument("version",data_.version.data());finish_request(2);return;
    case 2:
        status.needs_update=response[0]=='1';ports_.boolean("BEND","NeedsUpdate",status.needs_update);
        if(status.needs_update){request_prefix("getFirmwareAddress");finish_request(3);}else request_state();return;
    case 3:
        if(response[0]=='-')diagnostic_warning(ports_);
        else {std::memcpy(status.update_address.data(),response.data(),128);char address[129]={0};std::memcpy(address,status.update_address.data(),128);ports_.text("BEND","UpdateAddress",address);}
        request_state();return;
    case 4:receive_session();if(send_logs){request_logs();return;}break;
    case 5:receive_session();status.data_sent=true;ports_.boolean("BEND","LogDataSent",true);break;
    default:return;
    }
    status.done=true;pending_command=0;ports_.boolean("BEND","IsSyncDone",true);ports_.boolean("BEND","HasError",status.error);
}
void Backend::poll(){
    if(!pending)return;
    bool consumed=false;
    while(reading){
        ports_.yield();const int available=client_.available();
        if(available==0){
            const auto now=hardware_.millis();
            if(!client_.connected())fail("Client no more connected");else if(now>deadline)fail("Client read timeout");
            break;
        }
        const auto byte=static_cast<std::uint8_t>(client_.read());
        if(byte=='#'){
            if(inside){reading=false;response[received]=0;}inside=!inside;
        }else if(inside){if(received<255)response[received++]=static_cast<char>(byte);else fail("Read buffer is too small!");}
        consumed=true;
    }
    if(!reading){char length[16];snprintf(length,sizeof(length),"%u",received);ports_.text("BEND","RECVLEN",length);pending=false;client_.stop();ports_.yield();complete();}
    else if(consumed)deadline=hardware_.millis()+timeout;
}
}}
