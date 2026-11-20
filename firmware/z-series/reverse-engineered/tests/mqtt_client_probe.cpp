#define ARDUINOJSON_USE_LONG_LONG 0
#include <ArduinoJson.h>
#include "zseries/mqtt_client.hpp"
#include <iostream>
#include <string>
#include <vector>
using namespace zseries::oem;
namespace {
std::string hex(const std::uint8_t* bytes,std::size_t size){const char digits[]="0123456789abcdef";std::string out;out.reserve(size*2);for(std::size_t i=0;i<size;++i){out+=digits[bytes[i]>>4];out+=digits[bytes[i]&15];}return out;}
struct Context {ArduinoJson::JsonArray effects;std::vector<int> input;std::size_t at=0;std::uint32_t clock=1000,step=0;bool connected=true;int connect_result=1;std::uint32_t write_limit=0xffffffff;};
std::uint32_t clock(void* p){auto& c=*static_cast<Context*>(p);const auto now=c.clock;c.clock+=c.step;return now;}
bool connected(void* p,bool& value){auto& c=*static_cast<Context*>(p);value=c.connected;auto r=c.effects.createNestedArray();r.add("connected");r.add(value?1:0);return true;}
bool connect_host(void* p,const char* host,std::uint16_t port,int& value){auto& c=*static_cast<Context*>(p);value=c.connect_result;c.connected=value==1;auto r=c.effects.createNestedArray();r.add("connect");r.add(host);r.add(port);r.add(static_cast<std::uint32_t>(value));return true;}
bool connect_ip(void* p,std::uint32_t ip,std::uint16_t port,int& value){auto& c=*static_cast<Context*>(p);value=c.connect_result;c.connected=value==1;auto r=c.effects.createNestedArray();r.add("connect_ip");r.add(ip);r.add(port);r.add(static_cast<std::uint32_t>(value));return true;}
bool write(void* p,const std::uint8_t* data,std::size_t size,std::size_t& value){auto& c=*static_cast<Context*>(p);value=std::min<std::size_t>(size,c.write_limit);auto r=c.effects.createNestedArray();r.add("write");r.add(hex(data,size));r.add(value);return true;}
bool available(void* p,int& value){auto& c=*static_cast<Context*>(p);value=c.input.size()-c.at;auto r=c.effects.createNestedArray();r.add("available");r.add(value);return true;}
bool read(void* p,int& value){auto& c=*static_cast<Context*>(p);value=c.at<c.input.size()?c.input[c.at++]:-1;auto r=c.effects.createNestedArray();r.add("read");r.add(static_cast<std::uint32_t>(value));return true;}
bool flush(void* p){auto& c=*static_cast<Context*>(p);auto r=c.effects.createNestedArray();r.add("flush");return true;}
bool stop(void* p){auto& c=*static_cast<Context*>(p);c.connected=false;auto r=c.effects.createNestedArray();r.add("stop");return true;}
bool message(void* p,const char* topic,char* payload,std::size_t size){auto& c=*static_cast<Context*>(p);auto r=c.effects.createNestedArray();r.add("message");r.add(std::string(topic));r.add(hex(reinterpret_cast<std::uint8_t*>(payload),size));r.add(size);return true;}
const char* string(ArduinoJson::JsonVariantConst in,const char* key,const char* fallback){if(in.containsKey(key))return in[key].as<const char*>();return fallback;}
}
int main(){std::string line;while(std::getline(std::cin,line)){
    ArduinoJson::DynamicJsonDocument input(65536),output(262144);if(ArduinoJson::deserializeJson(input,line)!=ArduinoJson::DeserializationError::Ok)return 2;
    auto in=input.as<ArduinoJson::JsonVariantConst>();Context c;c.effects=output.createNestedArray("effects");for(auto v:in["incoming"].as<ArduinoJson::JsonArrayConst>())c.input.push_back(v.as<int>());c.clock=in["clock"]|1000u;c.step=in["clock_step"]|0u;c.connected=in["connected"]|true;c.connect_result=in["connect_result"]|1;c.write_limit=in["write_limit"]|0xffffffffu;
    MqttTransport io;io.context=&c;io.millis=clock;io.connected=connected;io.connect_host=connect_host;io.connect_ip=connect_ip;io.write=write;io.available=available;io.read=read;io.flush=flush;io.stop=stop;if(in["missing_read"]|false)io.read=nullptr;
    MqttClient client;client.bind(io);client.bound=in["bound"]|true;client.state=in["state"]|-1;client.last_in=in["last_in"]|0u;client.last_out=in["last_out"]|0u;client.ping_outstanding=in["ping"]|false;client.message_id=in["message_id"]|0u;client.host=string(in,"host","fixture.example");client.address=in["address"]|0x01020304u;client.port=in["port"]|8883u;client.message_context=&c;client.message=(in["callback"]|true)?message:nullptr;
    const auto op=in["op"].as<unsigned>();bool accepted=false;std::uint16_t length=0;std::uint8_t length_bytes=0;MqttResult result=MqttResult::Ok;
    if(op==0)result=client.connected(accepted);else if(op==1)result=client.disconnect();
    else if(op==2){MqttConnect options;options.client_id=string(in,"client_id","device");options.username=string(in,"username","user");options.password=string(in,"password","password");options.will_topic=string(in,"will_topic",nullptr);options.will_message=string(in,"will_message",nullptr);options.will_qos=in["will_qos"]|0u;options.will_retain=in["will_retain"]|false;result=client.connect(options,accepted);}
    else if(op==3){const std::string payload=string(in,"payload","state");result=client.publish(string(in,"topic","devices/fixture/messages/events/"),payload.data(),payload.size(),in["retained"]|false,accepted);}
    else if(op==4)result=client.subscribe(string(in,"topic","devices/fixture/messages/devicebound/#"),in["qos"]|0u,accepted);
    else if(op==5)result=client.loop(accepted);else if(op==6)result=client.read_packet(length,length_bytes);else return 3;
    output["id"]=in["id"].as<const char*>();output["accepted"]=accepted;output["state"]=static_cast<std::uint32_t>(client.state);output["last_in"]=client.last_in;output["last_out"]=client.last_out;output["ping"]=client.ping_outstanding?1:0;output["message_id"]=client.message_id;output["length"]=length;output["length_bytes"]=length_bytes;output["buffer"]=hex(client.buffer.data(),client.buffer.size());output["clock"]=c.clock;output["read"]=c.at;output["connected"]=c.connected?1:0;output["result"]=static_cast<unsigned>(result);
    ArduinoJson::serializeJson(output,std::cout);std::cout<<'\n';
}}
