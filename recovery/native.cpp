/* Independent CC3200 recovery entry. No brewing controller or timer is linked.
 * Only fixed enum destinations can be written; the recovery image, loader,
 * service pack, keys, profiles and application data have no write path here. */
#include <Energia.h>
#include <stdio.h>
#include <string.h>
#include "picobrew/actuators.h"
#include "picobrew/http_server.h"
#include "recovery.h"
#include "http.h"
#include "web_assets.h"
extern "C" {
#include <utility/simplelink.h>
#include <utility/fs.h>
#include <utility/netapp.h>
#include <utility/netcfg.h>
#include <utility/nonos.h>
#include <utility/socket.h>
#include <utility/udma_if.h>
#include <utility/wlan.h>
#include <driverlib/prcm.h>
}

namespace {
const uint32_t station_wait_ms = 20000;
const uint32_t reset_drain_ms = 300;
const uint32_t upload_inactivity_ms = 30000;
const char boot_path[] = "/sys/mcubootinfo.bin";
const char key_path[] = "/local/api.key";
const uint32_t ap_address = 0xc0a80401;
PicoRecovery recovery;
PicoHttpServer server;
volatile bool nwp_started, associated;
volatile uint32_t ipv4;
int network_role = -1;
bool ap_ready, reset_waiting, reset_issued, offline_waiting;
uint32_t reset_at, offline_at, listener_attempt;
uint32_t handler_elapsed;
bool upload_clock_running;
uint32_t upload_progress_at, upload_session, upload_written;
PicoRecoveryFile upload_destination;
int listener = -1;
int32_t file_handle = -1;
uint32_t file_capacity;
bool file_writable;
char ap_ssid[24];

void erase_secret(void *memory, size_t length)
{
    volatile uint8_t *p = static_cast<volatile uint8_t *>(memory);
    while (length--) *p++ = 0;
}

void outputs_off()
{
    const uint8_t pins[] = {PICO_PIN_HEATER, PICO_PIN_DRAIN_PUMP,
                            PICO_PIN_SHUTTLE_PUMP, PICO_PIN_STEAM_PUMP};
    /* Set each output latch low before enabling its output direction. */
    for (size_t i = 0; i < sizeof(pins); ++i) {
        digitalWrite(pins[i], LOW);
        pinMode(pins[i], OUTPUT);
        digitalWrite(pins[i], LOW);
    }
}

const char *allowed_path(PicoRecoveryFile file)
{
    switch (file) {
    case PICO_RECOVERY_IMAGE2: return "/sys/mcuimg2.bin";
    case PICO_RECOVERY_IMAGE3: return "/sys/mcuimg3.bin";
    case PICO_RECOVERY_WEB_I: return "/local/web/i.gz";
    case PICO_RECOVERY_WEB_A: return "/local/web/a.gz";
    case PICO_RECOVERY_WEB_E: return "/local/web/e.gz";
    case PICO_RECOVERY_WEB_H: return "/local/web/h.gz";
    case PICO_RECOVERY_WEB_S: return "/local/web/s.gz";
    default: return NULL;
    }
}

uint32_t allowed_capacity(PicoRecoveryFile file)
{
    switch (file) {
    case PICO_RECOVERY_IMAGE2:
    case PICO_RECOVERY_IMAGE3: return 188416;
    case PICO_RECOVERY_WEB_I:
    case PICO_RECOVERY_WEB_A:
    case PICO_RECOVERY_WEB_H: return 7680;
    case PICO_RECOVERY_WEB_E:
    case PICO_RECOVERY_WEB_S: return 3584;
    default: return 0;
    }
}

int logical_read(const char *path, uint8_t *bytes, uint32_t length)
{
    if (!nwp_started || !path || !bytes) return -1;
    SlFsFileInfo_t info = {};
    if (sl_FsGetInfo(reinterpret_cast<const _u8 *>(path), 0, &info) != 0 ||
        info.FileLen != length) return -1;
    _i32 handle = -1;
    if (sl_FsOpen(reinterpret_cast<const _u8 *>(path), FS_MODE_OPEN_READ, NULL,
                  &handle) != 0) return -1;
    int count = sl_FsRead(handle, 0, bytes, length);
    int closed = sl_FsClose(handle, NULL, NULL, 0);
    return count == static_cast<int>(length) && closed == 0 ? 0 : -1;
}

int boot_read(void *, uint8_t bytes[8])
{
    return logical_read(boot_path, bytes, 8);
}

uint32_t record_magic(const uint8_t bytes[8])
{
    return static_cast<uint32_t>(bytes[4]) | (static_cast<uint32_t>(bytes[5]) << 8) |
           (static_cast<uint32_t>(bytes[6]) << 16) | (static_cast<uint32_t>(bytes[7]) << 24);
}

bool writes_allowed()
{
    uint8_t record[8];
    return boot_read(NULL, record) == 0 && record[0] == 0 &&
           record_magic(record) == PICO_RECOVERY_NORMAL_MAGIC;
}

int file_stat(void *, PicoRecoveryFile file, PicoRecoveryFileInfo *result)
{
    const char *path = allowed_path(file);
    if (!nwp_started || !path || !result) return -1;
    SlFsFileInfo_t info = {};
    int rc = sl_FsGetInfo(reinterpret_cast<const _u8 *>(path), 0, &info);
    if (rc == SL_FS_ERR_FILE_NOT_EXISTS) return 1;
    if (rc != 0) return -1;
    result->size = info.FileLen;
    result->allocated = info.AllocatedLen;
    return 0;
}

int32_t open_file(PicoRecoveryFile file, bool write, bool create, uint32_t maximum)
{
    const char *path = allowed_path(file);
    if (!nwp_started || !path || file_handle >= 0 || (write && !writes_allowed()))
        return -1;
    PicoRecoveryFileInfo info = {};
    int exists = file_stat(NULL, file, &info);
    _u32 mode = write ? FS_MODE_OPEN_WRITE : FS_MODE_OPEN_READ;
    uint32_t capacity = allowed_capacity(file);
    if (create) {
        /* Application slots are never created or resized. Browser assets are
         * unmirrored and fit their fixed 2/2/1/2/1-block reservation. */
        if (!write || file < PICO_RECOVERY_WEB_I || exists != 1 || maximum != capacity)
            return -1;
        mode = FS_MODE_OPEN_CREATE(maximum, 0);
    } else {
        if (exists != 0) return -1;
        if (write && info.allocated < capacity) capacity = info.allocated;
        if (!write) capacity = info.size;
    }
    _i32 handle = -1;
    if (sl_FsOpen(reinterpret_cast<const _u8 *>(path), mode, NULL, &handle) != 0 || handle < 0)
        return -1;
    file_handle = handle;
    file_capacity = capacity;
    file_writable = write;
    return handle;
}

int32_t file_open_read(void *, PicoRecoveryFile file) { return open_file(file, false, false, 0); }
int32_t file_open_write(void *, PicoRecoveryFile file) { return open_file(file, true, false, 0); }
int32_t file_create(void *, PicoRecoveryFile file, uint32_t maximum)
{
    return open_file(file, true, true, maximum);
}

bool valid_io(int32_t handle, uint32_t offset, const void *bytes, size_t length)
{
    return nwp_started && handle >= 0 && handle == file_handle && bytes && length > 0 &&
           length <= PICO_RECOVERY_CHUNK_MAX && offset <= file_capacity &&
           length <= file_capacity - offset;
}

int32_t file_read(void *, int32_t handle, uint32_t offset, uint8_t *bytes, size_t length)
{
    if (file_writable || !valid_io(handle, offset, bytes, length)) return -1;
    return sl_FsRead(handle, offset, bytes, static_cast<_u32>(length));
}

int32_t file_write(void *, int32_t handle, uint32_t offset, const uint8_t *bytes, size_t length)
{
    if (!file_writable || !valid_io(handle, offset, bytes, length) || !writes_allowed())
        return -1;
    return sl_FsWrite(handle, offset, const_cast<_u8 *>(bytes), static_cast<_u32>(length));
}

int file_close(void *, int32_t handle)
{
    if (handle < 0 || handle != file_handle) return -1;
    file_handle = -1;
    file_capacity = 0;
    file_writable = false;
    return nwp_started && sl_FsClose(handle, NULL, NULL, 0) == 0 ? 0 : -1;
}

int boot_write(void *, const uint8_t next[8])
{
    uint8_t prior[8];
    if (!next || file_handle >= 0 || boot_read(NULL, prior) != 0 || prior[0] != 0 ||
        record_magic(prior) != PICO_RECOVERY_NORMAL_MAGIC ||
        memcmp(prior + 1, next + 1, 3) != 0) return -1;
    uint32_t magic = record_magic(next);
    if (!((magic == PICO_RECOVERY_NORMAL_MAGIC && (next[0] == 1 || next[0] == 2)) ||
          (magic == PICO_RECOVERY_TRIAL_MAGIC && next[0] == 0))) return -1;
    _i32 handle = -1;
    if (sl_FsOpen(reinterpret_cast<const _u8 *>(boot_path), FS_MODE_OPEN_WRITE, NULL,
                  &handle) != 0) return -1;
    int count = sl_FsWrite(handle, 0, const_cast<_u8 *>(next), 8);
    int closed = sl_FsClose(handle, NULL, NULL, 0);
    return count == 8 && closed == 0 ? 0 : -1;
}

const PicoRecoveryOps storage = {file_stat, file_open_read, file_open_write, file_create,
                                file_read, file_write, file_close, boot_read, boot_write};

bool nonblocking(int descriptor)
{
    SlSockNonblocking_t option = {};
    option.NonblockingEnabled = 1;
    return sl_SetSockOpt(descriptor, SL_SOL_SOCKET, SL_SO_NONBLOCKING, &option,
                         sizeof(option)) == 0;
}

int accept_client(void *)
{
    if (listener < 0 || recovery.boot_committed) return PICO_HTTP_IO_AGAIN;
    SlSockAddrIn_t peer = {};
    SlSocklen_t size = sizeof(peer);
    int client = sl_Accept(listener, reinterpret_cast<SlSockAddr_t *>(&peer), &size);
    if (client == SL_EAGAIN) return PICO_HTTP_IO_AGAIN;
    if (client < 0) {
        sl_Close(listener);
        listener = -1;
        listener_attempt = millis();
        return PICO_HTTP_IO_ERROR;
    }
    if (!nonblocking(client)) {
        sl_Close(client);
        return PICO_HTTP_IO_ERROR;
    }
    return client;
}

int receive_client(void *, int client, uint8_t *bytes, size_t length)
{
    int result = sl_Recv(client, bytes, static_cast<_i16>(length), 0);
    return result == SL_EAGAIN ? PICO_HTTP_IO_AGAIN : result < 0 ? PICO_HTTP_IO_ERROR : result;
}

int send_client(void *, int client, const uint8_t *bytes, size_t length)
{
    int result = sl_Send(client, bytes, static_cast<_i16>(length), 0);
    return result == SL_EAGAIN ? PICO_HTTP_IO_AGAIN : result < 0 ? PICO_HTTP_IO_ERROR : result;
}

void close_client(void *, int client)
{
    if (nwp_started) sl_Close(client);
}

const PicoHttpIo socket_io = {accept_client, receive_client, send_client, close_client};

bool handle_request(const PicoHttpRequest *request, PicoHttpResponse *response,
                    char *arena, size_t capacity, void *context)
{
    uint32_t before = millis();
    bool handled = pico_recovery_http_handler(request, response, arena, capacity, context);
    handler_elapsed += static_cast<uint32_t>(millis() - before);
    return handled;
}

void poll_http(uint32_t now)
{
    handler_elapsed = 0;
    pico_http_server_poll(&server, now);
    /* A full image hash may take time. Its synchronous filesystem work is not
     * a slow-client response timeout: start that deadline after the handler. */
    if (server.client >= 0 && server.response.status != 0)
        server.started_ms += handler_elapsed;
}

void close_listener()
{
    if (listener >= 0 && nwp_started) sl_Close(listener);
    listener = -1;
}

bool start_listener()
{
    if (!nwp_started || sl_NetAppStop(SL_NET_APP_HTTP_SERVER_ID) != 0) return false;
    int descriptor = sl_Socket(SL_AF_INET, SL_SOCK_STREAM, SL_IPPROTO_TCP);
    if (descriptor < 0) return false;
    SlSockAddrIn_t address = {};
    address.sin_family = SL_AF_INET;
    address.sin_port = sl_Htons(80);
    if (!nonblocking(descriptor) ||
        sl_Bind(descriptor, reinterpret_cast<SlSockAddr_t *>(&address), sizeof(address)) != 0 ||
        sl_Listen(descriptor, 1) != 0) {
        sl_Close(descriptor);
        return false;
    }
    listener = descriptor;
    return true;
}

void load_key()
{
    uint8_t key[PICO_HTTP_KEY_LENGTH + 2] = {};
    SlFsFileInfo_t info = {};
    if (sl_FsGetInfo(reinterpret_cast<const _u8 *>(key_path), 0, &info) == 0 &&
        info.FileLen >= PICO_HTTP_KEY_LENGTH && info.FileLen <= sizeof(key) &&
        logical_read(key_path, key, info.FileLen) == 0)
        pico_http_server_set_key(&server, reinterpret_cast<const char *>(key), info.FileLen);
    erase_secret(key, sizeof(key));
}

bool restart_network(int role)
{
    nwp_started = false;
    if (sl_Stop(200) != 0) return false;
    associated = false;
    ipv4 = 0;
    network_role = role;
    int result = sl_Start(NULL, NULL, NULL);
    nwp_started = result >= 0;
    if (result != role) return false;
    /* Energia's NWP web server can become enabled again after a restart. */
    return sl_NetAppStop(SL_NET_APP_HTTP_SERVER_ID) == 0;
}

bool start_station()
{
    UDMAInit();
    int role = sl_Start(NULL, NULL, NULL);
    nwp_started = role >= 0;
    if (!nwp_started) return false;
    if (sl_NetAppStop(SL_NET_APP_HTTP_SERVER_ID) != 0 ||
        sl_WlanPolicySet(SL_POLICY_CONNECTION, SL_CONNECTION_POLICY(0,0,0,0,0), NULL, 0) != 0)
        return false;
    /* No profile deletion or plaintext SSID/password reads. Restart applies
     * DHCP and discards stale association/address events before autoconnect. */
    sl_WlanDisconnect();
    _u8 enabled = 1;
    if (sl_WlanSetMode(ROLE_STA) != 0 ||
        sl_NetCfgSet(SL_IPV4_STA_P2P_CL_DHCP_ENABLE, IPCONFIG_MODE_ENABLE_IPV4, 1, &enabled) != 0 ||
        sl_WlanPolicySet(SL_POLICY_CONNECTION, SL_CONNECTION_POLICY(1,0,0,0,0), NULL, 0) != 0)
        return false;
    return restart_network(ROLE_STA);
}

bool online()
{
    return nwp_started && (ap_ready || (associated && ipv4 != 0));
}

bool start_access_point()
{
    /* The 64-character API bearer key remains required in the browser. Its
     * first 16 characters provide a separate 64-bit-entropy AP passphrase. */
    if (!nwp_started || !server.key[0] || file_handle >= 0) return false;
    _u8 mac[6] = {};
    _u8 length = sizeof(mac);
    if (sl_NetCfgGet(SL_MAC_ADDRESS_GET, NULL, &length, mac) != 0 || length != sizeof(mac))
        return false;
    snprintf(ap_ssid, sizeof(ap_ssid), "PicoRecovery-%02x%02x%02x", mac[3], mac[4], mac[5]);
    char password[17];
    memcpy(password, server.key, 16);
    password[16] = 0;
    _u8 security = SL_SEC_TYPE_WPA_WPA2;
    _u8 hidden = 0;
    _u8 channel = 6;
    SlNetCfgIpV4Args_t config = {};
    config.ipV4 = ap_address;
    config.ipV4Mask = 0xffffff00;
    config.ipV4Gateway = ap_address;
    config.ipV4DnsServer = ap_address;
    SlNetAppDhcpServerBasicOpt_t dhcp = {};
    dhcp.lease_time = 3600;
    dhcp.ipv4_addr_start = 0xc0a80402;
    dhcp.ipv4_addr_last = 0xc0a80410;
    bool configured =
        sl_WlanPolicySet(SL_POLICY_CONNECTION, SL_CONNECTION_POLICY(0,0,0,0,0), NULL, 0) == 0 &&
        sl_WlanSet(SL_WLAN_CFG_AP_ID, WLAN_AP_OPT_SSID, strlen(ap_ssid),
                    reinterpret_cast<const _u8 *>(ap_ssid)) == 0 &&
        sl_WlanSet(SL_WLAN_CFG_AP_ID, WLAN_AP_OPT_SECURITY_TYPE, 1, &security) == 0 &&
        sl_WlanSet(SL_WLAN_CFG_AP_ID, WLAN_AP_OPT_PASSWORD, 16,
                    reinterpret_cast<const _u8 *>(password)) == 0 &&
        sl_WlanSet(SL_WLAN_CFG_AP_ID, WLAN_AP_OPT_HIDDEN_SSID, 1, &hidden) == 0 &&
        sl_WlanSet(SL_WLAN_CFG_AP_ID, WLAN_AP_OPT_CHANNEL, 1, &channel) == 0 &&
        sl_NetCfgSet(SL_IPV4_AP_P2P_GO_STATIC_ENABLE, IPCONFIG_MODE_ENABLE_IPV4,
                      sizeof(config), reinterpret_cast<const _u8 *>(&config)) == 0;
    erase_secret(password, sizeof(password));
    if (!configured) return false;
    /* A stopped DHCP server in STA mode is expected. Set and subsequent AP
     * restart are checked; no station profile is changed or removed. */
    sl_NetAppStop(SL_NET_APP_DHCP_SERVER_ID);
    if (sl_NetAppSet(SL_NET_APP_DHCP_SERVER_ID, NETAPP_SET_DHCP_SRV_BASIC_OPT,
                      sizeof(dhcp), reinterpret_cast<const _u8 *>(&dhcp)) != 0 ||
        sl_WlanSetMode(ROLE_AP) != 0 || !restart_network(ROLE_AP) ||
        sl_NetAppStart(SL_NET_APP_DHCP_SERVER_ID) != 0) return false;
    SlNetCfgIpV4Args_t actual = {};
    length = sizeof(actual);
    if (sl_NetCfgGet(SL_IPV4_AP_P2P_GO_GET_INFO, NULL, &length,
                      reinterpret_cast<_u8 *>(&actual)) != 0 || actual.ipV4 != ap_address)
        return false;
    ap_ready = true;
    ipv4 = actual.ipV4;
    Serial.println(ap_ssid);
    Serial.println("Recovery AP: http://192.168.4.1/");
    return true;
}

void service_network()
{
    if (nwp_started) _SlNonOsMainLoopTask();
}

void expire_idle_upload(uint32_t now)
{
    if (!recovery.upload_active || recovery.boot_committed) {
        upload_clock_running = false;
        return;
    }
    /* GET polling is not progress. Only a new upload/session/destination or a
     * changed persisted byte count extends the upload's inactivity deadline. */
    if (!upload_clock_running || recovery.session_id != upload_session ||
        recovery.upload_file != upload_destination || recovery.written != upload_written) {
        upload_clock_running = true;
        upload_session = recovery.session_id;
        upload_destination = recovery.upload_file;
        upload_written = recovery.written;
        upload_progress_at = now;
    } else if (server.client < 0 &&
               static_cast<uint32_t>(now - upload_progress_at) >= upload_inactivity_ms) {
        /* Abort closes the existing partial handle and clears its verified
         * state. It does not write payload, select firmware, retry or reboot.
         * In particular this releases the handle that blocks AP fallback. */
        (void)pico_recovery_abort(&recovery, recovery.session_id);
        upload_clock_running = false;
        Serial.println("Recovery upload expired; partial file remains unverified");
    }
}

void announce_station()
{
    char message[48];
    uint32_t address = ipv4;
    snprintf(message, sizeof(message), "Recovery: http://%lu.%lu.%lu.%lu/",
             static_cast<unsigned long>((address >> 24) & 255),
             static_cast<unsigned long>((address >> 16) & 255),
             static_cast<unsigned long>((address >> 8) & 255),
             static_cast<unsigned long>(address & 255));
    Serial.println(message);
}
} // namespace

