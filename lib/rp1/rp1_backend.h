// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
#ifndef RPI_RGB_MATRIX_RP1_BACKEND_H
#define RPI_RGB_MATRIX_RP1_BACKEND_H

#include <string>

struct HardwareMapping;

namespace rgb_matrix {
namespace internal {

class Framebuffer;

// Shared RP1 backend boundary for the core matrix/framebuffer code. The
// concrete PIO/RIO implementations stay behind this wrapper.
bool Rp1BackendSelectForOptions(bool do_gpio_init, int rp1_pio,
                                int gpio_slowdown,
                                const char *hardware_mapping,
                                int row_address_type, int parallel,
                                const char *panel_type,
                                bool *use_dedicated_backend,
                                std::string *error);

bool Rp1BackendIsActive();
bool Rp1BackendInitIfNeeded(const HardwareMapping &mapping, int double_rows,
                            int parallel, int pwm_lsb_nanoseconds,
                            int dither_bits, int row_address_type,
                            const char *panel_type);
bool Rp1BackendInitializePanelsIfActive(const HardwareMapping &mapping,
                                        const char *panel_type, int columns);
bool Rp1BackendDumpFramebufferIfActive(Framebuffer *framebuffer,
                                       int pwm_low_bit);
void Rp1BackendDeinit();

}  // namespace internal
}  // namespace rgb_matrix

#endif  // RPI_RGB_MATRIX_RP1_BACKEND_H
