/* SPI2 display driver via /dev/spidev — kernel SPI driver.
 * PE4 (frame pin) controlled via /sys/class/gpio.
 */
#ifndef SPI_DISPLAY_H
#define SPI_DISPLAY_H

#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DISP_COLS 37
#define DISP_ROWS 8
#define FRAME_SIZE 488

static int spi_fd = -1;

static void gpio_export(int pin) {
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d", pin);
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) { write(fd, buf, n); close(fd); }
    usleep(100000);
}

static void gpio_direction(int pin, const char *dir) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, dir, strlen(dir)); close(fd); }
}

static void gpio_set(int pin, int val) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, val ? "1" : "0", 1); close(fd); }
}

/* PE4 = GPIO 132 (port E base=128, pin 4) */
#define PE4_GPIO 132

static int spi_display_init(void) {
    /* Try spidev2.0 first, then spidev0.0 */
    spi_fd = open("/dev/spidev2.0", O_RDWR);
    if (spi_fd < 0) spi_fd = open("/dev/spidev0.0", O_RDWR);
    if (spi_fd < 0) return -1;

    /* Configure SPI: mode 0, 12MHz */
    unsigned char mode = 0;
    unsigned char bits = 8;
    unsigned int speed = 12000000;
    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    /* Set up PE4 as frame pin (GPIO output) */
    gpio_export(PE4_GPIO);
    gpio_direction(PE4_GPIO, "out");
    gpio_set(PE4_GPIO, 0);  /* Start LOW (animation mode) */

    return 0;
}

static int spi_send_frame(const unsigned char *buf, int len) {
    if (spi_fd < 0) return -1;
    /* Protocol from original kernel driver (lametric_display.ko send_loop):
     * 1. SPI transfer: 1 byte header (0x04) — separate CS assertion
     * 2. PE4 = HIGH
     * 3. SPI transfer: 488 bytes pixel data — separate CS assertion
     * 4. PE4 = LOW
     */
    unsigned char header = 0x04;
    write(spi_fd, &header, 1);      /* Header byte with its own CS cycle */
    gpio_set(PE4_GPIO, 1);          /* PE4 HIGH */
    int ret = write(spi_fd, buf, len);  /* Pixel data with its own CS cycle */
    gpio_set(PE4_GPIO, 0);          /* PE4 LOW */
    return ret;
}

static void spi_frame_mode(int on) {
    gpio_set(PE4_GPIO, on);
}

/* Build a 488-byte frame from a 37x8 grayscale buffer.
 * Try row-major order (row * cols + col) instead of column-major.
 */
static void build_frame(const unsigned char pixels[DISP_ROWS][DISP_COLS],
                        unsigned char frame[FRAME_SIZE]) {
    int col, row;
    memset(frame, 0, FRAME_SIZE);

    /* Full 37-column mapping. Physical layout left→right:
     * [S1:8 RGBW] [S2p9-12:4 rowshift] [S2p0-8:9 normal] [S3p12-15:4 rowshift] [S3p0-11:12 normal]
     * Pixel cols:  0-7        8-11            12-20            21-24              25-36
     * Row-shifted: frame row R → physical row R+1. Row 0 wraps from frame row 7. */

    /* Section 1: 8 RGBW cols, simple row-major starting at byte 0 (not 4).
     * STM32 reads S1 from byte 0. Stride 32. */
    for (row = 0; row < 8; row++) {
        for (col = 0; col < 8; col++) {
            int idx = row * 32 + col * 4;
            unsigned char v = pixels[row][col];
            frame[idx+0] = 0; frame[idx+1] = 0; frame[idx+2] = 0; frame[idx+3] = v;
        }
    }

    /* S2 extra: pixel cols 8-11 → S2 positions 9-12, row-shifted.
     * Row 0 → bytes 256-259 (last 4 bytes of S1 area, shared with S1 row 7 col 7 RGBW).
     * Rows 1-7 → frame row-1 positions 9-12. */
    for (col = 8; col < 12; col++) {
        frame[256 + (col - 8)] = pixels[0][col];
        int pos = 9 + (col - 8);
        for (row = 1; row < 8; row++) {
            frame[260 + (row - 1) * 13 + pos] = pixels[row][col];
        }
    }

    /* S2 main: pixel cols 12-20 → S2 positions 0-8, normal rows */
    for (row = 0; row < 8; row++) {
        for (col = 12; col < 21; col++) {
            frame[260 + row * 13 + (col - 12)] = pixels[row][col];
        }
    }

    /* S3 extra: pixel cols 21-24 → S3 positions 12-15, row-shifted.
     * Row 0 → bytes 360-363 (S2 frame row 7 positions 9-12).
     * Rows 1-7 → S3 frame (row-1) positions 12-15. */
    for (col = 21; col < 25; col++) {
        frame[260 + 7 * 13 + 9 + (col - 21)] = pixels[0][col];
        int pos = 12 + (col - 21);
        for (row = 1; row < 8; row++) {
            frame[364 + (row - 1) * 16 + pos] = pixels[row][col];
        }
    }

    /* S3 main: pixel cols 25-36 → S3 positions 0-11, normal rows */
    for (row = 0; row < 8; row++) {
        for (col = 25; col < 37; col++) {
            int idx = 364 + row * 16 + (col - 25);
            if (idx < 488) frame[idx] = pixels[row][col];
        }
    }
}

#endif
