/* Cooperative foreground extension to the stock 1.1.14 controller. */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>
#include "zymatic_api.h"

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
    if (!zymatic_image_valid()) zymatic_image_invalid();
    const uint8_t rampz = RAMPZ;
    memcpy_PF(key, pgm_get_far_address(provision_key), sizeof(key));
    memcpy_PF(uid, pgm_get_far_address(provision_uid), sizeof(uid));
    memcpy_PF(zymatic_hub_host, pgm_get_far_address(provision_host), sizeof(zymatic_hub_host));
    strcpy(zymatic_hub_authority, zymatic_hub_host);
    char *colon = strchr(zymatic_hub_host, ':');
    if (colon) *colon = 0;
    RAMPZ = rampz;
    oem_setup();
}

static void poll_network(uint8_t brewing)
{
    uint32_t now = oem_millis();
    /* The hot-side step loop also checks the physical dial on every pass.
     * Reserve at least 20 ms between Ethernet service calls in that path. */
    if ((uint32_t)(now - last_poll) < (brewing ? 20U : 4U)) return;
    /* 1.1.14 malloc initializes this break from __malloc_heap_start when
     * first used (0x199a0), then advances it at 0x19a04. Defer optional
     * network work if the original stack and heap are too close together. */
    uint16_t heap = *(volatile uint16_t *)(uintptr_t)0x2c84;
    if (!heap) heap = (uintptr_t)&__extension_bss_end;
    uint16_t sp = SP;
    if (heap < (uintptr_t)&__extension_bss_end || sp <= heap || sp - heap < 768) return;
    last_poll = now;
    const uint8_t rampz = RAMPZ;
    zymatic_network_poll(&network, key, uid, now, brewing);
    RAMPZ = rampz;
}

void zymatic_after_loop(void)
{
    oem_serial_events();
    poll_network(0);
}

uint8_t zymatic_menu_button(void *encoder)
{
    uint8_t pressed = oem_button_event(encoder);
    if (!pressed) poll_network(0);
    return pressed;
}

uint8_t zymatic_brew_button(void *encoder)
{
    uint8_t pressed = oem_button_event(encoder);
    /* A pending button, a latched fault or an interrupt-masked caller always
     * returns straight to the original interpreter. */
    if (!pressed && (SREG & _BV(SREG_I)) && !*(volatile uint8_t *)(uintptr_t)0x2334)
        poll_network(1);
    return pressed;
}
