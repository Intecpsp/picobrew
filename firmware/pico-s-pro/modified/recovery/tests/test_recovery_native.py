#!/usr/bin/env python3
"""Exercise the real native recovery adapter against controlled SDK boundaries.

This fixture models only hardware/driver calls. The entry, filesystem guards,
WLAN/IP callbacks, native socket glue and generic HTTP state machine are real.
The separate ARM build checks the adapter against the hash-pinned SDK headers.
"""
import os
from pathlib import Path
import subprocess
import tempfile

SDK = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint8_t _u8; typedef int16_t _i16; typedef uint16_t _u16;
typedef int32_t _i32; typedef uint32_t _u32;
#define ROLE_STA 0
#define ROLE_AP 2
#define SL_FS_ERR_FILE_NOT_EXISTS (-11)
#define FS_MODE_OPEN_READ 0u
#define FS_MODE_OPEN_WRITE 1u
#define FS_MODE_OPEN_CREATE(size, flags) (0x10000000u | (size) | ((flags)<<24))
#define SL_POLICY_CONNECTION 0x10
#define SL_CONNECTION_POLICY(a,b,c,d,e) ((a)|((b)<<1)|((c)<<2)|((d)<<3)|((e)<<4))
#define IPCONFIG_MODE_ENABLE_IPV4 1
#define SL_IPV4_STA_P2P_CL_DHCP_ENABLE 1
#define SL_MAC_ADDRESS_GET 2
#define SL_IPV4_AP_P2P_GO_STATIC_ENABLE 7
#define SL_IPV4_AP_P2P_GO_GET_INFO 6
#define SL_SEC_TYPE_WPA_WPA2 2
#define SL_WLAN_CFG_AP_ID 0
#define WLAN_AP_OPT_SSID 0
#define WLAN_AP_OPT_SECURITY_TYPE 6
#define WLAN_AP_OPT_PASSWORD 7
#define WLAN_AP_OPT_HIDDEN_SSID 4
#define WLAN_AP_OPT_CHANNEL 3
#define SL_WLAN_CONNECT_EVENT 1
#define SL_WLAN_DISCONNECT_EVENT 2
#define SL_NETAPP_IPV4_IPACQUIRED_EVENT 1
#define SL_NETAPP_IPV6_IPACQUIRED_EVENT 2
#define SL_NET_APP_HTTP_SERVER_ID 1
#define SL_NET_APP_DHCP_SERVER_ID 2
#define NETAPP_SET_DHCP_SRV_BASIC_OPT 0
#define SL_EAGAIN (-11)
#define SL_AF_INET 2
#define SL_SOCK_STREAM 1
#define SL_IPPROTO_TCP 6
#define SL_SOL_SOCKET 1
#define SL_SO_NONBLOCKING 24
typedef struct { _u16 flags; _u32 FileLen, AllocatedLen, Token[4]; } SlFsFileInfo_t;
typedef struct { _u32 ipV4,ipV4Mask,ipV4Gateway,ipV4DnsServer; } SlNetCfgIpV4Args_t;
typedef struct { _u32 lease_time,ipv4_addr_start,ipv4_addr_last; } SlNetAppDhcpServerBasicOpt_t;
typedef struct { _u32 Event; } SlWlanEvent_t;
typedef struct { _u32 Event; struct { struct { _u32 ip; } ipAcquiredV4; } EventData; } SlNetAppEvent_t;
typedef struct { _u32 Event; } SlSockEvent_t;
typedef struct { _u32 Event; } SlHttpServerEvent_t;
typedef struct { _u32 Response; } SlHttpServerResponse_t;
typedef struct { _u32 NonblockingEnabled; } SlSockNonblocking_t;
typedef struct { _u16 sa_family; char data[14]; } SlSockAddr_t;
typedef struct { _u16 sin_family,sin_port; struct { _u32 s_addr; } sin_addr; _u8 padding[8]; } SlSockAddrIn_t;
typedef _u16 SlSocklen_t;
#ifdef __cplusplus
extern "C" {
#endif
int sl_FsGetInfo(const _u8*,_u32,SlFsFileInfo_t*);
int sl_FsOpen(const _u8*,_u32,_u32*,_i32*);
int sl_FsRead(_i32,_u32,_u8*,_u32);
int sl_FsWrite(_i32,_u32,_u8*,_u32);
int sl_FsClose(_i32,const _u8*,const _u8*,_u32);
int sl_SetSockOpt(_i16,_i16,_i16,const void*,_u16);
int sl_Accept(_i16,SlSockAddr_t*,SlSocklen_t*);
int sl_Recv(_i16,void*,_i16,_i16);
int sl_Send(_i16,const void*,_i16,_i16);
int sl_Close(_i16);
int sl_Socket(_i16,_i16,_i16);
int sl_Bind(_i16,const SlSockAddr_t*,_i16);
int sl_Listen(_i16,_i16);
_u16 sl_Htons(_u16);
int sl_NetAppStop(_u32);
int sl_NetAppStart(_u32);
int sl_NetAppSet(_u8,_u8,_u8,const _u8*);
int sl_NetCfgGet(_u8,_u8*,_u8*,_u8*);
int sl_NetCfgSet(_u8,_u8,_u8,const _u8*);
int sl_WlanPolicySet(_u8,_u8,const _u8*,_u8);
int sl_WlanDisconnect(void);
int sl_WlanSetMode(_u8);
int sl_WlanSet(_u16,_u16,_u16,const _u8*);
int sl_Start(const void*,const void*,const void*);
int sl_Stop(_u16);
int _SlNonOsMainLoopTask(void);
void UDMAInit(void);
void PRCMMCUReset(bool);
#ifdef __cplusplus
}
#endif
'''
ENERGIA = r'''
#pragma once
#include <stdint.h>
#define LOW 0
#define OUTPUT 1
class SerialClass { public: void begin(uint32_t); void println(const char*); };
extern SerialClass Serial;
uint32_t millis();
void delay(uint32_t);
void digitalWrite(uint8_t,uint8_t);
void pinMode(uint8_t,uint8_t);
void setup(); void loop();
'''


def main():
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='pico-recovery-native-') as directory:
        temp = Path(directory)
        (temp / 'utility').mkdir()
        (temp / 'driverlib').mkdir()
        (temp / 'Energia.h').write_text(ENERGIA)
        (temp / 'utility/simplelink.h').write_text(SDK)
        for header in ('fs', 'netapp', 'netcfg', 'nonos', 'socket', 'udma_if', 'wlan'):
            (temp / ('utility/' + header + '.h')).write_text('#include "simplelink.h"\n')
        (temp / 'driverlib/prcm.h').write_text('#include "utility/simplelink.h"\n')
        flags = ['-Wall', '-Wextra', '-Werror', '-g', '-O1', '-ffunction-sections',
                 '-fdata-sections', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                 '-I' + str(temp), '-I' + str(root), '-I' + str(root / 'include'),
                 '-I' + str(root / 'recovery')]
        objects = []
        for source in ('src/http_server.c', 'recovery/recovery.c', 'recovery/sha256.c',
                       'recovery/http.c', 'recovery/web_assets.c'):
            target = temp / (source.replace('/', '_') + '.o')
            subprocess.run(['gcc', '-std=c11', *flags, '-c', str(root / source), '-o', str(target)], check=True)
            objects.append(str(target))
        executable = temp / 'recovery-native-test'
        subprocess.run(['g++', '-std=c++11', *flags, str(root / 'tests/recovery_native_fixture.cpp'),
                        *objects, '-Wl,--gc-sections', '-o', str(executable)], check=True)
        env = dict(os.environ)
        env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
        subprocess.run([str(executable)], check=True, env=env, timeout=30)


if __name__ == '__main__':
    main()
