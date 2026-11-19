/* Driver/time fault injection around the actual recovery/native.cpp. */
#include <assert.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "recovery/native.cpp"

SerialClass Serial;
static uint32_t test_clock, nwp_start_at;
static int persisted_role, start_count, reset_count, mutation_count, next_file_handle;
static int fail_ap_option, fail_get_info, fail_write, fail_close;
static bool station_connects, station_leases, connected_event_sent, lease_event_sent;
static bool block_send, client_available;
static size_t send_limit, request_offset;
static uint32_t boot_read_delay;
static bool pin_output[32], pin_low[32];
static std::string diagnostic, received_request, transmitted_response, ap_password;
static std::vector<int> policies, mode_changes, closed_sockets;
struct File { std::vector<uint8_t> data; uint32_t allocated; };
struct Handle { std::string path; bool writable; };
static std::map<std::string, File> files;
static std::map<int, Handle> handles;
static std::map<std::string, unsigned> file_closes;
static SlNetCfgIpV4Args_t ap_config;
static SlNetAppDhcpServerBasicOpt_t dhcp_config;
static const char test_key[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

uint32_t millis() { return test_clock; }
void delay(uint32_t milliseconds) { test_clock += milliseconds; }
void SerialClass::begin(uint32_t baud) { assert(baud == 115200); }
void SerialClass::println(const char *text) { diagnostic += text; diagnostic += '\n'; }
void digitalWrite(uint8_t pin, uint8_t value)
{
    assert(pin < 32 && value == LOW);
    pin_low[pin] = true;
}
void pinMode(uint8_t pin, uint8_t mode)
{
    assert(pin < 32 && mode == OUTPUT && pin_low[pin]);
    pin_output[pin] = true;
}
static void assert_outputs_off()
{
    for (unsigned pin : {5, 8, 24, 27}) assert(pin_output[pin] && pin_low[pin]);
}
extern "C" void UDMAInit() { assert_outputs_off(); }
extern "C" void PRCMMCUReset(bool all) { assert(all); assert_outputs_off(); ++reset_count; }

extern "C" int sl_FsGetInfo(const _u8 *path, _u32 token, SlFsFileInfo_t *info)
{
    assert(token == 0);
    if (fail_get_info) return -99;
    auto found = files.find(reinterpret_cast<const char *>(path));
    if (found == files.end()) return SL_FS_ERR_FILE_NOT_EXISTS;
    *info = {};
    info->FileLen = found->second.data.size();
    info->AllocatedLen = found->second.allocated;
    return 0;
}
extern "C" int sl_FsOpen(const _u8 *raw, _u32 mode, _u32 *token, _i32 *handle)
{
    assert(!token);
    std::string path(reinterpret_cast<const char *>(raw));
    bool create = (mode & 0x10000000u) != 0;
    bool writable = mode == FS_MODE_OPEN_WRITE || create;
    if (create) {
        assert(path.find("/local/web/") == 0 && files.count(path) == 0);
        assert((mode & 0x0f000000u) == 0); // No mirror/secure flags.
        files[path] = {{}, mode & 0xffffu};
    } else if (!files.count(path)) return SL_FS_ERR_FILE_NOT_EXISTS;
    if (writable) {
        assert(path == "/sys/mcuimg2.bin" || path == "/sys/mcuimg3.bin" ||
               path == "/sys/mcubootinfo.bin" || path.find("/local/web/") == 0);
        files[path].data.clear();
        ++mutation_count;
    }
    *handle = next_file_handle++;
    handles[*handle] = {path, writable};
    return 0;
}
extern "C" int sl_FsRead(_i32 handle, _u32 offset, _u8 *data, _u32 length)
{
    assert(handles.count(handle) && !handles[handle].writable);
    File &file = files[handles[handle].path];
    if (handles[handle].path == boot_path) test_clock += boot_read_delay;
    if (offset > file.data.size()) return -1;
    size_t count = std::min<size_t>(length, file.data.size() - offset);
    memcpy(data, file.data.data() + offset, count);
    return static_cast<int>(count);
}
extern "C" int sl_FsWrite(_i32 handle, _u32 offset, _u8 *data, _u32 length)
{
    assert(handles.count(handle) && handles[handle].writable);
    if (fail_write) return -99;
    File &file = files[handles[handle].path];
    assert(offset <= file.allocated && length <= file.allocated - offset);
    if (file.data.size() < offset + length) file.data.resize(offset + length);
    memcpy(file.data.data() + offset, data, length);
    ++mutation_count;
    return static_cast<int>(length);
}
extern "C" int sl_FsClose(_i32 handle, const _u8 *certificate, const _u8 *signature, _u32 length)
{
    assert(!certificate && !signature && !length && handles.count(handle));
    ++file_closes[handles[handle].path];
    handles.erase(handle);
    return fail_close ? -99 : 0;
}

extern "C" int sl_SetSockOpt(_i16, _i16 level, _i16 option, const void *data, _u16 length)
{
    assert(level == SL_SOL_SOCKET && option == SL_SO_NONBLOCKING && length == sizeof(SlSockNonblocking_t));
    assert(static_cast<const SlSockNonblocking_t *>(data)->NonblockingEnabled == 1);
    return 0;
}
extern "C" int sl_Accept(_i16 descriptor, SlSockAddr_t *, SlSocklen_t *)
{
    assert(descriptor == 5);
    if (!client_available) return SL_EAGAIN;
    client_available = false;
    return 0; // Descriptor zero must work.
}
extern "C" int sl_Recv(_i16 client, void *data, _i16 length, _i16 flags)
{
    assert(client == 0 && flags == 0 && length > 0);
    if (request_offset == received_request.size()) return SL_EAGAIN;
    size_t count = std::min<size_t>(length, received_request.size() - request_offset);
    memcpy(data, received_request.data() + request_offset, count);
    request_offset += count;
    return static_cast<int>(count);
}
extern "C" int sl_Send(_i16 client, const void *data, _i16 length, _i16 flags)
{
    assert(client == 0 && flags == 0 && length > 0);
    if (block_send) return SL_EAGAIN;
    size_t count = std::min<size_t>(length, send_limit);
    transmitted_response.append(static_cast<const char *>(data), count);
    return static_cast<int>(count);
}
extern "C" int sl_Close(_i16 descriptor) { closed_sockets.push_back(descriptor); return 0; }
extern "C" int sl_Socket(_i16 family, _i16 type, _i16 protocol)
{
    assert(family == SL_AF_INET && type == SL_SOCK_STREAM && protocol == SL_IPPROTO_TCP);
    return 5;
}
extern "C" int sl_Bind(_i16 descriptor, const SlSockAddr_t *raw, _i16 length)
{
    auto address = reinterpret_cast<const SlSockAddrIn_t *>(raw);
    assert(descriptor == 5 && length == sizeof(*address) && address->sin_port == sl_Htons(80));
    return 0;
}
extern "C" int sl_Listen(_i16 descriptor, _i16 backlog) { assert(descriptor == 5 && backlog == 1); return 0; }
extern "C" _u16 sl_Htons(_u16 value) { return static_cast<_u16>((value << 8) | (value >> 8)); }
extern "C" int sl_NetAppStop(_u32 id)
{
    assert(id == SL_NET_APP_HTTP_SERVER_ID || id == SL_NET_APP_DHCP_SERVER_ID);
    return 0;
}
extern "C" int sl_NetAppStart(_u32 id) { assert(id == SL_NET_APP_DHCP_SERVER_ID); return 0; }
extern "C" int sl_NetAppSet(_u8 id, _u8 option, _u8 length, const _u8 *data)
{
    assert(id == SL_NET_APP_DHCP_SERVER_ID && option == NETAPP_SET_DHCP_SRV_BASIC_OPT && length == sizeof(dhcp_config));
    memcpy(&dhcp_config, data, length);
    return 0;
}
extern "C" int sl_NetCfgGet(_u8 id, _u8 *, _u8 *length, _u8 *data)
{
    if (id == SL_MAC_ADDRESS_GET) {
        assert(*length == 6);
        const uint8_t mac[] = {0x12, 0x34, 0x56, 0xab, 0xcd, 0xef};
        memcpy(data, mac, 6);
    } else {
        assert(id == SL_IPV4_AP_P2P_GO_GET_INFO && *length == sizeof(ap_config));
        memcpy(data, &ap_config, *length);
    }
    return 0;
}
extern "C" int sl_NetCfgSet(_u8 id, _u8 option, _u8 length, const _u8 *data)
{
    assert(option == IPCONFIG_MODE_ENABLE_IPV4);
    if (id == SL_IPV4_STA_P2P_CL_DHCP_ENABLE) assert(length == 1 && data[0] == 1);
    else {
        assert(id == SL_IPV4_AP_P2P_GO_STATIC_ENABLE && length == sizeof(ap_config));
        memcpy(&ap_config, data, length);
    }
    return 0;
}
extern "C" int sl_WlanPolicySet(_u8 id, _u8 policy, const _u8 *data, _u8 length)
{
    assert(id == SL_POLICY_CONNECTION && !data && !length);
    assert(policy == 0 || policy == 1); // Never fast-connect to a stale association.
    policies.push_back(policy);
    return 0;
}
static void wlan_event(uint32_t event)
{
    SlWlanEvent_t value = {event};
    SimpleLinkWlanEventHandler(&value);
}
static void ip_event(uint32_t address, uint32_t kind = SL_NETAPP_IPV4_IPACQUIRED_EVENT)
{
    SlNetAppEvent_t value = {};
    value.Event = kind;
    value.EventData.ipAcquiredV4.ip = address;
    SimpleLinkNetAppEventHandler(&value);
}
extern "C" int sl_WlanDisconnect() { wlan_event(SL_WLAN_DISCONNECT_EVENT); return 0; }
extern "C" int sl_WlanSetMode(_u8 role) { persisted_role = role; mode_changes.push_back(role); return 0; }
extern "C" int sl_WlanSet(_u16 id, _u16 option, _u16 length, const _u8 *data)
{
    assert(id == SL_WLAN_CFG_AP_ID);
    if (option == fail_ap_option) return -99;
    if (option == WLAN_AP_OPT_SECURITY_TYPE) assert(length == 1 && *data == SL_SEC_TYPE_WPA_WPA2);
    if (option == WLAN_AP_OPT_PASSWORD) ap_password.assign(reinterpret_cast<const char *>(data), length);
    if (option == WLAN_AP_OPT_SSID) assert(std::string(reinterpret_cast<const char *>(data), length) == "PicoRecovery-abcdef");
    return 0;
}
extern "C" int sl_Start(const void *, const void *, const void *)
{
    assert_outputs_off();
    ++start_count;
    nwp_start_at = test_clock;
    connected_event_sent = lease_event_sent = false;
    return persisted_role;
}
extern "C" int sl_Stop(_u16 timeout) { assert(timeout == 200); return 0; }
extern "C" int _SlNonOsMainLoopTask()
{
    if (persisted_role == ROLE_STA) {
        if (station_connects && !connected_event_sent && test_clock - nwp_start_at >= 100) {
            connected_event_sent = true;
            wlan_event(SL_WLAN_CONNECT_EVENT);
        }
        if (station_connects && station_leases && !lease_event_sent && test_clock - nwp_start_at >= 200) {
            lease_event_sent = true;
            ip_event(0x0a00018f);
        }
    }
    return 0;
}

static void fixture(bool key = true)
{
    test_clock = nwp_start_at = 0;
    persisted_role = ROLE_STA;
    start_count = reset_count = mutation_count = 0;
    next_file_handle = 0;
    fail_ap_option = -1;
    fail_get_info = fail_write = fail_close = 0;
    station_connects = station_leases = true;
    connected_event_sent = lease_event_sent = false;
    block_send = client_available = false;
    send_limit = 512;
    request_offset = boot_read_delay = 0;
    memset(pin_low, 0, sizeof(pin_low));
    memset(pin_output, 0, sizeof(pin_output));
    diagnostic.clear(); received_request.clear(); transmitted_response.clear(); ap_password.clear();
    policies.clear(); mode_changes.clear(); closed_sockets.clear(); files.clear(); handles.clear(); file_closes.clear();
    files[boot_path] = {{0, 0x3d, 0, 0x20, 0xba, 0xdc, 0xcd, 0xab}, 1024};
    if (key) files[key_path] = {std::vector<uint8_t>(test_key, test_key + 64), 1024};
    ap_config = {}; dhcp_config = {};
    recovery = {}; server = {};
    nwp_started = associated = false;
    ipv4 = 0; network_role = -1;
    ap_ready = reset_waiting = reset_issued = offline_waiting = false;
    reset_at = offline_at = listener_attempt = handler_elapsed = 0;
    upload_clock_running = false;
    upload_progress_at = upload_session = upload_written = 0;
    upload_destination = PICO_RECOVERY_FILE_COUNT;
    listener = -1; file_handle = -1; file_capacity = 0; file_writable = false;
    memset(ap_ssid, 0, sizeof(ap_ssid));
}

static void test_storage()
{
    fixture(); setup();
    assert(online() && mutation_count == 0);
    files["/sys/mcuimg2.bin"] = {std::vector<uint8_t>(16, 1), 188416};
    PicoRecoveryFileInfo info;
    assert(file_stat(NULL, PICO_RECOVERY_IMAGE2, &info) == 0 && info.allocated == 188416);
    for (int file : {-1, 7, 99}) {
        assert(file_stat(NULL, static_cast<PicoRecoveryFile>(file), &info) < 0);
        assert(file_open_write(NULL, static_cast<PicoRecoveryFile>(file)) < 0);
    }
    assert(file_create(NULL, PICO_RECOVERY_IMAGE3, 188416) < 0);
    assert(file_open_write(NULL, PICO_RECOVERY_IMAGE3) < 0);
    assert(file_create(NULL, PICO_RECOVERY_WEB_E, 7680) < 0);
    fail_get_info = 1;
    assert(file_create(NULL, PICO_RECOVERY_WEB_E, 3584) < 0);
    fail_get_info = 0;
    assert(mutation_count == 0);
    int handle = file_create(NULL, PICO_RECOVERY_WEB_E, 3584);
    assert(handle >= 0 && files["/local/web/e.gz"].allocated == 3584);
    const uint8_t bytes[32] = {};
    assert(file_write(NULL, handle, 3580, bytes, sizeof(bytes)) < 0);
    assert(file_write(NULL, handle + 1, 0, bytes, sizeof(bytes)) < 0);
    assert(file_write(NULL, handle, 0, bytes, sizeof(bytes)) == 32);
    assert(file_read(NULL, handle, 0, const_cast<uint8_t *>(bytes), sizeof(bytes)) < 0);
    files[boot_path].data[0] = 1;
    int before = mutation_count;
    assert(file_write(NULL, handle, 32, bytes, sizeof(bytes)) < 0 && mutation_count == before);
    assert(file_close(NULL, handle) == 0); // Cleanup remains possible after guard loss.
    assert(file_open_write(NULL, PICO_RECOVERY_IMAGE2) < 0);
    files[boot_path].data[0] = 0;
    assert(file_create(NULL, PICO_RECOVERY_WEB_E, 3584) < 0);
    files["/sys/mcuimg2.bin"].allocated = 16;
    handle = file_open_write(NULL, PICO_RECOVERY_IMAGE2);
    assert(handle >= 0 && file_capacity == 16);
    assert(file_write(NULL, handle, 0, bytes, 32) < 0);
    assert(file_write(NULL, handle, UINT32_MAX, bytes, 1) < 0);
    assert(file_close(NULL, handle) == 0);
    assert(handles.empty());
}

static void test_boot_record()
{
    fixture(); setup();
    uint8_t record[] = {1, 0x3d, 0, 0x20, 0xba, 0xdc, 0xcd, 0xab};
    record[1] = 0;
    assert(boot_write(NULL, record) < 0 && mutation_count == 0);
    record[1] = 0x3d; record[0] = 3;
    assert(boot_write(NULL, record) < 0 && mutation_count == 0);
    record[0] = 1;
    assert(boot_write(NULL, record) == 0);
    uint8_t readback[8];
    assert(boot_read(NULL, readback) == 0 && !memcmp(readback, record, 8));
    assert(boot_write(NULL, record) < 0); // Selector is no longer recovery.
    fixture(); setup();
    const uint8_t trial[] = {0, 0x3d, 0, 0x20, 0x65, 0x87, 0x78, 0x56};
    assert(boot_write(NULL, trial) == 0);
    assert(boot_read(NULL, readback) == 0 && !memcmp(readback, trial, 8));
}

static void test_network()
{
    fixture(); setup();
    assert_outputs_off();
    assert(online() && !ap_ready && ipv4 == 0x0a00018f && test_clock < 1000);
    assert(start_count == 2 && policies == std::vector<int>({0, 1}));
    assert(diagnostic.find("http://10.0.1.143/") != std::string::npos);
    wlan_event(SL_WLAN_DISCONNECT_EVENT);
    ip_event(0xc0a80101);
    assert(!online() && ipv4 == 0);
    wlan_event(SL_WLAN_CONNECT_EVENT);
    ip_event(0x12345678, SL_NETAPP_IPV6_IPACQUIRED_EVENT);
    assert(ipv4 == 0);
    ip_event(0x0a000144);
    assert(online() && ipv4 == 0x0a000144);

    fixture(); station_leases = false; setup();
    assert(test_clock >= 20000 && test_clock < 20020);
    assert(ap_ready && online() && ipv4 == ap_address && persisted_role == ROLE_AP);
    assert(ap_password == std::string(test_key, 16));
    assert(ap_config.ipV4 == ap_address && ap_config.ipV4Mask == 0xffffff00);
    assert(dhcp_config.ipv4_addr_start == 0xc0a80402 && dhcp_config.ipv4_addr_last == 0xc0a80410);
    assert(diagnostic.find(test_key) == std::string::npos && diagnostic.find(ap_password) == std::string::npos);

    fixture(false); station_connects = false; setup();
    assert(test_clock >= 20000 && !ap_ready && ap_password.empty());
    assert(persisted_role == ROLE_STA && mutation_count == 0);
    fixture(); station_connects = false; fail_ap_option = WLAN_AP_OPT_SECURITY_TYPE; setup();
    assert(!ap_ready && persisted_role == ROLE_STA && ap_password.empty());
    assert(start_count == 2); // Never restart as an unprotected AP after a failed setting.
    fixture(); files[key_path].data[0] = 'z'; station_connects = false; setup();
    assert(server.key[0] == 0 && !ap_ready && persisted_role == ROLE_STA);
}

static void request(const char *path, bool authenticated)
{
    received_request = std::string("GET ") + path + " HTTP/1.1\r\nHost: pico\r\n";
    if (authenticated) received_request += std::string("Authorization: Bearer ") + test_key + "\r\n";
    received_request += "\r\n";
    request_offset = 0; transmitted_response.clear(); client_available = true;
}

static void test_http_and_reset()
{
    fixture(); setup();
    request("/api/v1/recovery", true);
    boot_read_delay = 9000; // Handler work must not consume the response deadline.
    for (unsigned i = 0; i < 1000 && (client_available || server.client >= 0); ++i) loop();
    assert(transmitted_response.find("HTTP/1.1 200 OK") == 0);
    assert(transmitted_response.find("\"boot\"") != std::string::npos);
    assert(reset_count == 0 && server.client < 0 && closed_sockets.back() == 0);
    boot_read_delay = 0;
    request("/api/v1/health", false);
    block_send = true;
    for (unsigned i = 0; i < 30; ++i) loop();
    assert(server.client == 0 && transmitted_response.empty());
    recovery.boot_committed = true;
    for (unsigned i = 0; i < 100; ++i) loop();
    assert(reset_count == 0 && !reset_waiting && listener < 0);
    block_send = false; send_limit = 8;
    for (unsigned i = 0; i < 1000 && server.client >= 0; ++i) loop();
    assert(server.client < 0 && reset_waiting && reset_count == 0);
    assert(transmitted_response.find("HTTP/1.1 200 OK") == 0);
    test_clock = reset_at + reset_drain_ms - 1;
    loop(); assert(reset_count == 0);
    loop(); assert(reset_count == 1 && reset_issued && !nwp_started);
    loop(); assert(reset_count == 1);
}

static void begin_partial_upload()
{
    files["/sys/mcuimg2.bin"] = {std::vector<uint8_t>(128, 0), 188416};
    PicoRecoveryPlan plan = {};
    plan.slot = 2;
    strcpy(plan.version, "fixture");
    plan.image.length = 128;
    uint32_t id = 0;
    assert(pico_recovery_set_plan(&recovery, &plan, &id) == PICO_RECOVERY_OK);
    assert(pico_recovery_begin(&recovery, id, PICO_RECOVERY_IMAGE2) == PICO_RECOVERY_OK);
    const uint8_t bytes[32] = {0, 0, 3, 0x20, 9, 0x40, 0, 0x20};
    assert(pico_recovery_chunk(&recovery, id, PICO_RECOVERY_IMAGE2, 0, bytes,
                               sizeof(bytes)) == PICO_RECOVERY_OK);
    loop();
    assert(upload_clock_running && recovery.upload_active && file_handle >= 0 &&
           recovery.written == sizeof(bytes));
}

static void assert_aborted_once(const std::vector<uint8_t> &boot, int mutations)
{
    assert(!recovery.upload_active && recovery.handle < 0 && file_handle < 0);
    assert(!recovery.verified[PICO_RECOVERY_IMAGE2] && !recovery.boot_committed);
    assert(files[boot_path].data == boot && mutation_count == mutations && reset_count == 0);
    assert(file_closes["/sys/mcuimg2.bin"] == 1 && handles.empty());
}

static void test_abandoned_uploads()
{
    fixture(); setup(); begin_partial_upload();
    std::vector<uint8_t> boot = files[boot_path].data;
    int mutations = mutation_count;
    uint32_t progress = upload_progress_at;
    test_clock = progress + upload_inactivity_ms - 1000;
    request("/api/v1/recovery", true);
    for (unsigned i = 0; i < 1000 && (client_available || server.client >= 0); ++i) loop();
    assert(transmitted_response.find("HTTP/1.1 200 OK") == 0);
    assert(upload_progress_at == progress && recovery.upload_active); // GET is not progress.
    test_clock = progress + upload_inactivity_ms;
    loop();
    assert_aborted_once(boot, mutations);
    loop(); loop();
    assert_aborted_once(boot, mutations);
    assert(online() && !ap_ready);

    fixture(); setup(); begin_partial_upload();
    boot = files[boot_path].data; mutations = mutation_count; progress = upload_progress_at;
    station_connects = false;
    wlan_event(SL_WLAN_DISCONNECT_EVENT);
    loop();
    test_clock = progress + station_wait_ms + 2;
    loop();
    assert(recovery.upload_active && file_handle >= 0 && !ap_ready);
    test_clock = progress + upload_inactivity_ms;
    loop();
    assert_aborted_once(boot, mutations);
    assert(ap_ready && online() && persisted_role == ROLE_AP);
    assert(files["/sys/mcuimg2.bin"].data.size() == 32); // Partial payload retained, never retried.

    fixture(); setup(); begin_partial_upload();
    boot = files[boot_path].data; progress = upload_progress_at;
    test_clock = progress + upload_inactivity_ms - 100;
    const uint8_t bytes[32] = {};
    assert(pico_recovery_chunk(&recovery, recovery.session_id, PICO_RECOVERY_IMAGE2,
                               32, bytes, sizeof(bytes)) == PICO_RECOVERY_OK);
    loop();
    uint32_t renewed = upload_progress_at;
    assert(renewed > progress && recovery.written == 64);
    test_clock = progress + upload_inactivity_ms;
    loop();
    assert(recovery.upload_active && file_handle >= 0); // Actual bytes extend the deadline.
    mutations = mutation_count;
    test_clock = renewed + upload_inactivity_ms;
    loop();
    assert_aborted_once(boot, mutations);

    fixture(); setup(); begin_partial_upload();
    boot = files[boot_path].data; mutations = mutation_count; progress = upload_progress_at;
    test_clock = progress + upload_inactivity_ms - 100;
    request("/api/v1/health", false); block_send = true;
    for (unsigned i = 0; i < 20; ++i) loop();
    assert(server.client == 0);
    test_clock = progress + upload_inactivity_ms;
    loop();
    assert(recovery.upload_active && file_handle >= 0); // Never abort inside an active HTTP request.
    block_send = false;
    for (unsigned i = 0; i < 1000 && server.client >= 0; ++i) loop();
    loop();
    assert_aborted_once(boot, mutations);
}

int main()
{
    test_storage();
    test_boot_record();
    test_network();
    test_http_and_reset();
    test_abandoned_uploads();
    puts("Recovery native fixture: file/boot guards, networking, outputs-off, response/reset and abandoned-upload tests passed");
}
