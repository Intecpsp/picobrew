#include "zseries/network_client.hpp"
namespace zseries { namespace oem {
NetworkResult ClientFactory::perform(ClientOperation operation, HttpClientClass type, std::uintptr_t handle,
                                     std::uint32_t argument, std::uintptr_t* output) {
    if (!services.perform) return NetworkResult::MissingService;
    ClientAction action(operation, type); action.handle = handle; action.argument = argument;
    std::uintptr_t result = 0;
    if (!services.perform(services.context, action, result)) return NetworkResult::ServiceFailure;
    if (output) *output = result;
    return NetworkResult::Ok;
}
NetworkResult ClientFactory::assertion(std::uint32_t line, const char* expression) {
    if (!services.diagnostics.assertion) return NetworkResult::MissingService;
    return services.diagnostics.assertion(services.diagnostics.context, "ZClient.h", line, expression)
        ? NetworkResult::Ok : NetworkResult::ServiceFailure;
}
NetworkResult ClientFactory::clear() {
    for (unsigned i = 0; i < handles.size(); ++i) {
        if (!handles[i]) continue;
        const auto type = static_cast<HttpClientClass>(i + 1);
        auto result = perform(ClientOperation::Stop, type, handles[i], 0);
        if (result != NetworkResult::Ok) return result;
        if (handles[i]) {
            result = perform(ClientOperation::Destroy, type, handles[i], 0);
            if (result != NetworkResult::Ok) return result;
        }
        handles[i] = 0;
    }
    kind = HttpClientClass::None;
    return NetworkResult::Ok;
}
NetworkResult ClientFactory::acquire(std::uint32_t network, bool secure, std::uintptr_t& output,
                                    HttpClientClass& selected, bool& accepted) {
    output = 0; selected = HttpClientClass::None; accepted = false;
    if (network != 1 && network != 2) return NetworkResult::Ok;
    const unsigned index = network == 1 ? 0 : secure ? 2 : 1;
    const auto type = static_cast<HttpClientClass>(index + 1);
    const bool create = handles[index] == 0;
    if (create) {
        auto result = clear(); if (result != NetworkResult::Ok) return result;
        result = perform(ClientOperation::Create, type, 0, 0, &handles[index]);
        if (result != NetworkResult::Ok) return result;
        // The original dereferences allocation failure. The rebuild reports it.
        if (!handles[index]) return NetworkResult::ServiceFailure;
        if (index != 1) {
            result = perform(ClientOperation::Timeout, type, handles[index], index == 0 ? 30000 : 30);
            if (result != NetworkResult::Ok) return result;
        }
        kind = type;
    } else if (kind != type) {
        const std::uint32_t lines[] = {119, 138, 155};
        const char* expressions[] = {"_class == ClientClass::EthernetClient", "_class == ClientClass::WiFiClient", "_class == ClientClass::WiFiClientSecure"};
        auto result = assertion(lines[index], expressions[index]); if (result != NetworkResult::Ok) return result;
    }
    if (index != 0 || create) {
        std::uintptr_t connected = 0;
        auto result = perform(ClientOperation::Connected, type, handles[index], 0, &connected);
        if (result != NetworkResult::Ok) return result;
        if (connected) {
            const std::uint32_t lines[] = {126, 145, 163};
            const char* expressions[] = {"!_ethernet->connected()", "!_wifi->connected()", "!_wifiSecure->connected()"};
            result = assertion(lines[index], expressions[index]); if (result != NetworkResult::Ok) return result;
        }
    }
    if (index == 0) {
        const auto result = perform(ClientOperation::PrepareEthernet, type, handles[index], secure);
        if (result != NetworkResult::Ok) return result;
    }
    output = handles[index]; selected = kind; accepted = true;
    return NetworkResult::Ok;
}

bool EthernetSocket::ready() {
    if (callback_result != NetworkResult::Ok) return false;
    if (!services.perform || !services.millis || !services.delay_ms) {
        callback_result = NetworkResult::MissingService; return false;
    }
    return true;
}
std::int32_t EthernetSocket::perform(const SocketAction& action) {
    if (!ready()) return -1;
    std::int32_t result = 0;
    if (!services.perform(services.context, action, result)) {
        callback_result = NetworkResult::ServiceFailure; return -1;
    }
    return result;
}
std::int32_t EthernetSocket::operation(SocketOperation op) { return perform(SocketAction(op, socket)); }
int EthernetSocket::status() { return socket == 8 ? 0 : operation(SocketOperation::Status); }
int EthernetSocket::connect(std::uint32_t address, std::uint16_t port, std::uint16_t& source_port) {
    if (!ready()) return -1;
    if (socket != 8) return -30;
    for (std::uint8_t i = 0; i < 8; ++i) {
        const int state = perform(SocketAction(SocketOperation::Status, i));
        if (callback_result != NetworkResult::Ok) return -1;
        if (state == 0 || state == 0x18 || state == 0x1c) { socket = i; break; }
    }
    if (socket == 8) return -31;
    if (++source_port == 0) source_port = 1024;
    SocketAction action(SocketOperation::Open, socket); action.argument = source_port;
    perform(action);
    if (callback_result != NetworkResult::Ok) return -1;
    action.operation = SocketOperation::Connect; action.argument = address; action.second = port;
    if (!perform(action)) { socket = 8; return -32; }
    while (callback_result == NetworkResult::Ok && status() != 0x17) {
        services.delay_ms(services.context, 1);
        if (status() == 0) { socket = 8; return -33; }
    }
    if (callback_result != NetworkResult::Ok) return -1;
    if (!secure) return 1;
    tls_active = true;
    return operation(SocketOperation::TlsStart);
}
bool EthernetSocket::connected() {
    if (socket == 8) return false;
    const int state = status();
    return state != 0x14 && state != 0 && state != 0x18 && (state != 0x1c || available() != 0);
}
std::size_t EthernetSocket::raw_write(const std::uint8_t* data, std::size_t size) {
    SocketAction action(SocketOperation::Send, socket); action.data = data; action.size = size & 0xffff;
    if (socket == 8 || !perform(action)) { write_error = true; return 0; }
    return size;
}
int EthernetSocket::raw_read(std::uint8_t* data, std::size_t size) {
    SocketAction action(SocketOperation::Receive, socket); action.destination = data;
    action.size = static_cast<std::int16_t>(size); return perform(action);
}
std::size_t EthernetSocket::write(const std::uint8_t* data, std::size_t size) {
    if (!data && size) { callback_result = NetworkResult::InvalidInput; return 0; }
    if (!tls_active) return raw_write(data, size);
    SocketAction action(SocketOperation::TlsWrite, socket); action.data = data; action.size = size & 0xffff;
    int result;
    do { result = perform(action); if (result > 0) return result; }
    while (callback_result == NetworkResult::Ok && (result == -0x6900 || result == -0x6880));
    action.operation = SocketOperation::TlsError; action.size = 0; action.data = nullptr; action.argument = static_cast<std::uint32_t>(result);
    return static_cast<std::size_t>(perform(action));
}
int EthernetSocket::read(std::uint8_t* data, std::size_t size) {
    if (!data && size) { callback_result = NetworkResult::InvalidInput; return -1; }
    if (!tls_active) return raw_read(data, size);
    SocketAction action(SocketOperation::TlsRead, socket); action.destination = data; action.size = size & 0xffff;
    return perform(action);
}
int EthernetSocket::read() {
    std::uint8_t byte = 0;
    if (read(&byte, 1) > 0) return byte;
    operation(SocketOperation::BadRead); return -1;
}
int EthernetSocket::available() {
    if (!tls_active) return socket == 8 ? 0 : operation(SocketOperation::Available);
    SocketAction action(SocketOperation::TlsRead, socket); const int result = perform(action);
    const int count = operation(SocketOperation::TlsAvailable);
    if (result < 0 && result != -0x6900 && result != -0x6880) {
        action.operation = SocketOperation::TlsError; action.size = 0; action.data = nullptr; action.argument = static_cast<std::uint32_t>(result); return perform(action);
    }
    return count;
}
int EthernetSocket::peek() {
    if (!available()) return -1;
    // The OEM peeks the raw socket even when TLS is active.
    return operation(SocketOperation::Peek);
}
void EthernetSocket::flush() { operation(SocketOperation::Flush); }
void EthernetSocket::stop() {
    if (socket == 8 || !ready()) return;
    operation(SocketOperation::Disconnect);
    const std::uint32_t start = services.millis(services.context);
    while (callback_result == NetworkResult::Ok && status() != 0 &&
           static_cast<std::uint32_t>(services.millis(services.context) - start) < 1000)
        services.delay_ms(services.context, 1);
    if (callback_result != NetworkResult::Ok) return;
    if (status() != 0) operation(SocketOperation::Close);
    operation(SocketOperation::ClearServerPort); socket = 8;
    operation(SocketOperation::TlsStop); tls_active = false;
}
}} // namespace zseries::oem
