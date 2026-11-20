#include "pico_c/energia_network.h"
#include <WiFi.h>
#include <SLFS.h>
#include <string.h>
extern "C" {
#include <driverlib/prcm.h>
#include <driverlib/shamd5.h>
#include <inc/hw_memmap.h>
}

static PicoHttpClient &client(void *context)
{
    return *static_cast<PicoEnergiaNetwork *>(context)->client;
}
static PicoIpv4 unpack(IPAddress address)
{
    PicoIpv4 result = {{address[0], address[1], address[2], address[3]}};
    return result;
}
static IPAddress pack(PicoIpv4 address)
{
    return IPAddress(address.octet);
}
static uint32_t clock_ms(void *)
{
    return millis();
}
static void delay_ms(void *, uint32_t value)
{
    delay(value);
}
static void serial_write(void *, const char *text)
{
    Serial.print(text);
}
static void serial_write_float(void *, double value, unsigned digits)
{
    Serial.print(static_cast<float>(value), static_cast<int>(digits));
}
static bool initialized(void *)
{
    return WiFiClass::_initialized;
}
static bool initialize(void *)
{
    return WiFiClass::init();
}
static bool connecting(void *)
{
    return WiFiClass::_connecting;
}
static int status(void *)
{
    return WiFi.status();
}
static int begin(void *, const char *ssid, const uint8_t *password, uint8_t security, bool)
{
    if (WiFiClass::_connecting) {
        delay(500);
        return WiFi.status();
    }
    if (!WiFiClass::init())
        return WL_CONNECT_FAILED;
    sl_WlanProfileDel(0xff);
    WiFi.setIpDefaults();
    /* Original fast reconnect policy is 2; upstream begin() enables 3. */
    sl_WlanPolicySet(SL_POLICY_CONNECTION, SL_CONNECTION_POLICY(0, 1, 0, 0, 0), NULL, 0);
    SlSecParams_t parameters = {};
    if (security < 7) {
        parameters.Type = security == 5 ? SL_SEC_TYPE_WEP : SL_SEC_TYPE_WPA;
        parameters.Key = reinterpret_cast<signed char *>(const_cast<uint8_t *>(password));
        parameters.KeyLen = strlen(reinterpret_cast<const char *>(password));
    }
    int16_t length = strlen(ssid);
    if (sl_WlanConnect(reinterpret_cast<const signed char *>(ssid), length, NULL, &parameters,
                       NULL) != 0)
        return WL_CONNECT_FAILED;
    sl_WlanProfileAdd(reinterpret_cast<const signed char *>(ssid), length, NULL, &parameters, NULL,
                      6, 0);
    WiFiClass::_connecting = true;
    return WiFi.status();
}
static void disconnect(void *)
{
    WiFi.disconnect();
}
static PicoIpv4 local_ip(void *)
{
    return unpack(WiFi.localIP());
}
static int resolve(void *, const char *hostname, PicoIpv4 *result)
{
    IPAddress address;
    int success = WiFi.hostByName(const_cast<char *>(hostname), address);
    *result = unpack(address);
    return success;
}
static void dns(void *, PicoIpv4 address)
{
    WiFi.setDNS(pack(address));
}
static int scan(void *)
{
    return WiFi.scanNetworks();
}
static const char *ssid(void *, unsigned index)
{
    return WiFi.SSID(static_cast<uint8_t>(index));
}
static uint8_t security(void *, unsigned index)
{
    return WiFi.encryptionType(static_cast<uint8_t>(index));
}
static int32_t rssi(void *, unsigned index)
{
    return WiFi.RSSI(static_cast<uint8_t>(index));
}
static void mac(void *, uint8_t address[6])
{
    WiFi.macAddress(address);
}

