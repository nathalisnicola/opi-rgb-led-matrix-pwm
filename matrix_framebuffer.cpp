///g++ -O3 -Wall -Wextra -I./include matrix_framebuffer.cpp -o matrix_framebuffer -L./lib -lrgbmatrix -lpthread -lrt -lm

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <algorithm> // std::min
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <time.h>

// Hlavičkové súbory z knižnice rpi-rgb-led-matrix
#include "include/led-matrix.h"

using rgb_matrix::RGBMatrix;
using rgb_matrix::RuntimeOptions;
using rgb_matrix::FrameCanvas;

struct ThreadArgs {
    RGBMatrix *matrix;
    FrameCanvas *canvas;
    const uint8_t *fb_ptr;
    unsigned int max_x;
    unsigned int max_y;
    size_t line_length; // Opravené: namiesto stride z vinfo
    int bpp;
    int bytes_per_pixel;
};

void timespec_add_ns(struct timespec *t, long ns) {
    t->tv_nsec += ns;
    if (t->tv_nsec >= 1000000000L) {
        t->tv_sec += t->tv_nsec / 1000000000L;
        t->tv_nsec %= 1000000000L;
    }
}

// Vykresľovacie vlákno s opravenou logikou čítania pixelov
void* render_thread_func(void *arg) {
    ThreadArgs *args = (ThreadArgs*)arg;
    FrameCanvas *canvas = args->canvas;
    
    // 50 FPS = 20 000 000 ns
    const long INTERVAL_NS = 20000000L; 

    struct timespec next_wake;
    clock_gettime(CLOCK_MONOTONIC, &next_wake);

    while (true) {
        // Spracovanie riadkov
        for (unsigned int y = 0; y < args->max_y; ++y) {
            // Ukazovateľ na začiatok aktuálneho riadku vo framebufferi
            const uint8_t* row_ptr = args->fb_ptr + (y * args->line_length);

            if (args->bpp == 32 || args->bpp == 24) {
                for (unsigned int x = 0; x < args->max_x; ++x) {
                    // Framebuffer má väčšinou formát BGRA/BGR, matica vyžaduje RGB
                    canvas->SetPixel(x, y, row_ptr[2], row_ptr[1], row_ptr[0]);
                    row_ptr += args->bytes_per_pixel;
                }
            } 
            else if (args->bpp == 16) {
                const uint16_t* row_ptr16 = (const uint16_t*)row_ptr;
                for (unsigned int x = 0; x < args->max_x; ++x) {
                    uint16_t pixel = row_ptr16[x];
                    // Konverzia RGB565 na RGB888
                    uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                    uint8_t g = ((pixel >> 5)  & 0x3F) << 2;
                    uint8_t b = (pixel & 0x1F) << 3;
                    canvas->SetPixel(x, y, r, g, b);
                }
            }
        }

        // Odovzdanie hotového canvasu hardvéru a získanie nového pozadia (Double-buffering)
        canvas = args->matrix->SwapOnVSync(canvas);
        if (canvas == nullptr) {
            std::cerr << "Chyba: SwapOnVSync vrátil nullptr." << std::endl;
            break;
        }

        // Presné časovanie na zamedzenie driftu
        timespec_add_ns(&next_wake, INTERVAL_NS);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wake, NULL);
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    int fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        std::cerr << "Nepodarilo sa otvoriť /dev/fb0. Spustili ste program ako root (sudo)?" << std::endl;
        return 1;
    }

    // Získanie fixných informácií o obrazovke (potrebujeme presnú dĺžku riadku v bajtoch)
    struct fb_fix_screeninfo finfo;
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        std::cerr << "Chyba pri načítaní fixných info o FB." << std::endl;
        close(fb_fd);
        return 1;
    }

    // Získanie variabilných informácií (rozlíšenie, bpp)
    struct fb_var_screeninfo vinfo;
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        std::cerr << "Chyba pri načítaní variabilných info o FB." << std::endl;
        close(fb_fd);
        return 1;
    }

    RGBMatrix::Options matrix_options;
    RuntimeOptions runtime_options;
    
    matrix_options.rows = 64; 
    matrix_options.cols = 128;
    matrix_options.hardware_mapping = "regular";
    runtime_options.gpio_slowdown = 2; // Odporúčané pre RPi 4 (hodnota 2 alebo 3 pre stabilitu)
    
    RGBMatrix *matrix = rgb_matrix::CreateMatrixFromFlags(&argc, &argv, &matrix_options, &runtime_options);
    if (matrix == nullptr) {
        close(fb_fd);
        return 1;
    }
    
    FrameCanvas *canvas = matrix->CreateFrameCanvas();
    
    int bytes_per_pixel = vinfo.bits_per_pixel / 8;
    // Výpočet veľkosti mapovanej pamäte pomocou finfo.line_length (bezpečnejšie)
    size_t screensize = vinfo.yres * finfo.line_length;
    
    uint8_t *fb_ptr = (uint8_t *)mmap(0, screensize, PROT_READ, MAP_SHARED, fb_fd, 0);
    if (fb_ptr == MAP_FAILED) {
        std::cerr << "mmap zlyhal." << std::endl;
        close(fb_fd);
        delete matrix;
        return 1;
    }

    ThreadArgs t_args;
    t_args.matrix = matrix;
    t_args.canvas = canvas;
    t_args.fb_ptr = fb_ptr;
    // Zabezpečíme, aby sme nečítali viac pixelov, než koľko má matica alebo reálny framebuffer
    t_args.max_x = std::min(vinfo.xres, (unsigned int)matrix_options.cols);
    t_args.max_y = std::min(vinfo.yres, (unsigned int)matrix_options.rows);
    t_args.line_length = finfo.line_length;
    t_args.bpp = vinfo.bits_per_pixel;
    t_args.bytes_per_pixel = bytes_per_pixel;

    pthread_t render_thread;
    if (pthread_create(&render_thread, NULL, render_thread_func, &t_args) != 0) {
        std::cerr << "Nepodarilo sa vytvoriť pthread vlákno." << std::endl;
        munmap(fb_ptr, screensize);
        close(fb_fd);
        delete matrix;
        return 1;
    }

    std::cout << "Mostík úspešne spustený. Mapujem " << t_args.max_x << "x" << t_args.max_y 
              << " z FB na LED maticu (50 FPS)." << std::endl;

    // Hlavné vlákno čaká na renderovacie vlákno
    pthread_join(render_thread, NULL);

    // Sem sa program dostane iba v prípade prerušenia / chyby vo vlákne
    munmap(fb_ptr, screensize);
    close(fb_fd);
    delete matrix;
    return 0;
}
