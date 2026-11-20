#include "zseries/mqtt_client.hpp"
#include <cstring>

namespace zseries { namespace oem {
constexpr std::size_t MqttClient::BufferSize;
constexpr std::uint32_t MqttClient::KeepAliveMs;
bool MqttClient::ready() const noexcept {
    return bound&&transport.millis&&transport.connected&&transport.write&&transport.available&&transport.read&&transport.flush&&transport.stop;
}
MqttResult MqttClient::connected(bool& value){
    value=false;if(!bound)return MqttResult::Ok;
    if(!transport.connected)return MqttResult::MissingService;
    if(!transport.connected(transport.context,value))return MqttResult::ServiceFailure;
    if(!value&&state==0){state=-3;if(!transport.flush||!transport.stop)return MqttResult::MissingService;
        if(!transport.flush(transport.context)||!transport.stop(transport.context))return MqttResult::ServiceFailure;}
    return MqttResult::Ok;
}
std::uint16_t MqttClient::write_string(const char* text,std::uint16_t at){
    const auto size=std::strlen(text);buffer[at]=static_cast<std::uint8_t>(size>>8);buffer[at+1]=static_cast<std::uint8_t>(size);
    std::memcpy(buffer.data()+at+2,text,size);return static_cast<std::uint16_t>(at+2+size);
}
MqttResult MqttClient::write_packet(std::uint8_t header,std::uint16_t length,bool& accepted){
    accepted=false;if(!transport.write||!transport.millis)return MqttResult::MissingService;
    std::uint8_t encoded[4];unsigned count=0;std::uint16_t remaining=length;
    do{encoded[count]=static_cast<std::uint8_t>(remaining&127);remaining>>=7;if(remaining)encoded[count]|=128;++count;}while(remaining);
    const auto start=4-count;buffer[start]=header;for(unsigned i=0;i<count;++i)buffer[start+1+i]=encoded[i];
    std::size_t wrote=0;const auto size=static_cast<std::size_t>(length)+count+1;
    if(!transport.write(transport.context,buffer.data()+start,size,wrote))return MqttResult::ServiceFailure;
    last_out=transport.millis(transport.context);accepted=static_cast<std::uint16_t>(wrote)==size;return MqttResult::Ok;
}
MqttResult MqttClient::read_byte(std::uint8_t& byte,bool& read){
    read=false;if(!transport.millis||!transport.available||!transport.read)return MqttResult::MissingService;
    const auto started=transport.millis(transport.context);
    for(;;){int available=0;if(!transport.available(transport.context,available))return MqttResult::ServiceFailure;
        if(available){int value=0;if(!transport.read(transport.context,value))return MqttResult::ServiceFailure;byte=static_cast<std::uint8_t>(value);read=true;return MqttResult::Ok;}
        if(static_cast<std::uint32_t>(transport.millis(transport.context)-started)>=KeepAliveMs)return MqttResult::Ok;}
}
MqttResult MqttClient::read_packet(std::uint16_t& result,std::uint8_t& length_bytes){
    result=0;length_bytes=0;std::uint16_t length=0;bool got=false;auto status=read_byte(buffer[0],got);if(status!=MqttResult::Ok||!got)return status;
    ++length;const auto header=buffer[0];std::uint16_t remaining=0,multiplier=1;std::uint8_t byte=0;
    do{if(length==6){state=-1;if(!transport.stop)return MqttResult::MissingService;return transport.stop(transport.context)?MqttResult::Ok:MqttResult::ServiceFailure;}
        status=read_byte(byte,got);if(status!=MqttResult::Ok||!got)return status;buffer[length++]=byte;
        remaining=static_cast<std::uint16_t>(remaining+(byte&127)*multiplier);multiplier=static_cast<std::uint16_t>(multiplier<<7);
    }while(byte&128);
    length_bytes=static_cast<std::uint8_t>(length-1);std::uint16_t consumed=0;
    if((header&0xf0)==0x30){for(unsigned i=0;i<2;++i){status=read_byte(buffer[length],got);if(status!=MqttResult::Ok||!got)return status;++length;}consumed=2;}
    while(consumed<remaining){status=read_byte(byte,got);if(status!=MqttResult::Ok||!got)return status;if(length<BufferSize)buffer[length]=byte;++length;++consumed;}
    // Azure uses the original non-streaming path: oversized packets are drained
    // completely, then discarded. The optional library Stream API is not used.
    result=length>BufferSize?0:length;return MqttResult::Ok;
}
MqttResult MqttClient::connect(const MqttConnect& options,bool& accepted){
    accepted=false;bool present=false;auto status=connected(present);if(status!=MqttResult::Ok)return status;if(present){accepted=true;return MqttResult::Ok;}
    if(!ready()||(!host&&!transport.connect_ip)||(host&&!transport.connect_host))return MqttResult::MissingService;
    if(!options.client_id||(options.will_topic&&!options.will_message))return MqttResult::InvalidInput;
    std::size_t needed=15+2+std::strlen(options.client_id);
    if(options.will_topic)needed+=4+std::strlen(options.will_topic)+std::strlen(options.will_message);
    if(options.username){needed+=2+std::strlen(options.username);if(options.password)needed+=2+std::strlen(options.password);}
    // The original unchecked CONNECT string writer can overrun its packet.
    // Required application identities fit; overlong inputs fail explicitly.
    if(needed>BufferSize)return MqttResult::PacketTooLarge;
    int connected_result=0;const bool done=host?transport.connect_host(transport.context,host,port,connected_result):transport.connect_ip(transport.context,address,port,connected_result);
    if(!done)return MqttResult::ServiceFailure;
    if(connected_result!=1){state=-2;return MqttResult::Ok;}
    message_id=1;const std::uint8_t protocol[]={0,4,'M','Q','T','T',4};std::memcpy(buffer.data()+5,protocol,sizeof(protocol));
    std::uint8_t flags=2;if(options.will_topic)flags=static_cast<std::uint8_t>(6|(options.will_qos<<3)|(options.will_retain?32:0));
    if(options.username){flags|=128;if(options.password)flags|=64;}buffer[12]=flags;buffer[13]=0;buffer[14]=15;
    auto length=write_string(options.client_id,15);if(options.will_topic){length=write_string(options.will_topic,length);length=write_string(options.will_message,length);}
    if(options.username){length=write_string(options.username,length);if(options.password)length=write_string(options.password,length);}
    bool wrote=false;status=write_packet(0x10,static_cast<std::uint16_t>(length-5),wrote);if(status!=MqttResult::Ok)return status;
    last_out=transport.millis(transport.context);last_in=last_out;
    for(;;){int available=0;if(!transport.available(transport.context,available))return MqttResult::ServiceFailure;
        if(available){std::uint8_t length_bytes=0;std::uint16_t packet=0;status=read_packet(packet,length_bytes);if(status!=MqttResult::Ok)return status;
            if(packet==4){if(buffer[3]==0){last_in=transport.millis(transport.context);ping_outstanding=false;state=0;accepted=true;return MqttResult::Ok;}state=buffer[3];}
            return transport.stop(transport.context)?MqttResult::Ok:MqttResult::ServiceFailure;}
        if(static_cast<std::uint32_t>(transport.millis(transport.context)-last_in)>=KeepAliveMs){state=-4;return transport.stop(transport.context)?MqttResult::Ok:MqttResult::ServiceFailure;}}
}
MqttResult MqttClient::disconnect(){
    if(!ready())return MqttResult::MissingService;
    buffer[0]=0xe0;buffer[1]=0;std::size_t count=0;
    if(!transport.write(transport.context,buffer.data(),2,count))return MqttResult::ServiceFailure;
    state=-1;if(!transport.stop(transport.context))return MqttResult::ServiceFailure;
    last_out=transport.millis(transport.context);last_in=last_out;return MqttResult::Ok;
}
MqttResult MqttClient::publish(const char* topic,const void* data,std::size_t size,bool retained,bool& accepted){
    accepted=false;bool present=false;auto status=connected(present);if(status!=MqttResult::Ok||!present)return status;
    if(!topic||(!data&&size))return MqttResult::InvalidInput;
    const auto topic_size=std::strlen(topic);if(topic_size>BufferSize-7||size>BufferSize-7-topic_size)return MqttResult::Ok;
    auto end=write_string(topic,5);if(size)std::memcpy(buffer.data()+end,data,size);end=static_cast<std::uint16_t>(end+size);
    return write_packet(retained?0x31:0x30,static_cast<std::uint16_t>(end-5),accepted);
}
MqttResult MqttClient::subscribe(const char* topic,std::uint8_t qos,bool& accepted){
    accepted=false;if(!topic)return MqttResult::InvalidInput;if(qos>1||std::strlen(topic)>BufferSize-9)return MqttResult::Ok;
    bool present=false;auto status=connected(present);if(status!=MqttResult::Ok||!present)return status;
    if(++message_id==0)message_id=1;
    buffer[5]=static_cast<std::uint8_t>(message_id>>8);buffer[6]=static_cast<std::uint8_t>(message_id);
    const auto end=write_string(topic,7);buffer[end]=qos;return write_packet(0x82,static_cast<std::uint16_t>(end-4),accepted);
}
MqttResult MqttClient::loop(bool& active){
    active=false;auto status=connected(active);if(status!=MqttResult::Ok||!active)return status;if(!ready())return MqttResult::MissingService;
    const auto now=transport.millis(transport.context);
    if(static_cast<std::uint32_t>(now-last_in)>KeepAliveMs||static_cast<std::uint32_t>(now-last_out)>KeepAliveMs){
        if(ping_outstanding){state=-4;active=false;return transport.stop(transport.context)?MqttResult::Ok:MqttResult::ServiceFailure;}
        buffer[0]=0xc0;buffer[1]=0;std::size_t count=0;if(!transport.write(transport.context,buffer.data(),2,count))return MqttResult::ServiceFailure;
        last_in=now;last_out=now;ping_outstanding=true;
    }
    int available=0;if(!transport.available(transport.context,available))return MqttResult::ServiceFailure;if(!available)return MqttResult::Ok;
    std::uint8_t length_bytes=0;std::uint16_t length=0;status=read_packet(length,length_bytes);if(status!=MqttResult::Ok)return status;
    if(!length){bool connected_now=false;status=connected(connected_now);if(!connected_now)active=false;return status;}
    last_in=now;const auto type=buffer[0]&0xf0;std::size_t count=0;
    if(type==0x30&&message){
        const auto topic_length=(static_cast<std::uint16_t>(buffer[length_bytes+1])<<8)|buffer[length_bytes+2];
        const auto payload_at=static_cast<std::size_t>(length_bytes)+3+topic_length;const bool qos1=(buffer[0]&6)==2;
        if(payload_at+(qos1?2:0)>length||payload_at>BufferSize)return MqttResult::MalformedPacket;
        std::memmove(buffer.data()+length_bytes+2,buffer.data()+length_bytes+3,topic_length);buffer[length_bytes+2+topic_length]=0;
        const auto first=qos1?buffer[payload_at]:0,second=qos1?buffer[payload_at+1]:0;
        if(!message(message_context,reinterpret_cast<const char*>(buffer.data()+length_bytes+2),reinterpret_cast<char*>(buffer.data()+payload_at+(qos1?2:0)),length-payload_at-(qos1?2:0)))return MqttResult::ServiceFailure;
        if(qos1){buffer[0]=0x40;buffer[1]=2;buffer[2]=first;buffer[3]=second;if(!transport.write(transport.context,buffer.data(),4,count))return MqttResult::ServiceFailure;last_out=now;}
    }else if(type==0xc0){buffer[0]=0xd0;buffer[1]=0;if(!transport.write(transport.context,buffer.data(),2,count))return MqttResult::ServiceFailure;}
    else if(type==0xd0)ping_outstanding=false;
    return MqttResult::Ok;
}
}} // namespace zseries::oem
