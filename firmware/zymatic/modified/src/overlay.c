/* Cooperative foreground extension to the stock 1.1.14 controller. */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/atomic.h>
#include <stdint.h>
#include <string.h>
#include "zymatic_api.h"
#include "zymatic_history.h"
#include "zymatic_pairing.h"

extern char __extension_bss_end;
extern void oem_setup(void);
extern uint8_t zymatic_image_valid(void);
extern void zymatic_image_invalid(void) __attribute__((noreturn));
extern void oem_serial_events(void);
extern uint32_t oem_millis(void);
extern uint8_t oem_button_event(void *encoder);

/* build.py supplies these fields in the private, per-unit application.
 * High flash is read with ELPM, not a 16-bit C pointer. */
const uint8_t provision_key[32] __attribute__((section(".provision"), used)) = {0};
const char provision_uid[33] __attribute__((section(".provision"), used)) = {0};
const char provision_host[64] __attribute__((section(".provision"), used)) = {0};
static uint8_t key[sizeof(provision_key)];
static char uid[sizeof(provision_uid)];
char zymatic_hub_host[sizeof(provision_host)];
char zymatic_hub_authority[sizeof(provision_host)];
static ZymaticNetwork network;
static uint32_t last_poll;

void zymatic_setup(void)
{
    /* The reset hook verified the complete flash image before initializing SRAM. */
    const uint8_t rampz = RAMPZ;
    memcpy_PF(key, pgm_get_far_address(provision_key), sizeof(key));
    memcpy_PF(uid, pgm_get_far_address(provision_uid), sizeof(uid));
    memcpy_PF(zymatic_hub_host, pgm_get_far_address(provision_host), sizeof(zymatic_hub_host));
    strcpy(zymatic_hub_authority, zymatic_hub_host);
    char *colon = strchr(zymatic_hub_host, ':');
    if (colon) *colon = 0;
    RAMPZ = rampz;
    zymatic_pair_reset();
    zymatic_history_init();
    oem_setup();
}

static void poll_network(uint8_t brewing)
{
    uint32_t now = oem_millis();
    /* The hot-side step loop also checks the physical dial on every pass.
     * Reserve at least 20 ms between Ethernet service calls in that path. */
    if ((uint32_t)(now - last_poll) < (brewing == ZYMATIC_PROGRAM ? 20U : 4U)) return;
    /* 1.1.14 malloc initializes this break from __malloc_heap_start when
     * first used (0x199a0), then advances it at 0x19a04. Defer optional
     * network work if the original stack and heap are too close together. */
    uint16_t heap = *(volatile uint16_t *)(uintptr_t)0x2c84;
    if (!heap) heap = (uintptr_t)&__extension_bss_end;
    uint16_t sp = SP;
    if (heap < (uintptr_t)&__extension_bss_end || sp <= heap || sp - heap < 768) return;
    last_poll = now;
    const uint8_t rampz = RAMPZ;
    zymatic_history_poll(now);
    zymatic_network_poll(&network, key, uid, now, brewing);
    RAMPZ = rampz;
}

/* Replace only About's OEM physical wait. The dial position and latch are
 * owned by stock interrupt handlers. REST polls at this site use OUTER, where
 * the command handler rejects both remote motion and remote presses. */
#define OEM_ENTRY(address,type,args) ((type (*) args)(uintptr_t)((address)/2))
#define PAIR_DIAL ((void *)(uintptr_t)0x2494)
static int16_t physical_position(void)
{
    int16_t v;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {v=*(volatile int16_t *)(uintptr_t)0x24a6;}
    return v;
}
static void pair_line(uint8_t row,uint_farptr_t address)
{
    char line[22];
    strcpy_PF(line,address);
    OEM_ENTRY(0x7f0e,void,(uint8_t,uint8_t,const char *))(row,0,line);
}
#define PAIR_LINE(row,text) do {static const char v[] PROGMEM=text;pair_line(row,pgm_get_far_address(v));} while(0)
__attribute__((section(".low_functions")))
void zymatic_about_wait(void)
{
    uint8_t rampz=RAMPZ;
    char code[9];
    uint32_t ticket;
    OEM_ENTRY(0x11240,void,(void *))(PAIR_DIAL);
    zymatic_panel_changed();
    for (;;) {
        wdt_reset();
        if (oem_button_event(PAIR_DIAL)) break;
        poll_network(ZYMATIC_OUTER);
        if (!zymatic_pair_prompt(oem_millis(),code,&ticket)) continue;
        int16_t initial=physical_position();
        uint8_t drawn=2;
        for (;;) {
            uint32_t current_ticket,now=oem_millis();
            if (!zymatic_pair_prompt(now,code,&current_ticket) || current_ticket!=ticket) goto done;
            uint8_t allow=physical_position()!=initial;
            if (allow!=drawn) {
                OEM_ENTRY(0x1604e,void,(void *))((void *)(uintptr_t)0x1693);
                PAIR_LINE(0,"Pair with Hub");
                PAIR_LINE(2,"Compare Hub code:");
                OEM_ENTRY(0x7f0e,void,(uint8_t,uint8_t,const char *))(3,0,code);
                PAIR_LINE(5,"Turn to choose");
                if (allow) {PAIR_LINE(7,"Press to approve");}
                else {PAIR_LINE(7,"Press to cancel");}
                OEM_ENTRY(0x15e46,void,(void *))((void *)(uintptr_t)0x1693);
                drawn=allow;
            }
            wdt_reset();
            if (oem_button_event(PAIR_DIAL)) {
                zymatic_pair_decide(oem_millis(),ticket,drawn);
                goto done;
            }
            poll_network(ZYMATIC_OUTER);
        }
    }
done:
    zymatic_panel_changed();
    RAMPZ=rampz;
}
void zymatic_after_loop(void)
{
    oem_serial_events();
    poll_network(0);
}

/* A regular list is 1-based. After a remote press, its stock confirmation
 * immediately reads 0x111ce again. Preserve the row shown and confirmed even
 * if a physical rotation interrupts those two adjacent stock calls. */
static uint8_t confirmed_row;
int16_t zymatic_menu_selection(void *encoder)
{
    uint8_t row = confirmed_row;
    confirmed_row = 0;
    return row ? row : ((int16_t (*)(void *))(uintptr_t)(0x111ceu / 2))(encoder);
}

__attribute__((used, externally_visible))
uint8_t zymatic_menu_input(void *encoder, uint8_t displayed, uint8_t mode)
{
    zymatic_drawn_row = displayed;
    uint8_t pressed = oem_button_event(encoder);
    if (pressed) zymatic_panel_changed();
    else if (SREG & _BV(SREG_I)) poll_network(mode);
    if (!pressed && zymatic_panel_take_press()) {
        oem_button_event(encoder);  /* Coalesce a simultaneous physical press. */
        if (mode == ZYMATIC_MENU) confirmed_row = displayed;
        return 0xff;
    }
    return pressed;
}

uint8_t zymatic_brew_button(void *encoder)
{
    uint8_t pressed = oem_button_event(encoder);
    /* A pending button, a latched fault or an interrupt-masked caller always
     * returns straight to the original interpreter. */
    if (pressed) zymatic_panel_changed();
    else if ((SREG & _BV(SREG_I)) && !*(volatile uint8_t *)(uintptr_t)0x2334)
        poll_network(1);
    if (!pressed && zymatic_panel_take_press()) {
        oem_button_event(encoder);  /* Coalesce a simultaneous physical press. */
        return 0xff;
    }
    return pressed;
}
