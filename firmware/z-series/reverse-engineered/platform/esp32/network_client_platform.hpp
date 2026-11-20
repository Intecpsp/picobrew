#pragma once
#include "zseries/network_client.hpp"
#include "http_client_platform.hpp"
#include <Client.h>
#include <SPI.h>
#include <Print.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/x509_crt.h>

namespace zseries { namespace oem { namespace esp32 {
// The OEM ESP32Ethernet client adds TLS to a W5500 TCP socket. It is not the
// ordinary upstream EthernetClient. The portable object owns its socket policy.
class EthernetTransport : public Client {
public:
    EthernetTransport();
    ~EthernetTransport() override;
    EthernetTransport(const EthernetTransport&) = delete;
    EthernetTransport& operator=(const EthernetTransport&) = delete;
    EthernetSocket state;
    std::uint32_t handshake_timeout_ms = 120000;
    const char* ca_certificate = nullptr;
    Print* diagnostics = nullptr;
    int connect(IPAddress, std::uint16_t) override;
    int connect(const char*, std::uint16_t) override;
    std::size_t write(std::uint8_t) override;
    std::size_t write(const std::uint8_t*, std::size_t) override;
    int available() override;
    int read() override;
    int read(std::uint8_t*, std::size_t) override;
    int peek() override;
    void flush() override;
    void stop() override;
    std::uint8_t connected() override;
    operator bool() override;
    using Print::write;
private:
    friend bool ethernet_operation(void*, const SocketAction&, std::int32_t&);
    mbedtls_ssl_context ssl_;
    mbedtls_ssl_config config_;
    mbedtls_ctr_drbg_context random_;
    mbedtls_entropy_context entropy_;
    mbedtls_x509_crt certificate_;
    bool tls_initialized_ = false, certificate_initialized_ = false;
    const char* host_ = nullptr;
    static std::uint16_t source_port_;
    int start_tls();
    void free_tls();
    int tls_error(int);
    void log(const char*, unsigned, const char*, const char*);
    static int tls_send(void*, const unsigned char*, std::size_t);
    static int tls_receive(void*, unsigned char*, std::size_t);
};

class NetworkClientBinding {
public:
    explicit NetworkClientBinding(std::uint32_t& network, const ConsoleServices& diagnostics, Print* serial = nullptr);
    ~NetworkClientBinding();
    NetworkClientBinding(const NetworkClientBinding&) = delete;
    NetworkClientBinding& operator=(const NetworkClientBinding&) = delete;
    ClientFactory factory;
    HttpBindings http;
    std::uint32_t& network;
    Print* serial;
};

// System has already called SPI.begin(17,5,16,-1). This passes the same bus to
// the evidenced W5500 CS21 driver and uses the OEM DHCP response timeout5000.
int connect_ethernet(SPIClass&, const std::array<std::uint8_t, 6>& mac, std::uint32_t timeout_ms);
std::uint32_t ethernet_address();
void device_identity(std::uint64_t efuse_mac, std::array<char, 13>& identifier,
                     std::array<std::uint8_t, 6>& wifi, std::array<std::uint8_t, 6>& ethernet);
}}} // namespace zseries::oem::esp32
