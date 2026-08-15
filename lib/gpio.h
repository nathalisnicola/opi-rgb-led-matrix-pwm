// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2013 Henner Zeller <h.zeller@acm.org>
// Ported to Orange Pi Zero 2W (Allwinner H618 / H616)

#ifndef OPI_GPIO_INTERNAL_H
#define OPI_GPIO_INTERNAL_H

#include "gpio-bits.h"

#include <vector>
#include <stdint.h>

#ifndef LED_MATRIX_ALLOW_BARRIER_DELAY
#define LED_MATRIX_ALLOW_BARRIER_DELAY 1
#endif

namespace rgb_matrix {

class GPIO {
public:
  GPIO();
  ~GPIO();

  // Initialize before use. Returns 'true' if successful, 'false' otherwise
  // (e.g. due to a permission problem).
  bool Init(int slowdown, int delay_ns = 0);

  // Initialize outputs.
  // Returns the bits that were available and could be set for output.
  gpio_bits_t InitOutputs(gpio_bits_t outputs,
                          bool adafruit_hack_needed = false);

  // Request given bitmap of GPIO inputs.
  // Returns the bits that were available and could be reserved.
  gpio_bits_t RequestInputs(gpio_bits_t inputs);

  // Reset internal bookkeeping about which GPIOs have been claimed.
  // This does not touch the hardware registers directly; it clears the
  // tracked masks so subsequent InitOutputs/RequestInputs will reconfigure
  // GPIO pins as needed.
  void ResetState();

  // Set the bits that are '1' in the output. Leave the rest untouched.
  inline void SetBits(gpio_bits_t value) {
    if (!value) return;
    uint32_t c = (uint32_t)(value & 0xFFFF);
    uint32_t h = (uint32_t)((value >> 16) & 0xFFFF);
    uint32_t i = (uint32_t)((value >> 32) & 0xFFFFFF);
    if (c && pc_data_) { shadow_c_ |= c; *pc_data_ = shadow_c_; }
    if (h && ph_data_) { shadow_h_ |= h; *ph_data_ = shadow_h_; }
    if (i && pi_data_) { shadow_i_ |= i; *pi_data_ = shadow_i_; }
    delay();
  }

  // Clear the bits that are '1' in the output. Leave the rest untouched.
  inline void ClearBits(gpio_bits_t value) {
    if (!value) return;
    uint32_t c = (uint32_t)(value & 0xFFFF);
    uint32_t h = (uint32_t)((value >> 16) & 0xFFFF);
    uint32_t i = (uint32_t)((value >> 32) & 0xFFFFFF);
    if (c && pc_data_) { shadow_c_ &= ~c; *pc_data_ = shadow_c_; }
    if (h && ph_data_) { shadow_h_ &= ~h; *ph_data_ = shadow_h_; }
    if (i && pi_data_) { shadow_i_ &= ~i; *pi_data_ = shadow_i_; }
    delay();
  }

  // Write all the bits of "value" mentioned in "mask". Leave the rest untouched.
  inline void WriteMaskedBits(gpio_bits_t value, gpio_bits_t mask) {
    uint32_t c_mask = (uint32_t)(mask & 0xFFFF);
    if (c_mask && pc_data_) {
      uint32_t c_val = (uint32_t)(value & 0xFFFF);
      shadow_c_ = (shadow_c_ & ~c_mask) | (c_val & c_mask);
      *pc_data_ = shadow_c_;
    }
    uint32_t h_mask = (uint32_t)((mask >> 16) & 0xFFFF);
    if (h_mask && ph_data_) {
      uint32_t h_val = (uint32_t)((value >> 16) & 0xFFFF);
      shadow_h_ = (shadow_h_ & ~h_mask) | (h_val & h_mask);
      *ph_data_ = shadow_h_;
    }
    uint32_t i_mask = (uint32_t)((mask >> 32) & 0xFFFFFF);
    if (i_mask && pi_data_) {
      uint32_t i_val = (uint32_t)((value >> 32) & 0xFFFFFF);
      shadow_i_ = (shadow_i_ & ~i_mask) | (i_val & i_mask);
      *pi_data_ = shadow_i_;
    }
    delay();
  }

  inline gpio_bits_t Read() const { return ReadRegisters() & input_bits_; }

  static bool IsPi4() { return false; }
  static bool IsPi5Family() { return false; }

private:
  inline void delay() const {
    if (delay_loops_ > 0) {
      for (int n = 0; n < delay_loops_; n++) {
        asm volatile("nop" ::: "memory");
      }
      return;
    }
    if (slowdown_ <= 0) return;
    for (int n = 0; n < slowdown_; n++) {
      asm volatile("dmb ish\n nop\n nop\n nop\n nop\n" ::: "memory");
    }
  }

  inline gpio_bits_t ReadRegisters() const {
    uint64_t c = pc_data_ ? (*pc_data_ & 0xFFFF) : 0;
    uint64_t h = ph_data_ ? (*ph_data_ & 0xFFFF) : 0;
    uint64_t i = pi_data_ ? (*pi_data_ & 0xFFFFFF) : 0;
    return (c) | (h << 16) | (i << 32);
  }

private:
  gpio_bits_t output_bits_;
  gpio_bits_t input_bits_;
  gpio_bits_t reserved_bits_;
  int slowdown_;
  int delay_ns_;
  int delay_loops_;

  // Shadow port states for fast bitwise mutations
  uint32_t shadow_c_;
  uint32_t shadow_h_;
  uint32_t shadow_i_;

  // Direct pointers to Allwinner H618 Port Data Registers
  volatile uint32_t *pc_data_;
  volatile uint32_t *ph_data_;
  volatile uint32_t *pi_data_;
};

// A PinPulser is a utility class that pulses a GPIO pin. There can be various
// implementations.
class PinPulser {
public:
  // Factory for a PinPulser. Chooses the right implementation depending
  // on the context (CPU and which pins are affected).
  static PinPulser *Create(GPIO *io, gpio_bits_t gpio_mask,
                           bool allow_hardware_pulsing,
                           const std::vector<int> &nano_wait_spec);

  virtual ~PinPulser() {}

  // Send a pulse with a given length (index into nano_wait_spec array).
  virtual void SendPulse(int time_spec_number) = 0;

  // If SendPulse() is asynchronously implemented, wait for pulse to finish.
  virtual void WaitPulseFinished() {}
};

// Get rolling over microsecond counter.
uint32_t GetMicrosecondCounter();

void SleepMicroseconds(long);

}  // end namespace rgb_matrix

#endif  // OPI_GPIO_INTERNAL_H

