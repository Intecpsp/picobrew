#ifndef PICO_C_MODEL_H
#define PICO_C_MODEL_H

/* Build identity describes this source target; it does not detect a board. */
#define PICO_MODEL_PICO_C 3
#ifndef PICO_MODEL
#define PICO_MODEL PICO_MODEL_PICO_C
#endif
#if PICO_MODEL != PICO_MODEL_PICO_C
#error "This source target supports only Pico C"
#endif
#define PICO_MODEL_ID "pico-c"
#define PICO_MODEL_NAME "Pico C"
#define PICO_MODEL_FAMILY "pico-c"

/* The firmware build requires an explicit display variant. The default below
 * is only for portable host harnesses that do not drive a physical display. */
#ifndef PICO_C_DISPLAY_SSD1309
#define PICO_C_DISPLAY_SSD1309 0
#endif
#if PICO_C_DISPLAY_SSD1309 != 0 && PICO_C_DISPLAY_SSD1309 != 1
#error "PICO_C_DISPLAY_SSD1309 must be 0 or 1"
#endif
#if PICO_C_DISPLAY_SSD1309
#define PICO_DISPLAY_VARIANT "ssd1309"
#define PICO_LOGICAL_DISPLAY_WIDTH 128
#define PICO_LOGICAL_DISPLAY_HEIGHT 32
#define PICO_DISPLAY_ROTATION 0
#else
#define PICO_DISPLAY_VARIANT "ssd1325"
#define PICO_LOGICAL_DISPLAY_WIDTH 128
#define PICO_LOGICAL_DISPLAY_HEIGHT 64
#define PICO_DISPLAY_ROTATION 2
#endif
#define PICO_DISPLAY_STORAGE_BYTES 1024
#define PICO_DISPLAY_TRANSFER_BYTES 1024
#endif
