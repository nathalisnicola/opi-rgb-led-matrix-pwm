// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2013 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

// This file needs to compile in C and C++ context, so deliberately broken out.

#ifndef RPI_GPIOBITS_H
#define RPI_GPIOBITS_H

#include <stdint.h>

/*
 * Orange Pi Zero 2W (Allwinner H618 / H616 SoC) GPIO Bitmask Encoding:
 * - Bits 0..15  : Port C Data Register (PC0..PC15) -> Pin PC12 is bit 12
 * - Bits 16..31 : Port H Data Register (PH0..PH15) -> Pins PH0..PH9 are bits 16..25
 * - Bits 32..55 : Port I Data Register (PI0..PI23) -> Pins PI0..PI16 are bits 32..48
 */
typedef uint64_t gpio_bits_t;

#define OPI_PC(pin) ((uint64_t)1 << (pin))
#define OPI_PH(pin) ((uint64_t)1 << (16 + (pin)))
#define OPI_PI(pin) ((uint64_t)1 << (32 + (pin)))

#ifndef GPIO_BIT
#define GPIO_BIT(b) ((uint64_t)1 << (b))
#endif

#endif

