/* The stock firmware owns SPI initialization, Ethernet/DHCP and three other
 * W5100 sockets. A foreground poll may use one already-configured, free socket. */
#include "zymatic_api.h"
#include <string.h>

#ifdef __AVR__
#include <avr/io.h>
#include <avr/interrupt.h>

static uint8_t ethernet_selected(void)
{
    /* Assigned the Ethernet.begin() result by 1.1.14's three network paths. */
    return *(volatile uint8_t *)(uintptr_t)0x230b == 1;
}

static uint8_t transfer(uint8_t out, uint8_t *in)
{
    SPDR = out;
    uint8_t count = 0;
    do {
        if (SPSR & _BV(SPIF)) {
            *in = SPDR;
            return 1;
        }
    } while (--count);
    return 0;
}

static uint8_t reg_io(uint8_t write, uint16_t address, uint8_t *value)
{
    const uint8_t selects = _BV(PD6) | _BV(PD7);
    uint8_t saved = SREG, discard;
    cli();
    /* Do not drive either pin until the firmware has configured and released
     * both select lines. Never change SPI mode, timer ownership or direction. */
    if ((DDRD & selects) != selects || (PORTD & selects) != _BV(PD6)
        /* Bound transfer time at the board's 16 MHz. SPR1 means F_CPU/32
         * or slower. SPR0 without SPI2X would select F_CPU/16. */
        || (SPCR & _BV(SPR1)) || ((SPCR & _BV(SPR0)) && !(SPSR & _BV(SPI2X)))
        || (TCCR2A & (_BV(COM2A0) | _BV(COM2A1)))
        || (SPCR & (_BV(SPE) | _BV(MSTR) | _BV(CPOL) | _BV(CPHA) | _BV(DORD)))
           != (_BV(SPE) | _BV(MSTR))) {
        SREG = saved;
        return 0;
    }
    PORTD &= (uint8_t)~_BV(PD6);
    PORTD |= _BV(PD7);
    uint8_t ok = transfer(write ? 0xf0 : 0x0f, &discard)
                 && transfer((uint8_t)(address >> 8), &discard)
                 && transfer((uint8_t)address, &discard)
                 && transfer(write ? *value : 0, &discard);
    PORTD |= _BV(PD6);
    PORTD &= (uint8_t)~_BV(PD7);
    SREG = saved;
    if (!write && ok) *value = discard;
    return ok;
}
#else
/* Test fixture models the socket and the hardware-owned circular buffers. */
extern uint8_t zymatic_test_network_selected(void);
extern uint8_t zymatic_test_register(uint8_t, uint16_t, uint8_t *);
#define reg_io zymatic_test_register
#define ethernet_selected zymatic_test_network_selected
#endif

enum { SOCK = 0x0700, MODE = SOCK, COMMAND = SOCK+1, IRQ = SOCK+2, STATUS = SOCK+3, PORT = SOCK+4,
       TX_FREE = SOCK+0x20, TX_WRITE = SOCK+0x24, RX_SIZE = SOCK+0x26, RX_READ = SOCK+0x28,
       RX_BASE = 0x7800, TX_BASE = 0x5800, RING = 2048, MAX_PER_POLL = 16 };
enum { FREE, OPENING, LISTENING, READING, FILLING, SENDING, CLOSING, ABANDON };
enum { CLOSED = 0, INIT = 0x13, LISTEN = 0x14, ESTABLISHED = 0x17, CLOSE_WAIT = 0x1c };
enum { OPEN = 1, LISTEN_CMD = 2, DISCON = 8, CLOSE = 16, SEND = 32, RECV = 64 };

static uint8_t put(uint16_t addr, uint8_t val) { return reg_io(1, addr, &val); }

static uint8_t word(uint16_t addr, uint16_t *result)
{
    uint8_t hi, lo;
    if (!reg_io(0, addr, &hi) || !reg_io(0, addr+1, &lo)) return 0;
    *result = ((uint16_t)hi << 8) | lo;
    return 1;
}

static uint8_t put_word(uint16_t addr, uint16_t val)
{
    return put(addr, (uint8_t)(val >> 8)) && put(addr+1, (uint8_t)val);
}

static void forget(ZymaticNetwork *n)
{
    memset(n, 0, sizeof(*n));
}

/* 0 indicates an uncertain SPI operation: recheck ownership on the next poll
 * before attempting any cleanup. Unstable RX/TX hardware counters are retried. */
