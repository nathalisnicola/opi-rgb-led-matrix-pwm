// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2013 Henner Zeller <h.zeller@acm.org>
// Ported to Orange Pi Zero 2W (Allwinner H618 / H616 SoC)

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "gpio.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/*
 * Allwinner H618 / H616 GPIO Memory Base and Offsets:
 * Direct memory mapped PIO base: 0x0300B000 (size 0x1000)
 */
#define SUNXI_PIO_BASE        0x0300B000
#define SUNXI_PIO_BLOCK_SIZE  0x1000

#define PORT_C_OFFSET (2 * 0x24)
#define PORT_H_OFFSET (7 * 0x24)
#define PORT_I_OFFSET (8 * 0x24)

#define EMPIRICAL_NANOSLEEP_OVERHEAD_US 10
#define MINIMUM_NANOSLEEP_TIME_US 4

static uint8_t *s_pio_base = NULL;
static uint8_t *s_port_c = NULL;
static uint8_t *s_port_h = NULL;
static uint8_t *s_port_i = NULL;

static volatile uint32_t *s_pc_data = NULL;
static volatile uint32_t *s_ph_data = NULL;
static volatile uint32_t *s_pi_data = NULL;

namespace rgb_matrix {

static void SetPortPinMode(uint8_t *port_base, int pin, uint32_t mode) {
  if (port_base == NULL) return;
  int cfg_reg_index = pin / 8;
  int shift = (pin % 8) * 4;
  volatile uint32_t *cfg = (volatile uint32_t *)(port_base + cfg_reg_index * 4);
  *cfg = (*cfg & ~(0x7u << shift)) | ((mode & 0x7u) << shift);
}

static bool InitSunxiPioRegisters() {
  if (s_pio_base != NULL) return true;

  int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (mem_fd < 0) {
    // Try /dev/gpiomem as fallback
    mem_fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
  }
  if (mem_fd < 0) {
    perror("Need root permissions to access /dev/mem for direct GPIO on Orange Pi Zero 2W");
    return false;
  }

  void *gpio_map = mmap(NULL, SUNXI_PIO_BLOCK_SIZE, PROT_READ | PROT_WRITE,
                        MAP_SHARED, mem_fd, SUNXI_PIO_BASE);
  close(mem_fd);

  if (gpio_map == MAP_FAILED) {
    perror("mmap failed for SUNXI_PIO_BASE (0x0300B000)");
    return false;
  }

  s_pio_base = (uint8_t *)gpio_map;
  s_port_c = s_pio_base + PORT_C_OFFSET;
  s_port_h = s_pio_base + PORT_H_OFFSET;
  s_port_i = s_pio_base + PORT_I_OFFSET;

  s_pc_data = (volatile uint32_t *)(s_port_c + 0x10);
  s_ph_data = (volatile uint32_t *)(s_port_h + 0x10);
  s_pi_data = (volatile uint32_t *)(s_port_i + 0x10);

  return true;
}

GPIO::GPIO()
  : output_bits_(0),
    input_bits_(0),
    reserved_bits_(0),
    slowdown_(1),
    delay_ns_(0),
    delay_loops_(0),
    shadow_c_(0),
    shadow_h_(0),
    shadow_i_(0),
    pc_data_(NULL),
    ph_data_(NULL),
    pi_data_(NULL) {
}

GPIO::~GPIO() {
  // Do not munmap globally as other instances might share it
}

bool GPIO::Init(int slowdown, int delay_ns) {
  slowdown_ = slowdown;
  delay_ns_ = delay_ns;

  // Environment override fallback if set
  const char *env_delay = getenv("OPI_DELAY_NS");
  if (env_delay != NULL && *env_delay != '\0') {
    delay_ns_ = atoi(env_delay);
  }

  // Calculate loop count for requested nanoseconds.
  // On Orange Pi Zero 2W (Allwinner H618, Cortex-A53 @ ~1.5GHz):
  // 1 CPU cycle is ~0.67 ns. A loop of asm volatile("nop") takes ~1.5 - 2 cycles (approx 1 - 1.3 ns per iteration).
  // Thus loops ≈ delay_ns / 1.0 (approx 1 iteration per nanosecond).
  if (delay_ns_ > 0) {
    delay_loops_ = delay_ns_;
    fprintf(stderr, "Orange Pi Zero 2W: Global GPIO delay configured: %d ns (~%d nop loops)\n",
            delay_ns_, delay_loops_);
  } else {
    delay_loops_ = 0;
  }

  if (!InitSunxiPioRegisters()) {
    return false;
  }

  pc_data_ = s_pc_data;
  ph_data_ = s_ph_data;
  pi_data_ = s_pi_data;

  if (pc_data_) shadow_c_ = *pc_data_;
  if (ph_data_) shadow_h_ = *ph_data_;
  if (pi_data_) shadow_i_ = *pi_data_;

  return true;
}

gpio_bits_t GPIO::InitOutputs(gpio_bits_t outputs, bool /*adafruit_hack_needed*/) {
  if (!InitSunxiPioRegisters()) return 0;

  // Port C (bits 0..15)
  uint32_t c_bits = (uint32_t)(outputs & 0xFFFF);
  for (int b = 0; b < 16; ++b) {
    if (c_bits & (1u << b)) {
      SetPortPinMode(s_port_c, b, 0x1);  // 0x1 = Output
    }
  }

  // Port H (bits 16..31)
  uint32_t h_bits = (uint32_t)((outputs >> 16) & 0xFFFF);
  for (int b = 0; b < 16; ++b) {
    if (h_bits & (1u << b)) {
      SetPortPinMode(s_port_h, b, 0x1);  // 0x1 = Output
    }
  }

  // Port I (bits 32..55)
  uint32_t i_bits = (uint32_t)((outputs >> 32) & 0xFFFFFF);
  for (int b = 0; b < 24; ++b) {
    if (i_bits & (1u << b)) {
      SetPortPinMode(s_port_i, b, 0x1);  // 0x1 = Output
    }
  }

  output_bits_ |= outputs;
  reserved_bits_ |= outputs;
  return outputs;
}

gpio_bits_t GPIO::RequestInputs(gpio_bits_t inputs) {
  if (!InitSunxiPioRegisters()) return 0;

  // Port C (bits 0..15)
  uint32_t c_bits = (uint32_t)(inputs & 0xFFFF);
  for (int b = 0; b < 16; ++b) {
    if (c_bits & (1u << b)) {
      SetPortPinMode(s_port_c, b, 0x0);  // 0x0 = Input
    }
  }

  // Port H (bits 16..31)
  uint32_t h_bits = (uint32_t)((inputs >> 16) & 0xFFFF);
  for (int b = 0; b < 16; ++b) {
    if (h_bits & (1u << b)) {
      SetPortPinMode(s_port_h, b, 0x0);  // 0x0 = Input
    }
  }

  // Port I (bits 32..55)
  uint32_t i_bits = (uint32_t)((inputs >> 32) & 0xFFFFFF);
  for (int b = 0; b < 24; ++b) {
    if (i_bits & (1u << b)) {
      SetPortPinMode(s_port_i, b, 0x0);  // 0x0 = Input
    }
  }

  input_bits_ |= inputs;
  reserved_bits_ |= inputs;
  return inputs;
}

void GPIO::ResetState() {
  output_bits_ = 0;
  input_bits_ = 0;
  reserved_bits_ = 0;
}

namespace {

class TimerBasedPinPulser : public PinPulser {
public:
  TimerBasedPinPulser(GPIO *io, gpio_bits_t gpio_mask,
                      const std::vector<int> &nano_wait_spec)
    : io_(io),
      gpio_mask_(gpio_mask),
      nano_wait_spec_(nano_wait_spec) {
    // Initial state: OE = 1 (HIGH) -> Matrix is blanked / HAT gate disabled
    if (io_ != NULL && gpio_mask_ != 0) {
      io_->SetBits(gpio_mask_);
    }
  }

