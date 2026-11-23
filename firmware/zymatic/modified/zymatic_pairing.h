#ifndef ZYMATIC_PAIRING_H
#define ZYMATIC_PAIRING_H
#include <stdint.h>

/* Active tickets live only in volatile memory. The physical display captures
 * the request's deadline, so it cannot approve a replacement request. */
enum { ZP_REQUEST, ZP_CLAIM, ZP_GRANT };
enum { ZP_BAD, ZP_MISSING, ZP_CONFLICT, ZP_PENDING, ZP_REQUESTED, ZP_APPROVED, ZP_REDEEMED };
void zymatic_pair_reset(void);
uint8_t zymatic_pair_operation(uint8_t operation, const uint8_t nonce[32], uint32_t now);
uint8_t zymatic_pair_prompt(uint32_t now, char code[9], uint32_t *ticket);
uint8_t zymatic_pair_decide(uint32_t now, uint32_t ticket, uint8_t approve);
char zymatic_hex_digit(uint8_t value);
#endif