static volatile bool hash_ready;
static void hash_interrupt()
{
    const uint32_t status = SHAMD5IntStatus(SHAMD5_BASE, true);
    if (status & 8) {
        SHAMD5IntDisable(SHAMD5_BASE, 8);
        hash_ready = true;
    }
    if (status & 4)
        SHAMD5IntDisable(SHAMD5_BASE, 4);
    if (status & 2)
        SHAMD5IntDisable(SHAMD5_BASE, 2);
    if (status & 1)
        SHAMD5IntDisable(SHAMD5_BASE, 1);
}
static bool md5(void *, const uint8_t *data, size_t length, uint8_t digest[16], uint32_t started)
{
    PRCMPeripheralClkEnable(PRCM_DTHE, PRCM_RUN_MODE_CLK);
    SHAMD5IntRegister(SHAMD5_BASE, hash_interrupt);
    SHAMD5IntEnable(SHAMD5_BASE, 0xf);
    while (!hash_ready) {
        if (static_cast<uint32_t>(millis() - started) >= 10001u)
            return false;
    }
    hash_ready = false;
    SHAMD5ConfigSet(SHAMD5_BASE, SHAMD5_ALGO_MD5);
    bool success = SHAMD5DataProcess(SHAMD5_BASE, const_cast<uint8_t *>(data), length, digest);
    SHAMD5IntDisable(SHAMD5_BASE, 0xf);
    return success;
}
static int connect(void *context, PicoIpv4 address, uint16_t port, bool background)
{
    return client(context).connectPico(pack(address), port, background);
}
static int connected(void *context)
{
    return client(context).status();
}
static int available(void *context)
{
    return client(context).available();
}
static int read(void *context)
{
    return client(context).read();
}
static size_t write(void *context, const uint8_t *data, size_t length)
{
    return client(context).write(data, length);
}
static void flush(void *context)
{
    client(context).flush();
}
static void stop(void *context)
{
    client(context).stop();
}
static void file_begin(void *)
{
    SerFlash.begin();
}
static bool file_open(void *, const char *name, uint32_t mode)
{
    return SerFlash.open(name, static_cast<int32_t>(mode)) == SL_FS_OK;
}
static int file_read(void *)
{
    return SerFlash.read();
}
static int32_t file_write(void *, const uint8_t *data, size_t length)
{
    return SerFlash.write(data, length);
}
static bool file_close(void *)
{
    return SerFlash.close() == SL_FS_OK;
}
static int file_remove(void *, const char *name)
{
    return SerFlash.del(name);
}

static int boot_read(void *, uint8_t data[8])
{
    _i32 handle = -1;
    _u32 token = 0;
    if (sl_FsOpen(reinterpret_cast<const _u8 *>("/sys/mcubootinfo.bin"), FS_MODE_OPEN_READ, &token,
                  &handle) != 0)
        return -1;
    int count = sl_FsRead(handle, 0, data, 8);
    int closed = sl_FsClose(handle, NULL, NULL, 0);
    return count == 8 && closed == 0 ? 0 : -1;
}
static int boot_write(void *, const uint8_t data[8])
{
    _i32 handle = -1;
    _u32 token = 0;
    if (sl_FsOpen(reinterpret_cast<const _u8 *>("/sys/mcubootinfo.bin"), FS_MODE_OPEN_WRITE, &token,
                  &handle) != 0)
        return -1;
    int count = sl_FsWrite(handle, 0, const_cast<_u8 *>(data), 8);
    int closed = sl_FsClose(handle, NULL, NULL, 0);
    return count == 8 && closed == 0 ? 0 : -1;
}
static void reset(void *)
{
    PRCMMCUReset(true);
}
static uint8_t ping(void *)
{
    SlPingStartCommand_t command = {};
    SlPingReport_t report = {};
    command.PingIntervalTime = 25;
    command.PingSize = 56;
    command.PingRequestTimeout = 1000;
    command.TotalNumberOfAttempts = 5;
    command.Flags = 0;
    command.Ip = 0x08080404;
    if (sl_NetAppPingStart(&command, SL_AF_INET, &report, NULL) < 0)
        return 0;
    return static_cast<uint8_t>(report.PacketsReceived);
}
static void ip_configuration(void *, PicoIpv4 *local, PicoIpv4 *dns, PicoIpv4 *gateway,
                             PicoIpv4 *mask)
{
    if (!WiFiClass::_initialized)
        WiFiClass::init();
    SlNetCfgIpV4Args_t config = {};
    _u8 length = sizeof(config);
    sl_NetCfgGet(SL_IPV4_STA_P2P_CL_GET_INFO, NULL, &length, reinterpret_cast<_u8 *>(&config));
    *local = unpack(IPAddress(sl_Htonl(config.ipV4)));
    *dns = unpack(IPAddress(sl_Htonl(config.ipV4DnsServer)));
    *gateway = unpack(IPAddress(sl_Htonl(config.ipV4Gateway)));
    *mask = unpack(IPAddress(sl_Htonl(config.ipV4Mask)));
}

const PicoNetworkPlatform *pico_energia_network_platform()
{
    static const PicoNetworkPlatform platform = {
        clock_ms,    delay_ms,   serial_write, initialized, initialize, connecting,
        status,      begin,      disconnect,   local_ip,    resolve,    dns,
        scan,        ssid,       security,     rssi,        mac,        md5,
        connect,     connected,  available,    read,        write,      flush,
        stop,        file_begin, file_open,    file_read,   file_write, file_close,
        file_remove, boot_read,  boot_write,   reset,       ping,       ip_configuration,
        serial_write_float};
    return &platform;
}