/* These are the pinned SimpleLink driver's event entry points, intentionally
 * independent of WiFiClass and the brewing application's global state. */
extern "C" void SimpleLinkWlanEventHandler(SlWlanEvent_t *event)
{
    if (!event || network_role != ROLE_STA) return;
    if (event->Event == SL_WLAN_CONNECT_EVENT) {
        associated = true;
        ipv4 = 0;
    } else if (event->Event == SL_WLAN_DISCONNECT_EVENT) {
        associated = false;
        ipv4 = 0;
    }
}

extern "C" void SimpleLinkNetAppEventHandler(SlNetAppEvent_t *event)
{
    if (event && network_role == ROLE_STA && associated &&
        event->Event == SL_NETAPP_IPV4_IPACQUIRED_EVENT)
        ipv4 = event->EventData.ipAcquiredV4.ip;
}

extern "C" void SimpleLinkSockEventHandler(SlSockEvent_t *) {}
extern "C" void SimpleLinkHttpServerCallback(SlHttpServerEvent_t *, SlHttpServerResponse_t *) {}

void setup()
{
    outputs_off();
    Serial.begin(115200);
    Serial.println("Pico web recovery candidate; outputs off");
    pico_http_server_initialize(&server, &socket_io, NULL, handle_request,
                                &recovery, pico_recovery_assets, pico_recovery_assets_count);
    pico_recovery_init(&recovery, &storage, NULL, millis());
    if (!start_station()) {
        Serial.println("Recovery network initialization failed");
        return;
    }
    load_key();
    uint32_t started = millis();
    while (!online() && static_cast<uint32_t>(millis() - started) < station_wait_ms) {
        service_network();
        delay(10);
    }
    if (online()) announce_station();
    else if (!start_access_point())
        Serial.println("Recovery AP unavailable; existing valid API key required");
    listener_attempt = millis() - 1000;
}

