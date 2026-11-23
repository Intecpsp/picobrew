#include "zymatic_pairing.h"
#include <string.h>

static struct {
    uint8_t nonce[32];
    uint32_t until;
    uint8_t phase;
} pending;

char zymatic_hex_digit(uint8_t value) {
    return value < 10 ? (char)('0'+value) : (char)('a'+value-10);
}

void zymatic_pair_reset(void) {
    memset(&pending, 0, sizeof pending);
}

static void expire(uint32_t now) {
    if (pending.phase && (int32_t)(now - pending.until) >= 0) zymatic_pair_reset();
}

uint8_t zymatic_pair_operation(uint8_t operation, const uint8_t nonce[32], uint32_t now) {
    expire(now);
    uint8_t mismatch = 0, nonzero = 0;
    for (uint8_t i=0; i<32; i++) {
        mismatch |= nonce[i] ^ pending.nonce[i];
        nonzero |= nonce[i];
    }
    if (!nonzero) return ZP_BAD;
    if (operation == ZP_CLAIM) {
        if (!pending.phase || mismatch) return ZP_MISSING;
        if (pending.phase == 1) return ZP_PENDING;
        zymatic_pair_reset();
        return ZP_REDEEMED;
    }
    if (pending.phase && mismatch) return ZP_CONFLICT;
    if (!pending.phase) {
        memcpy(pending.nonce,nonce,32);
        pending.phase=1; pending.until=now+90000UL;
    }
    if (operation == ZP_GRANT) {
        pending.phase=2; pending.until=now+30000UL;
    }
    return pending.phase == 1 ? ZP_REQUESTED : ZP_APPROVED;
}

uint8_t zymatic_pair_prompt(uint32_t now, char code[9], uint32_t *ticket) {
    expire(now);
    if (pending.phase != 1) return 0;
    *ticket=pending.until;
    for (uint8_t i=0; i<4; i++) {
        code[i*2]=zymatic_hex_digit(pending.nonce[i]>>4);
        code[i*2+1]=zymatic_hex_digit(pending.nonce[i]&15);
    }
    code[8]=0;
    return 1;
}

uint8_t zymatic_pair_decide(uint32_t now, uint32_t ticket, uint8_t approve) {
    expire(now);
    if (pending.phase!=1 || pending.until!=ticket) return 0;
    if (approve) {
        pending.phase=2; pending.until=now+30000UL;
    } else zymatic_pair_reset();
    return 1;
}
