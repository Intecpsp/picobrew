#ifndef PICO_RECOVERY_SHA256_H
#define PICO_RECOVERY_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} PicoRecoverySha256;
void pico_recovery_sha256_init(PicoRecoverySha256 *);
void pico_recovery_sha256_update(PicoRecoverySha256 *, const uint8_t *, size_t);
void pico_recovery_sha256_final(PicoRecoverySha256 *, uint8_t digest[32]);
#endif