void loop()
{
    service_network();
    uint32_t now = millis();
    expire_idle_upload(now);
    if (recovery.boot_committed) {
        close_listener();
        /* Do not accept another command after committing boot selection. The
         * existing response must finish (or reach its bounded HTTP deadline)
         * before allowing the network's final packets to drain and resetting. */
        if (server.client >= 0) poll_http(now);
        if (server.client < 0) {
            if (!reset_waiting) { reset_waiting = true; reset_at = now; }
            if (!reset_issued && static_cast<uint32_t>(now - reset_at) >= reset_drain_ms) {
                reset_issued = true;
                outputs_off();
                if (nwp_started) sl_Stop(200);
                nwp_started = false;
                PRCMMCUReset(true);
            }
        }
    } else if (online()) {
        offline_waiting = false;
        if (listener < 0 && static_cast<uint32_t>(now - listener_attempt) >= 1000) {
            listener_attempt = now;
            start_listener();
        }
        if (listener >= 0) poll_http(now);
    } else {
        pico_http_server_disconnect(&server);
        close_listener();
        if (!offline_waiting) { offline_waiting = true; offline_at = now; }
        if (nwp_started && !ap_ready && file_handle < 0 &&
            static_cast<uint32_t>(now - offline_at) >= station_wait_ms) {
            offline_at = now;
            start_access_point();
        }
    }
    delay(1);
}
