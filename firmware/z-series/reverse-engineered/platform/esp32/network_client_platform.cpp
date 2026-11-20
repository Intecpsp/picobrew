#include "network_client_platform.hpp"
#include <Arduino.h>
#include <Ethernet2.h>
#include <Dns.h>
#include <utility/socket.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/error.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>
#include <new>

namespace zseries { namespace oem { namespace esp32 {
namespace {
std::uint32_t clock(void*) { return ::millis(); }
void delay_ms(void*, std::uint32_t value) { ::delay(value); }
}
std::uint16_t EthernetTransport::source_port_ = 1024;
bool ethernet_operation(void* opaque, const SocketAction& action, std::int32_t& result) {
    auto& self = *static_cast<EthernetTransport*>(opaque); result = 0;
    switch (action.operation) {
    case SocketOperation::Status: result = w5500.readSnSR(action.socket); break;
    case SocketOperation::Open: result = ::socket(action.socket, SnMR::TCP, action.argument, 0); break;
    case SocketOperation::Connect: {
        IPAddress address(action.argument); std::uint8_t bytes[4];
        for (unsigned i = 0; i < 4; ++i) bytes[i] = address[i];
        result = ::connect(action.socket, bytes, action.second); break;
    }
    case SocketOperation::Disconnect: ::disconnect(action.socket); break;
    case SocketOperation::Close: ::close(action.socket); break;
    case SocketOperation::ClearServerPort: EthernetClass::_server_port[action.socket] = 0; break;
    case SocketOperation::Send: result = ::send(action.socket, action.data, action.size); break;
    case SocketOperation::Receive: result = ::recv(action.socket, action.destination, static_cast<std::int16_t>(action.size)); break;
    case SocketOperation::Available: result = w5500.getRXReceivedSize(action.socket); break;
    case SocketOperation::Peek: { std::uint8_t byte = 0; ::peek(action.socket, &byte); result = byte; break; }
    case SocketOperation::Flush: ::flush(action.socket); break;
    case SocketOperation::TlsStart: result = self.start_tls(); break;
    case SocketOperation::TlsStop: self.free_tls(); break;
    case SocketOperation::TlsWrite: result = mbedtls_ssl_write(&self.ssl_, action.data, action.size); break;
    case SocketOperation::TlsRead: result = mbedtls_ssl_read(&self.ssl_, action.destination, action.size); break;
    case SocketOperation::TlsAvailable: result = mbedtls_ssl_get_bytes_avail(&self.ssl_); break;
    case SocketOperation::TlsError: result = self.tls_error(static_cast<std::int32_t>(action.argument)); break;
    case SocketOperation::BadRead: if (self.diagnostics) self.diagnostics->print("BAD"); break;
    }
    return true;
}
EthernetTransport::EthernetTransport() {
    state.services.context = this; state.services.millis = clock;
    state.services.delay_ms = delay_ms; state.services.perform = ethernet_operation;
}
EthernetTransport::~EthernetTransport() { stop(); free_tls(); }
void EthernetTransport::log(const char* severity, unsigned line, const char* function, const char* message) {
    if (!diagnostics) return;
    char text[720];
    ::snprintf(text, sizeof(text), "[%s][EthernetClient.h:%u] %s(): %s\r\n", severity, line, function, message);
    diagnostics->print(text);
}
void EthernetTransport::free_tls() {
    if (!tls_initialized_) return;
    tls_initialized_ = false;
    mbedtls_ssl_free(&ssl_); mbedtls_ssl_config_free(&config_);
    mbedtls_ctr_drbg_free(&random_); mbedtls_entropy_free(&entropy_);
    if (certificate_initialized_) { mbedtls_x509_crt_free(&certificate_); certificate_initialized_ = false; }
}
int EthernetTransport::tls_error(int error) {
    char text[100]; ::snprintf(text, sizeof(text), "MbedTLS message code: %d", error);
    log("E", 97, "handle_error", text);
    if (error != -0x7880) { mbedtls_strerror(error, text, sizeof(text)); log("E", 102, "handle_error", text); }
    free_tls(); return error;
}
int EthernetTransport::tls_send(void* opaque, const unsigned char* data, std::size_t size) {
    return static_cast<EthernetTransport*>(opaque)->state.raw_write(data, size);
}
int EthernetTransport::tls_receive(void* opaque, unsigned char* data, std::size_t size) {
    const int result = static_cast<EthernetTransport*>(opaque)->state.raw_read(data, size);
    return result == -1 ? MBEDTLS_ERR_SSL_WANT_READ : result;
}
int EthernetTransport::start_tls() {
    if (!tls_initialized_) {
        tls_initialized_ = true;
        mbedtls_ssl_init(&ssl_); mbedtls_ssl_config_init(&config_);
        mbedtls_ctr_drbg_init(&random_); mbedtls_entropy_init(&entropy_);
    } else log("E", 64, "tlsInit", "isTlsInitialized is true");
    const unsigned char personalization[] = "esp32-tls";
    int result = mbedtls_ctr_drbg_seed(&random_, mbedtls_entropy_func, &entropy_, personalization, 9);
    if (result < 0) return tls_error(result);
    result = mbedtls_ssl_config_defaults(&config_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) return tls_error(result);
    if (!ca_certificate) {
        mbedtls_ssl_conf_authmode(&config_, MBEDTLS_SSL_VERIFY_NONE);
        log("I", 176, "start_ssl_client", "WARNING: Use certificates for a more secure communication!");
    } else {
        mbedtls_x509_crt_init(&certificate_); certificate_initialized_ = true;
        mbedtls_ssl_conf_authmode(&config_, MBEDTLS_SSL_VERIFY_REQUIRED);
        result = mbedtls_x509_crt_parse(&certificate_, reinterpret_cast<const unsigned char*>(ca_certificate), std::strlen(ca_certificate) + 1);
        mbedtls_ssl_conf_ca_chain(&config_, &certificate_, nullptr);
        if (result < 0) return tls_error(result);
    }
    result = mbedtls_ssl_set_hostname(&ssl_, host_);
    if (result != 0) return tls_error(result);
    mbedtls_ssl_conf_rng(&config_, mbedtls_ctr_drbg_random, &random_);
    result = mbedtls_ssl_setup(&ssl_, &config_);
    if (result != 0) return tls_error(result);
    mbedtls_ssl_set_bio(&ssl_, this, tls_send, tls_receive, nullptr);
    const std::uint32_t started = ::millis();
    while ((result = mbedtls_ssl_handshake(&ssl_)) != 0) {
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) return tls_error(result);
        if (static_cast<std::uint32_t>(::millis() - started) > handshake_timeout_ms) {
            log("W", 234, "start_ssl_client", "SSL Handshake timeout"); return tls_error(-1);
        }
        vTaskDelay(10);
    }
    const auto verification = mbedtls_ssl_get_verify_result(&ssl_);
    if (verification != 0) {
        char details[512] = {}; mbedtls_x509_crt_verify_info(details, sizeof(details), "  ! ", verification);
        char text[600]; ::snprintf(text, sizeof(text), "Failed to verify peer certificate! verification info: %s", details);
        log("E", 258, "start_ssl_client", text);
        // The original stops the transport, frees TLS and returns the last
        // handshake code (zero), not a fabricated certificate error number.
        state.stop(); free_tls(); return tls_error(result);
    }
    if (certificate_initialized_) { mbedtls_x509_crt_free(&certificate_); certificate_initialized_ = false; }
    return 1;
}
int EthernetTransport::connect(IPAddress address, std::uint16_t port) { return state.connect(static_cast<std::uint32_t>(address), port, source_port_); }
int EthernetTransport::connect(const char* host, std::uint16_t port) {
    if (!host) { state.callback_result = NetworkResult::InvalidInput; return 0; }
    host_ = host; DNSClient dns; IPAddress remote; dns.begin(Ethernet.dnsServerIP());
    const int result = dns.getHostByName(host, remote);
    if (result == 1) return connect(remote, port);
    if (diagnostics) { diagnostics->print("[E][EthernetClient.cpp:50] connect(): FAILED dns.getHostByName "); diagnostics->println(host); }
    return result;
}
std::size_t EthernetTransport::write(std::uint8_t byte) { return write(&byte, 1); }
std::size_t EthernetTransport::write(const std::uint8_t* data, std::size_t size) { const auto result = state.write(data, size); if (state.write_error) setWriteError(); return result; }
int EthernetTransport::read(std::uint8_t* data, std::size_t size) { return state.read(data, size); }
int EthernetTransport::read() { return state.read(); }
int EthernetTransport::available() { return state.available(); }
int EthernetTransport::peek() { return state.peek(); }
void EthernetTransport::flush() { state.flush(); }
void EthernetTransport::stop() { state.stop(); }
std::uint8_t EthernetTransport::connected() { return state.connected(); }
EthernetTransport::operator bool() { return state.socket != 8; }

namespace {
bool factory_operation(void* opaque, const ClientAction& action, std::uintptr_t& result) {
    auto& binding = *static_cast<NetworkClientBinding*>(opaque);
    auto* client = reinterpret_cast<Client*>(action.handle); result = 0;
    switch (action.operation) {
    case ClientOperation::Create:
        switch (action.kind) {
        case HttpClientClass::Ethernet: { auto* ethernet = new (std::nothrow) EthernetTransport; if (ethernet) ethernet->diagnostics = binding.serial; client = ethernet; break; }
        case HttpClientClass::Wifi: client = new (std::nothrow) WiFiClient; break;
        case HttpClientClass::WifiSecure: client = new (std::nothrow) WiFiClientSecure; break;
        default: return false;
        }
        result = reinterpret_cast<std::uintptr_t>(client); return client != nullptr;
    case ClientOperation::Stop: if (!client) return false; client->stop(); break;
    case ClientOperation::Destroy: delete client; break;
    case ClientOperation::Connected: if (!client) return false; result = client->connected(); break;
    case ClientOperation::Timeout:
        if (action.kind == HttpClientClass::Ethernet) static_cast<EthernetTransport*>(client)->handshake_timeout_ms = action.argument;
        else if (action.kind == HttpClientClass::WifiSecure) static_cast<WiFiClientSecure*>(client)->setHandshakeTimeout(action.argument);
        else return false;
        break;
    case ClientOperation::PrepareEthernet:
        if (action.kind != HttpClientClass::Ethernet || !client) return false;
        static_cast<EthernetTransport*>(client)->state.secure = action.argument != 0;
        static_cast<EthernetTransport*>(client)->ca_certificate = nullptr; break;
    }
    return true;
}
bool acquire(void* opaque, bool secure, Client*& client, HttpClientClass& kind) {
    auto& binding = *static_cast<NetworkClientBinding*>(opaque); std::uintptr_t handle = 0; bool accepted = false;
    const auto result = binding.factory.acquire(binding.network, secure, handle, kind, accepted);
    client = reinterpret_cast<Client*>(handle); return result == NetworkResult::Ok && accepted;
}
bool prepare_ethernet(void* opaque, bool secure) {
    auto& binding = *static_cast<NetworkClientBinding*>(opaque);
    if (binding.factory.kind != HttpClientClass::Ethernet || !binding.factory.handles[0]) return false;
    auto* client = reinterpret_cast<EthernetTransport*>(binding.factory.handles[0]);
    client->state.secure = secure; client->ca_certificate = nullptr; return true;
}
}
NetworkClientBinding::NetworkClientBinding(std::uint32_t& mode, const ConsoleServices& diagnostics, Print* output)
    : network(mode), serial(output) {
    factory.services.context = this; factory.services.perform = factory_operation; factory.services.diagnostics = diagnostics;
    http.context = this; http.acquire = acquire; http.prepare_ethernet = prepare_ethernet;
}
NetworkClientBinding::~NetworkClientBinding() { factory.clear(); }
int connect_ethernet(SPIClass& bus, const std::array<std::uint8_t, 6>& mac, std::uint32_t timeout_ms) {
    auto address = mac; w5500.setSPI(bus); Ethernet.init(21); return Ethernet.begin(address.data(), timeout_ms, 5000);
}
std::uint32_t ethernet_address() { return static_cast<std::uint32_t>(Ethernet.localIP()); }
void device_identity(std::uint64_t efuse_mac, std::array<char, 13>& identifier,
                     std::array<std::uint8_t, 6>& wifi, std::array<std::uint8_t, 6>& ethernet) {
    for (unsigned i = 0; i < 6; ++i) wifi[i] = static_cast<std::uint8_t>(efuse_mac >> (i * 8));
    ethernet = wifi; ethernet[5] += 3;
    ::snprintf(identifier.data(), identifier.size(), "%02x%02x%02x%02x%02x%02x", wifi[0], wifi[1], wifi[2], wifi[3], wifi[4], wifi[5]);
}
}}} // namespace zseries::oem::esp32