  virtual ~TimerBasedPinPulser() {
    // On exit: keep OE = 1 (HIGH) -> Matrix blanked
    if (io_ != NULL && gpio_mask_ != 0) {
      io_->SetBits(gpio_mask_);
    }
  }

  virtual void SendPulse(int time_spec_number) {
    if (time_spec_number < 0 || time_spec_number >= (int)nano_wait_spec_.size())
      return;

    int nanos = nano_wait_spec_[time_spec_number];
    if (nanos <= 0) return;

    // ACTIVE-LOW OE on HUB75 & HAT:
    // OE = 0 (LOW) -> Output Enabled / LEDs ON / HAT open
    io_->ClearBits(gpio_mask_);

    if (nanos > 0) {
      struct timespec start, now;
      clock_gettime(CLOCK_MONOTONIC_RAW, &start);
      long target_ns = nanos;

      if (target_ns > 20000) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = target_ns - (EMPIRICAL_NANOSLEEP_OVERHEAD_US * 1000);
        if (ts.tv_nsec > 0) {
          nanosleep(&ts, NULL);
        }
      }

      while (true) {
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
        long elapsed_ns = (now.tv_sec - start.tv_sec) * 1000000000L + (now.tv_nsec - start.tv_nsec);
        if (elapsed_ns >= target_ns) break;
      }
    }

    // Return to OE = 1 (HIGH) -> Output Disabled / LEDs OFF / Blanked
    io_->SetBits(gpio_mask_);
  }

  virtual void WaitPulseFinished() {
    // Ensure OE is in blanked/disabled state (OE = 1)
    if (io_ != NULL && gpio_mask_ != 0) {
      io_->SetBits(gpio_mask_);
    }
  }

private:
  GPIO *io_;
  gpio_bits_t gpio_mask_;
  std::vector<int> nano_wait_spec_;
};

}  // namespace

PinPulser *PinPulser::Create(GPIO *io, gpio_bits_t gpio_mask,
                             bool /*allow_hardware_pulsing*/,
                             const std::vector<int> &nano_wait_spec) {
  return new TimerBasedPinPulser(io, gpio_mask, nano_wait_spec);
}

uint32_t GetMicrosecondCounter() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  const uint64_t micros = (uint64_t)ts.tv_nsec / 1000;
  const uint64_t epoch_usec = (uint64_t)ts.tv_sec * 1000000ULL + micros;
  return epoch_usec & 0xFFFFFFFF;
}

void SleepMicroseconds(long usec) {
  if (usec <= 0) return;
  if (usec < MINIMUM_NANOSLEEP_TIME_US) {
    uint32_t start = GetMicrosecondCounter();
    while ((GetMicrosecondCounter() - start) < (uint32_t)usec) {
      asm volatile("yield");
    }
    return;
  }
  struct timespec ts;
  ts.tv_sec = usec / 1000000;
  ts.tv_nsec = (usec % 1000000) * 1000;
  nanosleep(&ts, NULL);
}

}  // namespace rgb_matrix