static uint8_t service(ZymaticNetwork *n, uint8_t status, const uint8_t *key, const char *uid, uint32_t now, uint8_t program_active)
{
    uint16_t length, again, ptr;
    if (n->phase != LISTENING && (uint32_t)(now - n->started) > 5000) n->phase = ABANDON;
    if (n->phase == ABANDON) {
        if (!put(COMMAND, CLOSE)) return 0;
        forget(n);
        return 1;
    }
    if (n->phase == OPENING) {
        if (status == INIT) {
            if (!put(COMMAND, LISTEN_CMD)) return 0;
            n->phase = LISTENING;
        }
        return 1;
    }
    if (n->phase == LISTENING) {
        if (status != ESTABLISHED && status != CLOSE_WAIT) return 1;
        n->phase = READING;
        n->started = now;
    }
    if (n->phase == READING) {
        if (!word(RX_SIZE, &length) || !word(RX_SIZE, &again) || !word(RX_READ, &ptr)) return 0;
        if (length != again) return 1;
        if (length > RING || (!length && status == CLOSE_WAIT)) { n->phase = ABANDON; return 1; }
        if (!length) return 1;
        if (length > MAX_PER_POLL) length = MAX_PER_POLL;
        uint8_t done = 0;
        do {
            uint8_t b;
            if (!reg_io(0, RX_BASE + (ptr++ & (RING-1)), &b)) return 0;
            done = zymatic_api_feed(n, b, key);
        } while (!done && --length);
        if (!put_word(RX_READ,ptr) || !put(COMMAND,RECV)) return 0;
        if (done) {
            zymatic_api_reply(n, uid, now, program_active);
            if (n->response_bytes > RING) return 0;
            n->phase = FILLING;
        }
        return 1;
    }
    if (n->phase == FILLING) {
        if (!word(TX_FREE,&length) || !word(TX_FREE,&again) || !word(TX_WRITE,&ptr)) return 0;
        if (length != again || length < n->response_bytes) return 1;
        uint8_t budget = MAX_PER_POLL;
        while (budget-- && n->sent < n->response_bytes) {
            if (!put(TX_BASE + ((ptr+n->sent) & (RING-1)), zymatic_api_reply_byte(n,n->sent))) return 0;
            ++n->sent;
        }
        if (n->sent == n->response_bytes) {
            if (!put_word(TX_WRITE, ptr+n->sent) || !put(COMMAND, SEND)) return 0;
            n->phase = SENDING;
        }
        return 1;
    }
    if (n->phase == SENDING) {
        uint8_t irq;
        if (!reg_io(0, IRQ, &irq)) return 0;
        if (irq & 8) n->phase = ABANDON;
        else if (irq & 16) {
            if (!put(COMMAND, DISCON)) return 0;
            n->phase = CLOSING;
        }
    }
    return 1;
}

void zymatic_network_poll(ZymaticNetwork *n, const uint8_t key[32], const char *uid, uint32_t now, uint8_t program_active)
{
    if (!ethernet_selected()) {
        /* Do not touch a bus that the original firmware has assigned to Wi-Fi.
         * After an interface change, recheck our port and discard the client. */
        if (n->phase != FREE) {
            forget(n);
            n->phase = ABANDON;
        }
        return;
    }
    uint8_t cmd, status, rx, tx;
    if (!reg_io(0, COMMAND, &cmd) || cmd || !reg_io(0, STATUS, &status)) return;
    if (status == CLOSED) {
        if (n->phase != FREE && n->phase != OPENING) forget(n);
        if (n->phase == OPENING) { if ((uint32_t)(now-n->started) > 1000) forget(n); return; }
        if (!reg_io(0, 0x1a, &rx) || !reg_io(0, 0x1b, &tx) || rx != 0x55 || tx != 0x55) return;
        if (put(MODE,1) && put_word(PORT,80) && put(IRQ,0x1f) && put(COMMAND,OPEN)) {
            n->phase = OPENING;
            n->started = now;
        }
        return;
    }
    if (n->phase == FREE) return;
    uint16_t port;
    uint8_t mode;
    if (!word(PORT,&port) || !reg_io(0,MODE,&mode)) return;
    if (port != 80 || mode != 1) { forget(n); return; }
    if (!service(n,status,key,uid,now,program_active)) n->phase = ABANDON;
}
