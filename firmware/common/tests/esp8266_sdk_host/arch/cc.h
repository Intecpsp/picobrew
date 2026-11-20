// Host compilation port for the unmodified pinned lwIP public headers. The
// target build uses the SDK's actual arch/cc.h. Keep wire integer widths equal
// to the ESP8266 ABI; no ESP8266 ROM/newlib declarations are imported on Linux.
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8_t;
typedef int8_t s8_t;
typedef uint16_t u16_t;
typedef int16_t s16_t;
typedef uint32_t u32_t;
typedef int32_t s32_t;
typedef uintptr_t mem_ptr_t;
#define LWIP_ERR_T s32_t
#define ICACHE_FLASH_ATTR
#define SHMEM_ATTR
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define LWIP_PLATFORM_DIAG(x)
#define LWIP_PLATFORM_ASSERT(x)
#define LWIP_PLATFORM_BYTESWAP 1
#define LWIP_PLATFORM_HTONS(n) ((u16_t)((((n)&0xff)<<8)|(((n)>>8)&0xff)))
#define LWIP_PLATFORM_HTONL(n) ((u32_t)((((n)&0xff)<<24)|(((n)&0xff00)<<8)|(((n)>>8)&0xff00)|(((n)>>24)&0xff)))
#define S16_F "d"
#define U16_F "u"
#define X16_F "x"
#define S32_F "d"
#define U32_F "u"
#define X32_F "x"
