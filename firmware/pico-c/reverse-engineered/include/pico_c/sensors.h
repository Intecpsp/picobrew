/* Pico C reconstruction: adapted from the project S/Pro typed source after
 * checking Pico C instruction flow, GPIO pins, constants and original-image
 * comparisons. See analysis/control-tests.json for coverage and boundaries.
 * This is not a complete firmware or a hardware-qualified port. */
#ifndef PICO_C_SENSORS_H
#define PICO_C_SENSORS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Firmware 0.1.34 calibration, valid for the 12-bit ADC range 0..4095.
 * The output is Fahrenheit. Pin 25 measures wort; pin 26 measures the
 * thermoblock. These functions have no hardware or global-state dependency.
 */
float pico_temperature_pin25_from_adc(uint32_t adc_count, int session_type);
float pico_temperature_pin26_from_adc(uint32_t adc_count);

#ifdef __cplusplus
}
#endif
#endif
