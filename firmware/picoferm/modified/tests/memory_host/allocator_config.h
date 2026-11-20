// Host address/interrupt boundary for the unmodified pinned SDK allocator.
// Preserve its best-fit policy, packed eight-byte blocks and no poisoning.
#pragma once
#include <stddef.h>
#include <stdint.h>
#define _UMM_MALLOC_CFG_H
#define UMM_BEST_FIT
#define UMM_H_ATTPACKPRE
#define UMM_H_ATTPACKSUF __attribute__((__packed__))
#define UMM_CRITICAL_ENTRY()
#define UMM_CRITICAL_EXIT()
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned char firmware_memory_heap[81920];
extern size_t firmware_memory_heap_size;
#ifdef __cplusplus
}
#endif
#define UMM_MALLOC_CFG__HEAP_ADDR ((uintptr_t)firmware_memory_heap)
#define UMM_MALLOC_CFG__HEAP_SIZE firmware_memory_heap_size
