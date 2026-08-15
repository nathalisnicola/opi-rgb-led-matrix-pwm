// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
#ifndef RPI_RGB_MATRIX_RP1_SPWM_GPIO_H
#define RPI_RGB_MATRIX_RP1_SPWM_GPIO_H

#include "../gpio-bits.h"

#include <stdint.h>

namespace rgb_matrix {
namespace internal {

// Pi5 SPWM compatibility layer: this gives the existing SPWM software upload
// path an RP1 RIO-backed GPIO implementation. It is separate from the
// non-SPWM RP1 PIO/RIO framebuffer backends.
struct Rp1SpwmGpioRegisters {
  volatile uint32_t *write_bits;
  volatile uint32_t *set_bits;
  volatile uint32_t *clear_bits;
  volatile uint32_t *read_bits;
};

bool Rp1SpwmGpioInit(Rp1SpwmGpioRegisters *registers);
bool Rp1SpwmGpioIsInitialized();
gpio_bits_t Rp1SpwmGpioInitOutputs(gpio_bits_t outputs,
                                   bool adafruit_pwm_transition_hack_needed);
gpio_bits_t Rp1SpwmGpioRequestInputs(gpio_bits_t inputs);

}  // namespace internal
}  // namespace rgb_matrix

#endif  // RPI_RGB_MATRIX_RP1_SPWM_GPIO_H
