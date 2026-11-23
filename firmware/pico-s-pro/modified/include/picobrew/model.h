#ifndef PICOBREW_MODEL_H
#define PICOBREW_MODEL_H

/* One build-time choice controls every reported and displayed model. The
 * shared S/Pro board implementation does not identify the case automatically.
 * Keep Pro as the historical default; the build tools set this explicitly. */
#define PICO_MODEL_PICO_PRO 1
#define PICO_MODEL_PICO_S 2
#ifndef PICO_MODEL
#define PICO_MODEL PICO_MODEL_PICO_PRO
#endif

#if PICO_MODEL == PICO_MODEL_PICO_PRO
#define PICO_MODEL_ID "pico-pro"
#define PICO_MODEL_NAME "Pico Pro"
#define PICO_MODEL_HEADING "PICO PRO"
#elif PICO_MODEL == PICO_MODEL_PICO_S
#define PICO_MODEL_ID "pico-s"
#define PICO_MODEL_NAME "Pico S"
#define PICO_MODEL_HEADING "PICO S"
#else
#error "Unsupported PICO_MODEL: this firmware supports only Pico Pro and Pico S"
#endif
#define PICO_MODEL_FAMILY "pico-s-pro"

#endif
