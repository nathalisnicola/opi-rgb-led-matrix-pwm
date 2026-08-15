/*
 * Direct Hardware GPIO Pin Blink & HAT / OE Gate Verification Test
 * for Orange Pi Zero 2W (Allwinner H618 / H616)
 *
 * Compiles directly with:
 *   gcc -O2 -o opi_blink_test opi_blink_test.c
 * Run with root privileges:
 *   sudo ./opi_blink_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define SUNXI_PIO_BASE        0x0300B000
#define SUNXI_PIO_BLOCK_SIZE  0x1000

#define PORT_C_OFFSET (2 * 0x24)
#define PORT_H_OFFSET (7 * 0x24)
#define PORT_I_OFFSET (8 * 0x24)

static void set_pin_mode(uint8_t *port_base, int pin, uint32_t mode) {
    int reg = pin / 8;
    int shift = (pin % 8) * 4;
    volatile uint32_t *cfg = (volatile uint32_t *)(port_base + reg * 4);
    *cfg = (*cfg & ~(0x7u << shift)) | ((mode & 0x7u) << shift);
}

int main(int argc, char **argv) {
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("Cannot open /dev/mem (run as root / sudo)");
        return 1;
    }

    void *map = mmap(NULL, SUNXI_PIO_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, SUNXI_PIO_BASE);
    close(mem_fd);

    if (map == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    uint8_t *pio = (uint8_t *)map;
    uint8_t *port_h = pio + PORT_H_OFFSET;
    uint8_t *port_i = pio + PORT_I_OFFSET;
    uint8_t *port_c = pio + PORT_C_OFFSET;

    volatile uint32_t *ph_data = (volatile uint32_t *)(port_h + 0x10);
    volatile uint32_t *pi_data = (volatile uint32_t *)(port_i + 0x10);
    volatile uint32_t *pc_data = (volatile uint32_t *)(port_c + 0x10);

    printf("=== Orange Pi Zero 2W (H618) GPIO & HAT OE Verification ===\n");
    printf("Configuring regular matrix pins as outputs...\n");

    // Regular Pins:
    // CLK = PH2 (Pin 11), OE = PI1 (Pin 12), LAT = PI13 (Pin 7)
    // A = PI5 (Pin 15), B = PI14 (Pin 16), C = PH4 (Pin 18), D = PI6 (Pin 22), E = PH1 (Pin 10)
    // R1 = PH6, G1 = PH3, B1 = PH9, R2 = PH5, G2 = PH8, B2 = PH7
    set_pin_mode(port_h, 2, 1);  // CLK
    set_pin_mode(port_i, 1, 1);  // OE (Pin 12 / PI1)
    set_pin_mode(port_i, 13, 1); // LAT
    set_pin_mode(port_i, 5, 1);  // A
    set_pin_mode(port_i, 14, 1); // B
    set_pin_mode(port_h, 4, 1);  // C
    set_pin_mode(port_i, 6, 1);  // D
    set_pin_mode(port_h, 1, 1);  // E

    set_pin_mode(port_h, 6, 1);  // R1
    set_pin_mode(port_h, 3, 1);  // G1
    set_pin_mode(port_h, 9, 1);  // B1
    set_pin_mode(port_h, 5, 1);  // R2
    set_pin_mode(port_h, 8, 1);  // G2
    set_pin_mode(port_h, 7, 1);  // B2

    printf("\n[PHASE 1] Pin Toggling Test (5 cycles)...\n");
    for (int cycle = 1; cycle <= 5; cycle++) {
        printf("Cycle %d: All High...\n", cycle);
        *ph_data |= (1 << 2) | (1 << 4) | (1 << 1) | (1 << 6) | (1 << 3) | (1 << 9) | (1 << 5) | (1 << 8) | (1 << 7);
        *pi_data |= (1 << 1) | (1 << 13) | (1 << 5) | (1 << 14) | (1 << 6);
        usleep(250000);

        printf("Cycle %d: All Low...\n", cycle);
        *ph_data &= ~((1 << 2) | (1 << 4) | (1 << 1) | (1 << 6) | (1 << 3) | (1 << 9) | (1 << 5) | (1 << 8) | (1 << 7));
        *pi_data &= ~((1 << 1) | (1 << 13) | (1 << 5) | (1 << 14) | (1 << 6));
        usleep(250000);
    }

    printf("\n[PHASE 2] HAT OE Gate Test (Active-Low Verification):\n");
    printf("1. Setting OE = 0 (LOW) -> HAT Buffer is OPEN (LEDs should be visible/active)\n");
    *pi_data &= ~(1 << 1); // OE = 0 (LOW) -> Active / Open
    // Turn on RGB pins and clock in some data
    *ph_data |= (1 << 6) | (1 << 3) | (1 << 9) | (1 << 5) | (1 << 8) | (1 << 7); // RGB on
    for (int i = 0; i < 64; i++) {
        *ph_data |= (1 << 2);  // CLK high
        *ph_data &= ~(1 << 2); // CLK low
    }
    *pi_data |= (1 << 13);  // LAT high
    *pi_data &= ~(1 << 13); // LAT low
    sleep(2);

    printf("2. Setting OE = 1 (HIGH) -> HAT Buffer is BLANKED/CLOSED (LEDs must be dark)\n");
    *pi_data |= (1 << 1); // OE = 1 (HIGH) -> Closed / Blanked
    sleep(2);

    printf("\nTest complete! When matrix driver is running: OE=0 enables output during PWM, OE=1 blanks during shifting.\n");
    return 0;
}
