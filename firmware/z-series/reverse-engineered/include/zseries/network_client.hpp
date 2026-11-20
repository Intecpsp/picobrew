// Z119 client selection and custom Ethernet socket policy.
#pragma once
#include "zseries/console.hpp"
#include "zseries/http_client.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {
enum class NetworkResult { Ok, MissingService, ServiceFailure, InvalidInput };
enum class ClientOperation : std::uint8_t { Create, Stop, Destroy, Connected, Timeout, PrepareEthernet };
struct ClientAction {
    ClientOperation operation;
    HttpClientClass kind;
    std::uintptr_t handle = 0;
    std::uint32_t argument = 0;
    ClientAction(ClientOperation operation_, HttpClientClass kind_) noexcept : operation(operation_), kind(kind_) {}
};
struct ClientFactoryServices {
    void* context = nullptr;
    bool (*perform)(void*, const ClientAction&, std::uintptr_t& result) = nullptr;
    ConsoleServices diagnostics;
};
// Each HTTP client owns one factory. Handles are owned by its platform binding;
// all three slots are cleared in Ethernet/WiFi/WiFiSecure order on a class change.
class ClientFactory {
public:
    HttpClientClass kind = HttpClientClass::None;
    std::array<std::uintptr_t, 3> handles{};
    ClientFactoryServices services;
    NetworkResult clear();
    NetworkResult acquire(std::uint32_t network, bool secure, std::uintptr_t& handle,
                          HttpClientClass& selected, bool& accepted);
private:
    NetworkResult perform(ClientOperation, HttpClientClass, std::uintptr_t, std::uint32_t,
                          std::uintptr_t* result = nullptr);
    NetworkResult assertion(std::uint32_t line, const char* expression);
};

enum class SocketOperation : std::uint8_t {
    Status, Open, Connect, Disconnect, Close, ClearServerPort, Send, Receive,
    Available, Peek, Flush, TlsStart, TlsStop, TlsWrite, TlsRead, TlsAvailable,
    TlsError, BadRead
};
struct SocketAction {
    SocketOperation operation;
    std::uint8_t socket;
    std::uint32_t argument = 0, second = 0;
    const std::uint8_t* data = nullptr;
    std::uint8_t* destination = nullptr;
    std::size_t size = 0;
    SocketAction(SocketOperation operation_, std::uint8_t socket_) noexcept : operation(operation_), socket(socket_) {}
};
struct EthernetSocketServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    void (*delay_ms)(void*, std::uint32_t) = nullptr;
    bool (*perform)(void*, const SocketAction&, std::int32_t& result) = nullptr;
};
class EthernetSocket {
public:
    std::uint8_t socket = 8;
    bool secure = false, tls_active = false, write_error = false;
    EthernetSocketServices services;
    NetworkResult callback_result = NetworkResult::Ok;
    int connect(std::uint32_t address, std::uint16_t port, std::uint16_t& source_port);
    int status();
    bool connected();
    std::size_t raw_write(const std::uint8_t*, std::size_t);
    int raw_read(std::uint8_t*, std::size_t);
    std::size_t write(const std::uint8_t*, std::size_t);
    int read(std::uint8_t*, std::size_t);
    int read();
    int available();
    int peek();
    void flush();
    void stop();
private:
    std::int32_t perform(const SocketAction&);
    std::int32_t operation(SocketOperation);
    bool ready();
};
}} // namespace zseries::oem
