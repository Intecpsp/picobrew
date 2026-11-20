// Wire client recovered from the Z119 PubSubClient-family implementation.
// The image, not an unverified upstream release, pins this implementation.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace zseries { namespace oem {
enum class MqttResult { Ok, MissingService, ServiceFailure, InvalidInput, PacketTooLarge, MalformedPacket };
struct MqttTransport {
    void* context=nullptr;
    std::uint32_t (*millis)(void*)=nullptr;
    bool (*connected)(void*,bool&)=nullptr;
    bool (*connect_host)(void*,const char*,std::uint16_t,int&)=nullptr;
    bool (*connect_ip)(void*,std::uint32_t,std::uint16_t,int&)=nullptr;
    bool (*write)(void*,const std::uint8_t*,std::size_t,std::size_t&)=nullptr;
    bool (*available)(void*,int&)=nullptr;
    bool (*read)(void*,int&)=nullptr;
    bool (*flush)(void*)=nullptr;
    bool (*stop)(void*)=nullptr;
};
struct MqttConnect {
    const char* client_id=nullptr;
    const char* username=nullptr;
    const char* password=nullptr;
    const char* will_topic=nullptr;
    const char* will_message=nullptr;
    std::uint8_t will_qos=0;
    bool will_retain=false;
};
class MqttClient {
public:
    static constexpr std::size_t BufferSize=1024;
    static constexpr std::uint32_t KeepAliveMs=15000;
    MqttTransport transport;
    bool bound=false,ping_outstanding=false;
    std::array<std::uint8_t,BufferSize> buffer{};
    std::uint16_t message_id=0;
    std::uint32_t last_out=0,last_in=0;
    std::int32_t state=-1;
    const char* host=nullptr;
    std::uint32_t address=0;
    std::uint16_t port=1883;
    void* message_context=nullptr;
    bool (*message)(void*,const char*,char*,std::size_t)=nullptr;
    void bind(const MqttTransport& value) noexcept {transport=value;bound=true;}
    void server(const char* name,std::uint16_t value) noexcept {host=name;port=value;}
    MqttResult connected(bool&); // 0x401bff2c
    MqttResult connect(const MqttConnect&,bool&); // 0x40126d8c/0x40126f4c
    MqttResult disconnect(); // 0x40126d48
    MqttResult publish(const char*,const void*,std::size_t,bool retained,bool&); // 0x401270fc
    MqttResult subscribe(const char*,std::uint8_t qos,bool&); // 0x4012718c
    MqttResult loop(bool&); // 0x40126f6c
    MqttResult read_packet(std::uint16_t& length,std::uint8_t& length_bytes); // 0x40126b90
private:
    bool ready() const noexcept;
    MqttResult read_byte(std::uint8_t&,bool&);
    MqttResult write_packet(std::uint8_t header,std::uint16_t length,bool&);
    std::uint16_t write_string(const char*,std::uint16_t);
};
}} // namespace zseries::oem
