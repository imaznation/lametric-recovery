/* Init with interactive shell + I2C + SPI display control on /dev/ttyGS0.
 * STM32 display controller is on /dev/i2c-1 (I2C2 controller) at addr 0x21.
 * SPI2 pixel frames via /dev/mem direct register access.
 */
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/sysmacros.h>

#include "spi_display.h"
#include "font5x7.h"
#include "icons8x8.h"

#define I2C_SLAVE 0x0703

static int ser_fd = -1;
static int i2c_fd = -1;

/* ===========================================================================
 * APP FRAMEWORK — each app has state, render, and button handlers
 * =========================================================================== */

/* Global state */
static int carousel_active = 0;
static int audio_volume = 63;

/* Scroll mode: 0=auto (bounce short, wrap long), 1=left-to-right, 2=right-to-left,
 * 3=bounce, 4=no-scroll (clip) */
static int scroll_mode = 0;

/* Auto-brightness mapping: min and max output brightness (0-255).
 * Default 15-127 is conservative. Raise lux_bright_min for brighter in dark rooms. */
static int lux_bright_min = 30;
static int lux_bright_max = 200;
static unsigned int vol_display_until = 0;

/* --- Clock App --- */
static int clock_mode = 0; /* 0=24h, 1=12h, 2=date, 3=day+time */

/* --- Weather App --- */
static char weather_icon_name[16] = "";
static char weather_temp_str[16] = "";
static char weather_cond_str[32] = "";   /* e.g. "Cloudy" */
static char weather_hilo_str[24] = "";   /* e.g. "H:52 L:38" */
static char weather_extra_str[24] = "";  /* e.g. "Wind 5mph" */
static int weather_mode = 0; /* 0=temp, 1=conditions, 2=hi/lo, 3=extra */

/* --- Metrics App --- */
#define MAX_METRICS 8
static char metric_names[MAX_METRICS][16];
static char metric_values[MAX_METRICS][24];
static char metric_icons[MAX_METRICS][16];
static char metric_trends[MAX_METRICS][8]; /* "up","down","stable","" */
static int metric_count = 0;
static int metric_idx = 0; /* which metric is shown */

/* --- Timer App --- */
static int timer_running = 0;
static int timer_target_secs = 0;
static unsigned int timer_start_up = 0;
static int timer_paused_secs = 0;

/* --- Notifications App --- */
#define MAX_NOTIFS 4
static char notif_texts[MAX_NOTIFS][64];
static char notif_icons[MAX_NOTIFS][16];
static unsigned int notif_expires[MAX_NOTIFS];
static int notif_count = 0;
static int notif_idx = 0; /* which notification is shown */

/* --- Sounds App --- */
static const char *sound_list[] = {
    "positive1","positive6","notification","cash","win",
    "cat","alarm","volume", NULL
};
static int sound_idx = 0;

/* --- App list --- */
#define APP_CLOCK   0
#define APP_WEATHER 1
#define APP_METRICS 2
#define APP_TIMER   3
#define APP_NOTIF   4
#define APP_SOUNDS  5
#define NUM_APPS    6
static int current_app = APP_CLOCK;

static void spr(const char *s) {
    if (ser_fd >= 0) write(ser_fd, s, strlen(s));
}

static void spr_num(int v) {
    char b[16];
    snprintf(b, sizeof(b), "%d", v);
    spr(b);
}

static int file_exists(const char *p) { struct stat s; return stat(p, &s) == 0; }

static void i2c_cmd(int reg, int val) {
    if (i2c_fd < 0) return;
    unsigned char buf[2] = {reg, val};
    write(i2c_fd, buf, 2);
    usleep(10000);
}

static int parse_hex(const char *s) {
    int v = 0;
    while (*s) {
        char c = *s++;
        if (c >= '0' && c <= '9') v = v*16 + (c-'0');
        else if (c >= 'a' && c <= 'f') v = v*16 + (c-'a'+10);
        else if (c >= 'A' && c <= 'F') v = v*16 + (c-'A'+10);
        else break;
    }
    return v;
}

static void cmd_info(void) {
    char buf[256];
    spr("=== LaMetric Recovery ===\r\n");
    int fd = open("/proc/version", O_RDONLY);
    if (fd >= 0) { int n=read(fd,buf,sizeof(buf)-1); if(n>0){buf[n]=0; spr(buf);} close(fd); spr("\r\n"); }
    fd = open("/proc/uptime", O_RDONLY);
    if (fd >= 0) { int n=read(fd,buf,sizeof(buf)-1); if(n>0){buf[n]=0; spr("Uptime: "); spr(buf);} close(fd); spr("\r\n"); }
    spr("I2C STM32: "); spr(i2c_fd >= 0 ? "connected\r\n" : "NOT connected\r\n");
    const char *devs[] = {"/dev/i2c-0","/dev/i2c-1","/dev/mem","/dev/ttyS1",NULL};
    for (int i=0; devs[i]; i++) { spr(devs[i]); spr(file_exists(devs[i]) ? ": YES\r\n" : ": no\r\n"); }
}

static void cmd_dmesg(void) {
    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
    if (fd < 0) { spr("Cannot open /dev/kmsg\r\n"); return; }
    char buf[512]; int n;
    while ((n = read(fd, buf, sizeof(buf)-1)) > 0) { buf[n]=0; spr(buf); }
    close(fd);
}

static void cmd_ls(const char *path) {
    DIR *d = opendir(path[0] ? path : "/");
    if (!d) { spr("Cannot open\r\n"); return; }
    struct dirent *ent;
    while ((ent = readdir(d))) { spr(ent->d_name); spr("  "); }
    spr("\r\n"); closedir(d);
}

static void cmd_cat(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { spr("Cannot open\r\n"); return; }
    char buf[512]; int n;
    while ((n = read(fd, buf, sizeof(buf)-1)) > 0) { buf[n]=0; spr(buf); }
    close(fd); spr("\r\n");
}

static void cmd_color(const char *args) {
    /* color R G B [W] — values 0-127 */
    if (i2c_fd < 0) { spr("I2C not connected\r\n"); return; }
    int r=0, g=0, b=0, w=0;
    sscanf(args, "%d %d %d %d", &r, &g, &b, &w);
    i2c_cmd(0x50, 0x00); /* stop animation */
    usleep(100000);
    i2c_cmd(0x90, 100);  /* scan freq */
    i2c_cmd(0x11, r);
    i2c_cmd(0x12, g);
    i2c_cmd(0x13, b);
    i2c_cmd(0x20, w);
    /* Refresh loop to keep display lit */
    int i;
    for (i = 0; i < 200; i++) { i2c_cmd(0xC0, 0x01); usleep(50000); }
    spr("Color set\r\n");
}

static void cmd_anim(void) {
    if (i2c_fd < 0) { spr("I2C not connected\r\n"); return; }
    i2c_cmd(0x50, 0x01);
    i2c_cmd(0x20, 127);
    spr("Animation started\r\n");
}

static int spi_ready = 0;
static pid_t spi_child = 0;

/* Shared frame buffer — child process reads this continuously */
static unsigned char *shared_frame = NULL;

static void spi_init_display(void) {
    if (spi_ready) return;
    spr("Initializing SPI2...\r\n");
    int ret = spi_display_init();
    if (ret < 0) { spr("SPI init failed: "); spr_num(ret); spr("\r\n"); return; }
    spi_ready = 1;
    i2c_cmd(0x50, 0x00);
    usleep(500000);
    i2c_cmd(0x90, 100);
    i2c_cmd(0x20, 127);
    i2c_cmd(0x11, 127);
    i2c_cmd(0x12, 127);
    i2c_cmd(0x13, 127);
    spi_frame_mode(1);
}

static void start_spi_loop(void) {
    /* Fork a child that continuously sends the shared frame */
    if (spi_child > 0) {
        kill(spi_child, 9); /* Kill old child */
        usleep(100000);
    }
    /* Allocate shared frame via mmap (shared between parent+child) */
    if (!shared_frame) {
        shared_frame = mmap(NULL, FRAME_SIZE, PROT_READ|PROT_WRITE,
                            MAP_SHARED|MAP_ANONYMOUS, -1, 0);
        memset(shared_frame, 0, FRAME_SIZE);
    }

    spi_child = fork();
    if (spi_child == 0) {
        /* Child: continuously send SPI frames forever */
        while (1) {
            spi_send_frame(shared_frame, FRAME_SIZE);
            usleep(5000); /* ~200 fps — reduced from 2000fps to minimize EMI buzzing */
        }
        _exit(0);
    }
    /* After starting child, trigger initial BCM render */
    usleep(100000);
    i2c_cmd(0xC0, 0x01);
}

static void spi_load_and_refresh(unsigned char *frame) {
    /* Copy frame to shared buffer and start persistent background loop.
     * If loop already running, just update the shared frame (child picks it up). */
    if (!shared_frame) {
        shared_frame = mmap(NULL, FRAME_SIZE, PROT_READ|PROT_WRITE,
                            MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    }
    memcpy(shared_frame, frame, FRAME_SIZE);
    if (spi_child <= 0) {
        start_spi_loop();
    }
    /* Also send a burst for immediate visibility */
    int i;
    for (i = 0; i < 500; i++) {
        spi_send_frame(frame, FRAME_SIZE);
    }
}

static unsigned char text_brightness = 200;

static void cmd_text(const char *text) {
    spi_init_display();
    if (!spi_ready) return;

    unsigned char pixels[8][37];
    render_text_at(text, pixels, text_brightness, 8);
    unsigned char frame[FRAME_SIZE];
    build_frame(pixels, frame);

    spi_load_and_refresh(frame);
    spr("Text: "); spr(text); spr("\r\n");
}

static void cmd_bright(const char *arg) {
    int b = atoi(arg);
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    text_brightness = b;
    /* Also update I2C brightness */
    i2c_cmd(0x20, b > 127 ? 127 : b);
    spr("Brightness: "); spr_num(b); spr("\r\n");
}

/* Simple time tracking: PC sends "settime HH:MM" and we count seconds from there */
static int time_hours = -1, time_minutes = -1, time_seconds = 0;
static unsigned int time_set_uptime = 0;

static unsigned int get_uptime_secs(void) {
    char buf[64];
    int fd = open("/proc/uptime", O_RDONLY);
    if (fd < 0) return 0;
    int n = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    /* Parse "123.45 ..." */
    unsigned int secs = 0;
    const char *p = buf;
    while (*p >= '0' && *p <= '9') { secs = secs * 10 + (*p - '0'); p++; }
    return secs;
}

static void cmd_settime(const char *arg) {
    /* Parse HH:MM */
    int h = 0, m = 0;
    const char *p = arg;
    while (*p >= '0' && *p <= '9') { h = h * 10 + (*p - '0'); p++; }
    if (*p == ':') p++;
    while (*p >= '0' && *p <= '9') { m = m * 10 + (*p - '0'); p++; }
    time_hours = h; time_minutes = m; time_seconds = 0;
    time_set_uptime = get_uptime_secs();
    spr("Time set to "); spr_num(h); spr(":"); spr_num(m); spr("\r\n");
}

static void cmd_clock(void) {
    if (time_hours < 0) { spr("Time not set. Use: settime HH:MM\r\n"); return; }
    spi_init_display();
    if (!spi_ready) return;

    spr("Clock mode (send any key to stop)\r\n");
    /* Set non-blocking for keypress detection */
    int flags = fcntl(ser_fd, F_GETFL, 0);
    fcntl(ser_fd, F_SETFL, flags | O_NONBLOCK);
    while (1) {
        /* Calculate current time */
        unsigned int elapsed = get_uptime_secs() - time_set_uptime;
        int total_secs = time_hours * 3600 + time_minutes * 60 + time_seconds + elapsed;
        total_secs %= 86400; /* Wrap at 24h */
        int h = total_secs / 3600;
        int m = (total_secs % 3600) / 60;

        char tbuf[8];
        tbuf[0] = '0' + h / 10;
        tbuf[1] = '0' + h % 10;
        tbuf[2] = ':';
        tbuf[3] = '0' + m / 10;
        tbuf[4] = '0' + m % 10;
        tbuf[5] = 0;

        unsigned char pixels[8][37];
        render_text_at(tbuf, pixels, text_brightness, 8);
        unsigned char frame[FRAME_SIZE];
        build_frame(pixels, frame);

        /* Update shared frame for persistent display */
        if (shared_frame) memcpy(shared_frame, frame, FRAME_SIZE);
        if (spi_child <= 0) start_spi_loop();

        /* Check for keypress to exit */
        char c;
        int n = read(ser_fd, &c, 1);
        if (n > 0) break;

        sleep(1);
    }
    /* Restore blocking mode */
    fcntl(ser_fd, F_SETFL, flags);
    spr("Clock stopped\r\n");
}

static void cmd_white(void) {
    spi_init_display();
    if (!spi_ready) return;

    unsigned char frame[FRAME_SIZE];
    memset(frame, 0, FRAME_SIZE);
    int i;
    for (i = 4; i < FRAME_SIZE; i++) frame[i] = 0xFF;

    spi_load_and_refresh(frame);
    spr("All white\r\n");
}

static void cmd_gradient(void) {
    /* Send a frame where each section has a brightness gradient
     * from dim (byte index low) to bright (byte index high).
     * This reveals the physical byte ordering on the display. */
    spi_init_display();
    if (!spi_ready) return;

    unsigned char frame[FRAME_SIZE];
    memset(frame, 0, FRAME_SIZE);
    int i;
    /* Section 1 (4-259): gradient in W channel */
    for (i = 4; i < 260; i += 4) {
        unsigned char v = ((i - 4) * 255) / 252;
        frame[i+0] = v; frame[i+1] = v; frame[i+2] = v; frame[i+3] = v;
    }
    /* Section 2 (260-363): gradient */
    for (i = 260; i < 364; i++) {
        frame[i] = ((i - 260) * 255) / 103;
    }
    /* Section 3 (364-487): gradient */
    for (i = 364; i < 488; i++) {
        frame[i] = ((i - 364) * 255) / 123;
    }
    spi_load_and_refresh(frame);
    spr("Gradient: dim=low byte, bright=high byte\r\n");
}

static void cmd_scroll(const char *text) {
    spi_init_display();
    if (!spi_ready) return;

    /* Render full text into a wide buffer, then scroll it across the display */
    int text_len = 0;
    const char *p = text;
    while (*p++) text_len++;
    int total_width = text_len * 6; /* 5px + 1px gap per char */
    if (total_width < 37) total_width = 37;

    /* Render into wide buffer (max 256 cols) */
    if (total_width > 256) total_width = 256;
    unsigned char wide[8][256];
    memset(wide, 0, sizeof(wide));
    /* Use render_text to fill the wide buffer */
    int x = 0;
    p = text;
    while (*p && x < 256) {
        unsigned char ch = *p++;
        if (ch >= 128) continue;
        const unsigned char *glyph = font5x7[ch];
        int col;
        for (col = 0; col < 5 && x < 256; col++, x++) {
            unsigned char bits = glyph[col];
            int row;
            for (row = 0; row < 7; row++) {
                if (bits & (1 << row))
                    wide[row][x] = text_brightness;
            }
        }
        x++; /* gap */
    }

    spr("Scrolling: "); spr(text); spr("\r\n");

    /* Set non-blocking for keypress detection */
    int flags = fcntl(ser_fd, F_GETFL, 0);
    fcntl(ser_fd, F_SETFL, flags | O_NONBLOCK);

    int offset;
    for (offset = 0; offset < total_width + 37; offset++) {
        unsigned char pixels[8][37];
        memset(pixels, 0, sizeof(pixels));
        int r, c;
        for (r = 0; r < 8; r++) {
            for (c = 0; c < 37; c++) {
                int src = c + offset - 37;
                if (src >= 0 && src < total_width)
                    pixels[r][c] = wide[r][src];
            }
        }
        unsigned char frame[FRAME_SIZE];
        build_frame(pixels, frame);
        if (shared_frame) memcpy(shared_frame, frame, FRAME_SIZE);
        if (spi_child <= 0) start_spi_loop();

        /* Small delay for scroll speed */
        usleep(50000); /* 50ms per step = ~20 cols/sec */

        /* Check for keypress to stop */
        char c2;
        if (read(ser_fd, &c2, 1) > 0) break;
    }

    fcntl(ser_fd, F_SETFL, flags);
    spr("Scroll done\r\n");
}

static int gpio_read(int pin) {
    char path[64], val[4];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, val, sizeof(val)-1);
    close(fd);
    if (n <= 0) return -1;
    return val[0] - '0';
}

static void cmd_buttons(void) {
    /* Export button GPIOs: PG0=192 (VolDn), PG11=203 (VolUp), PB2=34 (Action) */
    int pins[] = {192, 203, 34};
    const char *names[] = {"VolDn", "VolUp", "Action"};
    int i;
    for (i = 0; i < 3; i++) {
        gpio_export(pins[i]);
        gpio_direction(pins[i], "in");
    }
    spr("Button monitor (send any key to stop)\r\n");
    int flags = fcntl(ser_fd, F_GETFL, 0);
    fcntl(ser_fd, F_SETFL, flags | O_NONBLOCK);
    int prev[3] = {1, 1, 1};
    while (1) {
        for (i = 0; i < 3; i++) {
            int val = gpio_read(pins[i]);
            if (val == 0 && prev[i] == 1) {
                spr(names[i]); spr(" pressed\r\n");
            }
            prev[i] = val;
        }
        usleep(50000); /* 50ms polling */
        char c;
        if (read(ser_fd, &c, 1) > 0) break;
    }
    fcntl(ser_fd, F_SETFL, flags);
    spr("Button monitor stopped\r\n");
}

static void cmd_beep(const char *args);  /* forward declaration */
static void pa_gain_off(void);           /* forward declaration */

static void cmd_demo(void) {
    /* Interactive demo: buttons switch between display modes.
     * VolUp = next mode, VolDn = prev mode, Action = activate/toggle */
    spi_init_display();
    if (!spi_ready) return;

    /* Init button GPIOs: PB3=Left(35), PB2=Action(34), PB4=Right(36), PG0=VolDn(192) */
    int pins[] = {35, 34, 36, 192};
    int num_pins = 4;
    int i;
    for (i = 0; i < num_pins; i++) {
        gpio_export(pins[i]);
        gpio_direction(pins[i], "in");
    }

    const char *modes[] = {"clock", "hello", "scroll", "bright", "beep"};
    const int num_modes = 5;
    int mode = 0;
    int prev[4] = {1, 1, 1, 1};
    int running = 1;
    int brightness_val = 200;
    int clock_offset = 0;

    int flags = fcntl(ser_fd, F_GETFL, 0);
    fcntl(ser_fd, F_SETFL, flags | O_NONBLOCK);

    unsigned int up = get_uptime_secs();

    spr("Demo! < >=switch, O=select, serial=quit\r\n");

    while (running) {
        /* Poll buttons: [0]=Left(PB3), [1]=Action(PB2), [2]=Right(PB4), [3]=VolDn(PG0) */
        int btn_left = 0, btn_act = 0, btn_right = 0, btn_voldn = 0;
        int vals[4];
        for (i = 0; i < num_pins; i++) vals[i] = gpio_read(pins[i]);
        if (vals[0] == 0 && prev[0] == 1) btn_left = 1;   /* < */
        if (vals[1] == 0 && prev[1] == 1) btn_act = 1;     /* O */
        if (vals[2] == 0 && prev[2] == 1) btn_right = 1;   /* > */
        if (vals[3] == 0 && prev[3] == 1) btn_voldn = 1;   /* Vol- */
        for (i = 0; i < num_pins; i++) prev[i] = vals[i];

        if (btn_right) { mode = (mode + 1) % num_modes; spr("Mode: "); spr(modes[mode]); spr("\r\n"); }
        if (btn_left || btn_voldn) { mode = (mode + num_modes - 1) % num_modes; spr("Mode: "); spr(modes[mode]); spr("\r\n"); }

        /* Update display based on mode */
        unsigned char pixels[8][37];
        char tbuf[32];

        if (mode == 0) { /* Clock */
            unsigned int elapsed = get_uptime_secs() - up;
            int total = clock_offset + elapsed;
            total %= 86400;
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", total/3600, (total%3600)/60);
            if (btn_act) { clock_offset += 3600; spr("Hour+1\r\n"); }
        } else if (mode == 1) { /* Hello */
            snprintf(tbuf, sizeof(tbuf), "hello");
            if (btn_act) snprintf(tbuf, sizeof(tbuf), "WORLD");
        } else if (mode == 2) { /* Scroll - show mode name */
            snprintf(tbuf, sizeof(tbuf), "scroll");
            if (btn_act) {
                fcntl(ser_fd, F_SETFL, flags);
                cmd_scroll("LaMetric Time Recovery!");
                fcntl(ser_fd, F_SETFL, flags | O_NONBLOCK);
                continue;
            }
        } else if (mode == 3) { /* Brightness */
            if (btn_act) {
                brightness_val = (brightness_val + 50) % 256;
                text_brightness = brightness_val;
                i2c_cmd(0x20, brightness_val > 127 ? 127 : brightness_val);
            }
            snprintf(tbuf, sizeof(tbuf), "B:%d", brightness_val);
        } else if (mode == 4) { /* Beep */
            snprintf(tbuf, sizeof(tbuf), "beep");
            if (btn_act) {
                /* Try to beep */
                fcntl(ser_fd, F_SETFL, flags);
                cmd_beep("800 500");
                fcntl(ser_fd, F_SETFL, flags | O_NONBLOCK);
            }
        }

        render_text_at(tbuf, pixels, text_brightness, 8);
        unsigned char frame[FRAME_SIZE];
        build_frame(pixels, frame);
        if (shared_frame) memcpy(shared_frame, frame, FRAME_SIZE);
        if (spi_child <= 0) start_spi_loop();

        /* Check serial for quit */
        char c;
        if (read(ser_fd, &c, 1) > 0) running = 0;

        usleep(100000); /* 100ms update */
    }

    fcntl(ser_fd, F_SETFL, flags);
    spr("Demo stopped\r\n");
}

/* Set ALSA control by numid. Struct: 712 bytes, numid@0, value@72. ioctl=0xC2C85513 */
static void alsa_set_control(int ctl_fd, int numid, int value) {
    unsigned char buf[712];
    memset(buf, 0, 712);
    *(unsigned int *)(buf + 0) = numid;
    *(int *)(buf + 72) = value;    /* value.integer.value[0] */
    *(int *)(buf + 76) = value;    /* value.integer.value[1] */
    int r = ioctl(ctl_fd, 0xC2C85513, buf);
    spr("  ctl "); spr_num(numid); spr("="); spr_num(value);
    if (r < 0) { spr(" FAIL("); spr_num(errno); spr(")"); }
    spr("\r\n");
}

static void audio_setup_mixer(void) {
    /* Write OSS mixer mappings + set volumes.
     * Also set ALSA controls directly for unmuting. */
    int fd = open("/proc/asound/card0/oss_mixer", O_WRONLY);
    if (fd >= 0) {
        write(fd, "VOLUME \"Power Amplifier Volume\" 0\n", 34);
        write(fd, "PCM \"Power Amplifier\" 0\n", 24);
        close(fd);
    }
    mknod("/dev/mixer", S_IFCHR | 0666, makedev(14, 0));
    int mx = open("/dev/mixer", O_RDWR);
    if (mx >= 0) {
        int vol = 100 | (100 << 8);
        ioctl(mx, 0xC0044D00, &vol);
        vol = 100 | (100 << 8);
        ioctl(mx, 0xC0044D04, &vol);
        close(mx);
    }

    /* ALSA control interface: set PA volume to max and switches to ON.
     * sun4i-codec controls (from kernel source):
     * numid=1: "Power Amplifier Volume" integer 0-7 → set 7
     * numid=2: various DAPM switches → set 1
     * We set volumes to 7 and switches to 1 */
    fd = open("/dev/snd/controlC0", O_RDWR);
    if (fd >= 0) {
        alsa_set_control(fd, 1, 63); /* PA Volume = max (0-63 range) */
        int i;
        for (i = 2; i <= 20; i++) {
            alsa_set_control(fd, i, 1);  /* Enable all switches */
        }
        close(fd);
    }

    /* PA gain GPIOs PD25/PD26 only (NOT PD24 — kernel owns it) */
    gpio_export(121); gpio_direction(121, "out"); gpio_set(121, 1);
    gpio_export(122); gpio_direction(122, "out"); gpio_set(122, 1);
    spr("Mixer configured\r\n");
}

static void cmd_beep(const char *args) {
    /* beep [freq] [duration_ms] — play tone via OSS /dev/dsp.
     * ALSA driver manages codec clocks+registers, we just write PCM data. */
    int freq = 800, dur_ms = 500;
    if (*args) {
        freq = atoi(args);
        const char *p = args;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        if (*p) dur_ms = atoi(p);
    }
    if (freq < 100) freq = 100;
    if (freq > 10000) freq = 10000;
    if (dur_ms < 50) dur_ms = 50;
    if (dur_ms > 10000) dur_ms = 10000;

    /* Configure mixer via amixer fork/exec (proven to set DAPM correctly) */
    { pid_t p; int st;
      /* PA Volume max */
      p = fork(); if (p==0) { execl("/amixer","amixer","cset","numid=1","63",NULL); _exit(1); } waitpid(p,&st,0);
      /* Left Mixer Left DAC */
      p = fork(); if (p==0) { execl("/amixer","amixer","cset","numid=8","1",NULL); _exit(1); } waitpid(p,&st,0);
      /* Right Mixer Right DAC */
      p = fork(); if (p==0) { execl("/amixer","amixer","cset","numid=13","1",NULL); _exit(1); } waitpid(p,&st,0);
      /* PA DAC Playback Switch */
      p = fork(); if (p==0) { execl("/amixer","amixer","cset","numid=15","1",NULL); _exit(1); } waitpid(p,&st,0);
      /* PA Mute Switch (unmute) */
      p = fork(); if (p==0) { execl("/amixer","amixer","cset","numid=17","1",NULL); _exit(1); } waitpid(p,&st,0);
      /* PA gain GPIOs */
      gpio_export(121); gpio_direction(121, "out"); gpio_set(121, 1);
      gpio_export(122); gpio_direction(122, "out"); gpio_set(122, 1);
      spr("Mixer set via amixer\r\n");
    }

    /* Open OSS PCM device */
    mknod("/dev/dsp", S_IFCHR | 0666, makedev(14, 3));
    int dsp = open("/dev/dsp", O_WRONLY);
    if (dsp < 0) {
        spr("Cannot open /dev/dsp: "); spr_num(errno); spr("\r\n");
        return;
    }

    /* Configure: 16-bit signed LE, stereo, 48kHz */
    int val = 0x10; /* AFMT_S16_LE */
    ioctl(dsp, 0xC0045005, &val);
    val = 2; /* stereo — codec expects stereo */
    ioctl(dsp, 0xC0045006, &val);
    val = 48000;
    ioctl(dsp, 0xC0045002, &val);

    /* Clock now enabled at probe via driver patch. Let DAPM manage analog.
     * Just wait for DAPM to settle after /dev/dsp open. */
    usleep(200000);
    int rate = val;
    spr("Rate="); spr_num(rate); spr(" Ch=2\r\n");

    /* Generate square wave — stereo interleaved (L,R,L,R,...) */
    int num_frames = rate * dur_ms / 1000;
    int half_period = rate / (2 * freq);
    if (half_period < 1) half_period = 1;
    short *buf = malloc(num_frames * 4); /* 2 channels * 2 bytes */
    if (!buf) { close(dsp); spr("OOM\r\n"); return; }
    int i;
    for (i = 0; i < num_frames; i++) {
        short s = ((i / half_period) & 1) ? 32000 : -32000;
        buf[i*2] = s;     /* Left */
        buf[i*2+1] = s;   /* Right */
    }
    int written = write(dsp, buf, num_frames * 4);
    free(buf);
    /* Wait for playback to complete */
    ioctl(dsp, 0x5009, 0); /* SNDCTL_DSP_SYNC */
    close(dsp);

    /* Disable PA gain GPIOs to prevent idle buzzing */
    pa_gain_off();

    spr("Beep "); spr_num(freq); spr("Hz "); spr_num(dur_ms);
    spr("ms ("); spr_num(written); spr(" bytes)\r\n");
}

static void cmd_rawbeep(const char *args) {
    /* rawbeep [freq] [dur_ms] — PURE /dev/mem beep, bypasses ALSA entirely.
     * Exact copy of bare-metal beep_full_init2.c approach. */
    int freq = 800, dur_ms = 1000;
    if (*args) {
        freq = atoi(args);
        const char *p = args; while (*p && *p != ' ') p++; while (*p == ' ') p++;
        if (*p) dur_ms = atoi(p);
    }
    int memfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memfd < 0) { spr("No /dev/mem\r\n"); return; }

    /* Map CCU (0x01C20000) and codec (0x01C22000) */
    volatile unsigned int *ccu = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
                                       MAP_SHARED, memfd, 0x01C20000);
    volatile unsigned int *codec_pg = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
                                            MAP_SHARED, memfd, 0x01C22000);
    volatile unsigned int *gpio_pg = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
                                           MAP_SHARED, memfd, 0x01C20000);
    if (ccu == MAP_FAILED || codec_pg == MAP_FAILED) {
        spr("mmap fail\r\n"); close(memfd); return;
    }
    volatile unsigned int *codec = codec_pg + (0xC00/4);
    volatile unsigned int *gpio = gpio_pg + (0x800/4);
    volatile int d;

    /* Open /dev/dsp to trigger ALSA to configure PLL2 for 48kHz.
     * Keep it open (don't close) so PLL2 stays configured.
     * We write audio via PIO, not through /dev/dsp. */
    mknod("/dev/dsp", S_IFCHR | 0666, makedev(14, 3));
    int dsp_hold = open("/dev/dsp", O_WRONLY);
    if (dsp_hold >= 0) {
        int v = 0x10; ioctl(dsp_hold, 0xC0045005, &v);
        v = 2; ioctl(dsp_hold, 0xC0045006, &v);
        v = 48000; ioctl(dsp_hold, 0xC0045002, &v);
        /* Write a tiny bit of silence to start the DMA/trigger */
        short silence[256];
        memset(silence, 0, sizeof(silence));
        write(dsp_hold, silence, sizeof(silence));
        usleep(100000);
    }
    /* Ensure codec clock gate is open */
    ccu[0x140/4] |= (1U<<31);
    for (d=0; d<100000; d++);
    /* 4. GPIO: PD24=LOW, PD25/26=HIGH */
    { unsigned int c = gpio[0x78/4];
      c &= ~(0x7<<0); c |= (1<<0);
      c &= ~(0x7<<4); c |= (1<<4);
      c &= ~(0x7<<8); c |= (1<<8);
      gpio[0x78/4] = c;
    }
    { unsigned int dt = gpio[0x7C/4];
      dt &= ~(1<<24); dt |= (1<<25); dt |= (1<<26);
      gpio[0x7C/4] = dt;
    }
    for (d=0; d<200000; d++);
    /* 5. DAC DPC */
    codec[0x00/4] = (1U<<31);
    /* 6. FIFO flush + config */
    codec[0x04/4] = (1<<0);
    for (d=0; d<50000; d++);
    codec[0x04/4] = (0<<29)|(1<<26)|(1<<24)|(0x4<<8);
    /* 7. DAC analog — MUST include mixer routing bits!
     * DACAREN|DACALEN|MIXEN|LDACLMIXS|RDACRMIXS|DACPAS|MIXPAS|PA_MUTE|PA_VOL=63 */
    codec[0x10/4] = 0xE000C1FF;
    /* 7b. ADC_ACTL: PA_EN (bit 4) — CRITICAL for speaker output path */
    codec[0x28/4] |= (1 << 4);
    for (d=0; d<500000; d++);

    /* Readback all key registers */
    { char h[16];
      spr("PLL2="); snprintf(h,16,"0x%08X",ccu[0x08/4]); spr(h);
      spr(" APB0="); snprintf(h,16,"0x%08X",ccu[0x68/4]); spr(h);
      spr(" CLK="); snprintf(h,16,"0x%08X",ccu[0x140/4]); spr(h);
      spr("\r\nDPC="); snprintf(h,16,"0x%08X",codec[0x00/4]); spr(h);
      spr(" FIFOC="); snprintf(h,16,"0x%08X",codec[0x04/4]); spr(h);
      spr(" FIFOS="); snprintf(h,16,"0x%08X",codec[0x08/4]); spr(h);
      spr("\r\nACTL="); snprintf(h,16,"0x%08X",codec[0x10/4]); spr(h);
      spr(" ADC_ACTL="); snprintf(h,16,"0x%08X",codec[0x28/4]); spr(h);
      spr(" PD_DAT="); snprintf(h,16,"0x%08X",gpio[0x7C/4]); spr(h);
      spr("\r\n");
    }
    /* Check FIFO after a few writes to verify data flows */
    { int k;
      for (k = 0; k < 100; k++) {
        while (((codec[0x08/4]>>8)&0x7FFF) == 0);
        codec[0x0C/4] = 0x7FFF7FFF;
      }
      char h[16];
      spr("FIFOS after 100 writes="); snprintf(h,16,"0x%08X",codec[0x08/4]); spr(h);
      spr("\r\n");
    }
    spr("Raw playing "); spr_num(freq); spr("Hz\r\n");

    /* 8. Generate tone via PIO */
    int half = 48000/(2*freq);
    if (half < 1) half = 1;
    int total = 48000 * dur_ms / 1000;
    int toggle = 0, cnt = 0, j;
    for (j = 0; j < total; j++) {
        while (((codec[0x08/4]>>8)&0x7FFF) == 0);
        cnt++;
        if (cnt >= half) { cnt = 0; toggle = !toggle; }
        codec[0x0C/4] = toggle ? 0x7FFF7FFF : 0x80008000;
    }
    for (j = 0; j < 2000; j++) {
        while (((codec[0x08/4]>>8)&0x7FFF) == 0);
        codec[0x0C/4] = 0;
    }

    munmap((void*)ccu, 0x1000);
    munmap((void*)codec_pg, 0x1000);
    close(memfd);
    if (dsp_hold >= 0) close(dsp_hold);
    pa_gain_off();
    spr("Done\r\n");
}

static void cmd_i2c_write(const char *args) {
    /* i2c <reg_hex> <val_hex> */
    if (i2c_fd < 0) { spr("I2C not connected\r\n"); return; }
    int reg = parse_hex(args);
    const char *p = args;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    int val = parse_hex(p);
    i2c_cmd(reg, val);
    spr("Sent reg=0x"); char h[8]; snprintf(h,8,"%02x",reg); spr(h);
    spr(" val=0x"); snprintf(h,8,"%02x",val); spr(h); spr("\r\n");
}

/* GPIO tone: toggle PD24 (amp enable) at audio frequency via /dev/mem.
 * Bypasses codec AND headphone jack entirely — tests amp→speaker path.
 * "gptone [freq] [dur_ms]" */
static void cmd_gptone(const char *args) {
    int freq = 400, dur_ms = 3000;
    if (*args) {
        freq = atoi(args);
        const char *p = args; while (*p && *p != ' ') p++; while (*p == ' ') p++;
        if (*p) dur_ms = atoi(p);
    }
    if (freq < 100) freq = 100;
    if (freq > 4000) freq = 4000;
    int memfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memfd < 0) { spr("No /dev/mem\r\n"); return; }
    volatile unsigned int *base = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
                                        MAP_SHARED, memfd, 0x01C20000);
    if (base == MAP_FAILED) { spr("mmap fail\r\n"); close(memfd); return; }
    /* PIO base = 0x01C20800. PD_CFG3 at PIO+0x78, PD_DAT at PIO+0x7C */
    volatile unsigned int *pd_cfg3 = base + (0x800/4) + (0x78/4);
    volatile unsigned int *pd_dat = base + (0x800/4) + (0x7C/4);
    /* Set PD24 as output */
    unsigned int c = *pd_cfg3;
    c &= ~(0x7 << 0); c |= (1 << 0); /* PD24 bits [2:0] = output */
    *pd_cfg3 = c;
    /* Also set PD25/PD26 HIGH for gain */
    c = *pd_cfg3;
    c &= ~(0x7 << 4); c |= (1 << 4); /* PD25 output */
    c &= ~(0x7 << 8); c |= (1 << 8); /* PD26 output */
    *pd_cfg3 = c;
    *pd_dat |= (1 << 25) | (1 << 26); /* gain HIGH */

    int half_us = 500000 / freq; /* microseconds per half-period */
    int cycles = freq * dur_ms / 1000;
    spr("GPIO tone "); spr_num(freq); spr("Hz (PD24 toggle) ");
    spr_num(dur_ms); spr("ms\r\n");

    int i;
    for (i = 0; i < cycles; i++) {
        *pd_dat &= ~(1 << 24); /* LOW = PA on */
        usleep(half_us);
        *pd_dat |= (1 << 24);  /* HIGH = PA off */
        usleep(half_us);
    }
    *pd_dat |= (1 << 24); /* Leave HIGH (PA off) */
    munmap((void*)base, 0x1000);
    close(memfd);
    spr("Done\r\n");
}

/* Quick audio routing test: "atest ACTL_HEX [freq] [dur_ms]"
 * Plays a PIO tone with a specific DAC_ACTL register value.
 * This lets us test different mixer routing configurations. */
static void cmd_atest(const char *args) {
    /* atest ACTL [ADCACTL] [DPC] [freq] [dur]
     * All hex values for registers, decimal for freq/dur */
    unsigned int actl_val = 0xE000C1FF;
    unsigned int adcactl_val = 0x0000FF1F; /* aggressive: set all lower bits + PA_EN */
    unsigned int dpc_val = 0x80000000;
    int freq = 400, dur_ms = 3000;
    const char *p = args;
    while (*p == ' ') p++;
    if (*p) { actl_val = (unsigned int)parse_hex(p); while (*p && *p != ' ') p++; while (*p == ' ') p++; }
    if (*p) { adcactl_val = (unsigned int)parse_hex(p); while (*p && *p != ' ') p++; while (*p == ' ') p++; }
    if (*p) { dpc_val = (unsigned int)parse_hex(p); while (*p && *p != ' ') p++; while (*p == ' ') p++; }
    if (*p) { freq = atoi(p); while (*p && *p != ' ') p++; while (*p == ' ') p++; }
    if (*p) dur_ms = atoi(p);

    int memfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memfd < 0) { spr("No /dev/mem\r\n"); return; }
    volatile unsigned int *ccu = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, 0x01C20000);
    volatile unsigned int *codec_pg = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, 0x01C22000);
    if (ccu == MAP_FAILED || codec_pg == MAP_FAILED) { spr("mmap fail\r\n"); close(memfd); return; }
    volatile unsigned int *codec = codec_pg + (0xC00/4);
    volatile unsigned int *gpio = ccu + (0x800/4);
    volatile int d;

    /* Open /dev/dsp to configure PLL2 clocks */
    mknod("/dev/dsp", S_IFCHR | 0666, makedev(14, 3));
    int dsp = open("/dev/dsp", O_WRONLY);
    if (dsp >= 0) {
        int v = 0x10; ioctl(dsp, 0xC0045005, &v);
        v = 2; ioctl(dsp, 0xC0045006, &v);
        v = 48000; ioctl(dsp, 0xC0045002, &v);
        short silence[256]; memset(silence, 0, sizeof(silence));
        write(dsp, silence, sizeof(silence));
        usleep(100000);
    }
    /* Ensure clock gate open */
    ccu[0x140/4] |= (1U<<31);
    for (d=0; d<100000; d++);
    /* GPIO: PD24=LOW(PA on), PD25=HIGH(gain), PD26=HIGH(gain) */
    { unsigned int c = gpio[0x78/4];
      c &= ~(0x7<<0); c |= (1<<0);
      c &= ~(0x7<<4); c |= (1<<4);
      c &= ~(0x7<<8); c |= (1<<8);
      gpio[0x78/4] = c; }
    { unsigned int dt = gpio[0x7C/4];
      dt &= ~(1<<24); dt |= (1<<25); dt |= (1<<26);
      gpio[0x7C/4] = dt; }
    for (d=0; d<200000; d++);
    /* DAC DPC */
    codec[0x00/4] = dpc_val;
    /* FIFO: flush, then PRESERVE ALSA-configured bits!
     * ALSA sets FIFOC=0x01600F00 with mystery bits 25,22,21 that may be
     * critical for DAC operation. Only clear DRQ_EN for PIO mode. */
    codec[0x04/4] |= (1<<0); /* flush (self-clearing) */
    for (d=0; d<50000; d++);
    { unsigned int fifoc = codec[0x04/4]; /* read ALSA-configured value */
      char fh[16]; snprintf(fh,16,"%08X",fifoc); spr("FIFOC_ALSA=0x"); spr(fh); spr("\r\n");
      fifoc &= ~(1<<4);  /* clear DRQ_EN (PIO mode, no DMA) */
      codec[0x04/4] = fifoc; }
    /* DAC_ACTL */
    codec[0x10/4] = actl_val;
    /* ADC_ACTL */
    codec[0x28/4] = adcactl_val;
    for (d=0; d<500000; d++);

    char h[16];
    snprintf(h, 16, "%08X", actl_val); spr("ACTL=0x"); spr(h);
    snprintf(h, 16, "%08X", adcactl_val); spr(" ADC=0x"); spr(h);
    snprintf(h, 16, "%08X", dpc_val); spr(" DPC=0x"); spr(h);
    spr(" "); spr_num(freq); spr("Hz\r\n");

    /* PIO tone generation */
    int half = 48000/(2*freq); if (half < 1) half = 1;
    int total = 48000 * dur_ms / 1000;
    int toggle = 0, cnt = 0, j;
    for (j = 0; j < total; j++) {
        while (((codec[0x08/4]>>8)&0x7FFF) == 0);
        cnt++;
        if (cnt >= half) { cnt = 0; toggle = !toggle; }
        codec[0x0C/4] = toggle ? 0x7FFF7FFF : 0x80008000;
    }
    /* Silence */
    for (j = 0; j < 2000; j++) {
        while (((codec[0x08/4]>>8)&0x7FFF) == 0);
        codec[0x0C/4] = 0;
    }
    munmap((void*)ccu, 0x1000);
    munmap((void*)codec_pg, 0x1000);
    close(memfd);
    if (dsp >= 0) close(dsp);
    spr("Done\r\n");
}

/* ===========================================================================
 * AUDIO — comprehensive gain control + WAV playback
 * =========================================================================== */

/* Full codec register dump (0x01C22C00 + offsets 0x00-0x30) */
static void cmd_codec_dump(void) {
    int mfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mfd < 0) { spr("No /dev/mem\r\n"); return; }
    volatile unsigned int *cp = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
                                      MAP_SHARED, mfd, 0x01C22000);
    if (cp == MAP_FAILED) { spr("mmap fail\r\n"); close(mfd); return; }
    volatile unsigned int *c = cp + (0xC00/4);
    char h[16];
    const char *names[] = {
        "DPC   ", "FIFOC ", "FIFOS ", "TXDATA",
        "ACTL  ", "ADC_DP", "ADC_FC", "ADC_FS",
        "RXDATA", "resvd ", "ADCCTL", "resvd ",
        "TUNE  ", NULL
    };
    int i;
    for (i = 0; i <= 12; i++) {
        snprintf(h, 16, "%02X: 0x%08X", i*4, c[i]);
        spr(h);
        if (names[i]) { spr("  "); spr(names[i]); }
        spr("\r\n");
    }
    /* Also dump CCU PLL2 and codec clock gate */
    volatile unsigned int *ccu = cp; /* 0x01C20000 is at offset 0 from our map at 0x01C22000... no */
    /* Remap CCU */
    volatile unsigned int *ccu2 = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
                                        MAP_SHARED, mfd, 0x01C20000);
    if (ccu2 != MAP_FAILED) {
        snprintf(h, 16, "0x%08X", ccu2[0x08/4]); spr("PLL2="); spr(h);
        snprintf(h, 16, "0x%08X", ccu2[0x68/4]); spr(" APB0="); spr(h);
        snprintf(h, 16, "0x%08X", ccu2[0x140/4]); spr(" CGATE="); spr(h);
        spr("\r\n");
        /* GPIO PD data register */
        snprintf(h, 16, "0x%08X", ccu2[0x800/4 + 0x7C/4]); spr("PD_DAT="); spr(h);
        spr("\r\n");
        munmap((void*)ccu2, 0x1000);
    }
    munmap((void*)cp, 0x1000);
    close(mfd);
}

/* Maximize audio: enable FULL DAPM path + all gain stages.
 * The sun4i-codec DAPM path for A13:
 *   Left DAC → Left Mixer → Power Amplifier → PA Mute → HP → Speaker(GPIO)
 * EVERY switch in this chain must be ON. */
static void cmd_volume_max(void) {
    pid_t p; int st;
    spr("Enabling full audio path...\r\n");

    /* PA Volume = max (numid=1) */
    p=fork(); if(p==0){execl("/amixer","amixer","cset","numid=1","63",NULL);_exit(1);}waitpid(p,&st,0);

    /* Set ALL DAPM switches ON by name (more reliable than numid) */
    const char *controls[] = {
        "Left Mixer Left DAC Playback Switch",   /* LDACLMIXS: DAC→Mixer */
        "Right Mixer Right DAC Playback Switch",  /* RDACRMIXS: DAC→Mixer */
        "Right Mixer Left DAC Playback Switch",   /* LDACRMIXS: crossfeed */
        "Power Amplifier DAC Playback Switch",    /* DACPAS: DAC→PA direct */
        "Power Amplifier Mixer Playback Switch",  /* MIXPAS: Mixer→PA */
        "Power Amplifier Mute Switch",            /* PA_MUTE: unmute */
        NULL
    };
    int i;
    for (i = 0; controls[i]; i++) {
        p = fork();
        if (p == 0) {
            execl("/amixer", "amixer", "sset", controls[i], "on", NULL);
            _exit(1);
        }
        waitpid(p, &st, 0);
    }

    /* Also set by numid (backup — covers any controls we missed) */
    for (i = 2; i <= 20; i++) {
        char ni[16];
        snprintf(ni, 16, "numid=%d", i);
        p = fork();
        if (p == 0) { execl("/amixer", "amixer", "cset", ni, "1", NULL); _exit(1); }
        waitpid(p, &st, 0);
    }

    /* Set codec registers directly as final override */
    int mfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mfd >= 0) {
        volatile unsigned int *cp = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
                                          MAP_SHARED, mfd, 0x01C22000);
        if (cp != MAP_FAILED) {
            volatile unsigned int *c = cp + (0xC00/4);
            char h[16];
            /* DAC_DPC: Enable, DVOL=0 (max) */
            c[0x00/4] |= (1U << 31);
            /* DAC_ACTL: ALL enables + ALL switches + PA_VOL max
             * Bits: DACAREN(31)|DACALEN(30)|MIXEN(29)|LDACLMIXS(15)|
             *       RDACRMIXS(14)|DACPAS(8)|MIXPAS(7)|PA_MUTE(6)|PA_VOL(0-5)=63 */
            c[0x10/4] = 0xE000C1FF;
            snprintf(h, 16, "0x%08X", c[0x10/4]); spr("ACTL="); spr(h); spr("\r\n");
            /* ADC_ACTL: PA_EN(4) */
            c[0x28/4] |= (1 << 4);
            snprintf(h, 16, "0x%08X", c[0x28/4]); spr("ADC_ACTL="); spr(h); spr("\r\n");
            munmap((void*)cp, 0x1000);
        }
        close(mfd);
    }

    /* GPIO: PD24 LOW (PA on), PD25/26 HIGH (gain) */
    gpio_export(120); gpio_direction(120, "out"); gpio_set(120, 0);
    gpio_export(121); gpio_direction(121, "out"); gpio_set(121, 1);
    gpio_export(122); gpio_direction(122, "out"); gpio_set(122, 1);

    /* Set software boost to max (+20dB) */
    p = fork();
    if (p == 0) { execl("/amixer", "amixer", "sset", "Boost", "100%", NULL); _exit(1); }
    if (p > 0) { int st; waitpid(p, &st, 0); }

    spr("Audio path fully enabled\r\n");
}

/* Play a WAV file using aplay */
static void cmd_play(const char *path) {
    spr("Playing: "); spr(path); spr("\r\n");
    cmd_volume_max();
    pid_t p = fork();
    if (p == 0) {
        execl("/aplay", "aplay", "-D", "hw:0,0", path, NULL);
        _exit(127);
    }
    if (p > 0) { int st; waitpid(p, &st, 0); spr("Playback done\r\n"); }
    else spr("Fork failed\r\n");
    pa_gain_off();
}

/* Ensure audio amplifier is on with max gain */
static void audio_amp_on(void) {
    gpio_export(121); gpio_direction(121, "out"); gpio_set(121, 1); /* PD25 gain HIGH */
    gpio_export(122); gpio_direction(122, "out"); gpio_set(122, 1); /* PD26 gain HIGH */
    /* Set PA volume via ALSA */
    char vol_str[8]; snprintf(vol_str, 8, "%d", audio_volume);
    pid_t p = fork();
    if (p == 0) { execl("/amixer","amixer","cset","numid=1",vol_str,NULL); _exit(1); }
    if (p > 0) { int st; waitpid(p, &st, 0); }
}

/* Quick notification beep — generates and plays a short tone via aplay.
 * Non-verbose, designed to be called from notify/timer without flooding serial. */
static int audio_initialized = 0;
static void audio_ensure_init(void) {
    if (!audio_initialized) { cmd_volume_max(); audio_initialized = 1; }
}

static void play_quick_tone(int freq, int dur_ms) {
    audio_ensure_init();
    int rate = 48000, channels = 2, bits = 16;
    int num_frames = rate * dur_ms / 1000;
    int data_size = num_frames * channels * (bits / 8);
    int wfd = open("/tmp/ntone.wav", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (wfd < 0) return;
    unsigned char hdr[44];
    memset(hdr, 0, 44);
    memcpy(hdr, "RIFF", 4); *(unsigned int *)(hdr+4) = 36+data_size;
    memcpy(hdr+8, "WAVEfmt ", 8); *(unsigned int *)(hdr+16) = 16;
    *(unsigned short *)(hdr+20) = 1; *(unsigned short *)(hdr+22) = channels;
    *(unsigned int *)(hdr+24) = rate;
    *(unsigned int *)(hdr+28) = rate*channels*(bits/8);
    *(unsigned short *)(hdr+32) = channels*(bits/8);
    *(unsigned short *)(hdr+34) = bits;
    memcpy(hdr+36, "data", 4); *(unsigned int *)(hdr+40) = data_size;
    write(wfd, hdr, 44);
    short buf[512];
    int half = rate/(2*freq); if (half<1) half=1;
    int written=0, toggle=0, cnt=0, i;
    while (written < num_frames) {
        int chunk = num_frames-written; if (chunk>256) chunk=256;
        for (i=0; i<chunk; i++) {
            short s = toggle ? 32700 : -32700;
            buf[i*2]=s; buf[i*2+1]=s;
            if (++cnt >= half) { cnt=0; toggle=!toggle; }
        }
        write(wfd, buf, chunk*4); written += chunk;
    }
    close(wfd);
    /* Play via aplay (fork, no verbose output) */
    pid_t p = fork();
    if (p == 0) {
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) { dup2(null,1); dup2(null,2); close(null); }
        execl("/aplay", "aplay", "-D", "hw:0,0", "/tmp/ntone.wav", NULL);
        _exit(127);
    }
    if (p > 0) { int st; waitpid(p, &st, 0); }
}

/* Play a WAV file quietly (no serial output) */
static void play_wav_quiet(const char *path) {
    audio_ensure_init();
    pid_t p = fork();
    if (p == 0) {
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) { dup2(null,1); dup2(null,2); close(null); }
        execl("/aplay", "aplay", "-D", "hw:0,0", path, NULL);
        _exit(127);
    }
    if (p > 0) { int st; waitpid(p, &st, 0); }
}

/* Generate a WAV test tone and play via aplay (bypasses OSS/DAPM issues) */
static void cmd_testtone(const char *args) {
    int freq = 800, dur_ms = 2000;
    if (*args) {
        freq = atoi(args);
        const char *p = args; while (*p && *p != ' ') p++; while (*p == ' ') p++;
        if (*p) dur_ms = atoi(p);
    }
    int rate = 48000, channels = 2, bits = 16;
    int num_frames = rate * dur_ms / 1000;
    int data_size = num_frames * channels * (bits / 8);

    spr("Generating "); spr_num(freq); spr("Hz tone ("); spr_num(dur_ms); spr("ms)...\r\n");

    /* Write WAV file to /tmp */
    int wfd = open("/tmp/tone.wav", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (wfd < 0) { spr("Cannot create /tmp/tone.wav\r\n"); return; }

    /* WAV header (44 bytes) */
    unsigned char hdr[44];
    memset(hdr, 0, 44);
    memcpy(hdr, "RIFF", 4);
    *(unsigned int *)(hdr + 4) = 36 + data_size;
    memcpy(hdr + 8, "WAVEfmt ", 8);
    *(unsigned int *)(hdr + 16) = 16;           /* chunk size */
    *(unsigned short *)(hdr + 20) = 1;          /* PCM */
    *(unsigned short *)(hdr + 22) = channels;
    *(unsigned int *)(hdr + 24) = rate;
    *(unsigned int *)(hdr + 28) = rate * channels * (bits / 8);
    *(unsigned short *)(hdr + 32) = channels * (bits / 8);
    *(unsigned short *)(hdr + 34) = bits;
    memcpy(hdr + 36, "data", 4);
    *(unsigned int *)(hdr + 40) = data_size;
    write(wfd, hdr, 44);

    /* Generate PCM: square wave at full volume */
    short buf[1024]; /* 512 stereo frames per chunk */
    int half_period = rate / (2 * freq);
    if (half_period < 1) half_period = 1;
    int written = 0, toggle = 0, cnt = 0, i;
    while (written < num_frames) {
        int chunk = num_frames - written;
        if (chunk > 512) chunk = 512;
        for (i = 0; i < chunk; i++) {
            short s = toggle ? 32000 : -32000;
            buf[i * 2] = s;
            buf[i * 2 + 1] = s;
            cnt++;
            if (cnt >= half_period) { cnt = 0; toggle = !toggle; }
        }
        write(wfd, buf, chunk * 4);
        written += chunk;
    }
    close(wfd);

    spr("WAV written. Setting volume...\r\n");
    cmd_volume_max();

    spr("Playing via aplay (native ALSA)...\r\n");
    {
        int pfd[2]; pipe(pfd);
        pid_t ap = fork();
        if (ap == 0) {
            close(pfd[0]);
            dup2(pfd[1], 1); dup2(pfd[1], 2);
            close(pfd[1]);
            execl("/aplay", "aplay", "-v", "-D", "default", "/tmp/tone.wav", NULL);
            _exit(127);
        }
        close(pfd[1]);
        /* Read aplay output and send to serial */
        char rb[256]; int rn;
        while ((rn = read(pfd[0], rb, sizeof(rb)-1)) > 0) { rb[rn] = 0; spr(rb); }
        close(pfd[0]);
        int st;
        waitpid(ap, &st, 0);
        spr("\r\naplay exit="); spr_num(WEXITSTATUS(st)); spr("\r\n");
    }

    /* Also dump codec state DURING/AFTER playback */
    cmd_codec_dump();
    pa_gain_off();
    spr("Test tone done\r\n");
}

/* ===========================================================================
 * PRODUCTION CAROUSEL — widgets, weather, notifications, auto-brightness
 * =========================================================================== */

/* PA gain GPIOs cleanup — PD25/PD26 stay HIGH after beep, causing idle noise */
static void pa_gain_off(void) {
    gpio_export(121); gpio_direction(121, "out"); gpio_set(121, 0);
    gpio_export(122); gpio_direction(122, "out"); gpio_set(122, 0);
}

/* Render a single font glyph at a specific pixel column */
static void render_char(unsigned char pixels[8][37], char ch,
                        int start_col, unsigned char bright) {
    if ((unsigned char)ch > 127) return;
    const unsigned char *glyph = font5x7[(unsigned char)ch];
    int c, r;
    for (c = 0; c < 5; c++) {
        int col = start_col + c;
        if (col < 0 || col >= 37) continue;
        unsigned char bits = glyph[c];
        for (r = 0; r < 7; r++) {
            if (bits & (1 << r))
                pixels[r + 1][col] = bright; /* +1 for vertical center */
        }
    }
}

/* Render clock face HH:MM with blinking colon — tighter than render_text_at */
static void render_clock_face(unsigned char pixels[8][37],
                              int h, int m, int blink, unsigned char bright) {
    memset(pixels, 0, 8 * 37);
    render_char(pixels, '0' + h / 10, 9, bright);
    render_char(pixels, '0' + h % 10, 15, bright);
    if (blink) {
        pixels[3][21] = bright;   /* colon top dot (+1 for center) */
        pixels[5][21] = bright;   /* colon bottom dot (+1 for center) */
    }
    render_char(pixels, '0' + m / 10, 23, bright);
    render_char(pixels, '0' + m % 10, 29, bright);
}

/* Timer: "timer [MM:SS]" — countdown timer or stopwatch.
 * "timer 5:00" starts a 5-minute countdown.
 * "timer" starts/pauses a stopwatch.
 * "timer reset" resets. */
static void cmd_timer(const char *args) {
    while (*args == ' ') args++;
    if (strncmp(args, "reset", 5) == 0) {
        timer_running = 0; timer_target_secs = 0; timer_paused_secs = 0;
        spr("Timer reset\r\n"); return;
    }
    if (*args >= '0' && *args <= '9') {
        /* Parse MM:SS */
        int m = 0, s = 0;
        const char *p = args;
        while (*p >= '0' && *p <= '9') { m = m * 10 + (*p - '0'); p++; }
        if (*p == ':') { p++; while (*p >= '0' && *p <= '9') { s = s * 10 + (*p - '0'); p++; } }
        timer_target_secs = m * 60 + s;
        timer_paused_secs = 0;
        timer_start_up = get_uptime_secs();
        timer_running = 1;
        spr("Countdown: "); spr_num(m); spr(":"); if (s<10) spr("0"); spr_num(s); spr("\r\n");
        return;
    }
    /* Toggle start/pause */
    if (timer_running) {
        timer_paused_secs += get_uptime_secs() - timer_start_up;
        timer_running = 0;
        spr("Timer paused\r\n");
    } else {
        timer_start_up = get_uptime_secs();
        timer_running = 1;
        spr("Timer started\r\n");
    }
}

/* Get current timer display value (returns seconds, negative = countdown expired) */
static int timer_get_display(void) {
    int elapsed = timer_paused_secs;
    if (timer_running) elapsed += get_uptime_secs() - timer_start_up;
    if (timer_target_secs > 0) return timer_target_secs - elapsed; /* countdown */
    return elapsed; /* stopwatch */
}

/* Render timer face: MM:SS with icon */
static void render_timer_face(unsigned char pixels[8][37],
                              int secs, int blink, unsigned char bright) {
    memset(pixels, 0, 8 * 37);
    int neg = 0;
    if (secs < 0) { neg = 1; secs = -secs; }
    int m = secs / 60;
    int s = secs % 60;
    if (m > 99) m = 99;
    render_char(pixels, '0' + m / 10, 9, bright);
    render_char(pixels, '0' + m % 10, 15, bright);
    if (blink || !timer_running) {
        pixels[3][21] = bright;  /* +1 for center */
        pixels[5][21] = bright;
    }
    render_char(pixels, '0' + s / 10, 23, bright);
    render_char(pixels, '0' + s % 10, 29, bright);
    /* Flash bottom row if countdown expired */
    if (neg) {
        int c;
        for (c = 9; c < 35; c++) pixels[7][c] = (blink ? bright : 0);
    }
}

/* Store weather data: "weather <icon> <temp> [condition] [hilo] [extra]"
 * Fields separated by | character for multi-field: "weather cloud 48F|Overcast|H:52 L:38|Wind 5mph" */
static void cmd_weather_set(const char *args) {
    int i = 0;
    const char *p = args;
    /* First word = icon */
    while (*p && *p != ' ' && i < 15) weather_icon_name[i++] = *p++;
    weather_icon_name[i] = 0;
    while (*p == ' ') p++;
    /* Rest: temp|condition|hilo|extra (separated by |) */
    char *fields[4] = {weather_temp_str, weather_cond_str, weather_hilo_str, weather_extra_str};
    int sizes[4] = {15, 31, 23, 23};
    int f;
    for (f = 0; f < 4; f++) {
        i = 0;
        while (*p && *p != '|' && i < sizes[f]) fields[f][i++] = *p++;
        fields[f][i] = 0;
        if (*p == '|') p++;
    }
    spr("Weather: "); spr(weather_icon_name); spr(" "); spr(weather_temp_str); spr("\r\n");
}

/* Store metric: "metric <idx> <icon> <value> [trend]"
 * Or: "metric <name> <icon> <value> [trend]" */
static void cmd_metric(const char *args) {
    const char *p = args;
    while (*p == ' ') p++;
    /* Parse name or index */
    char name[16]; int i = 0;
    while (*p && *p != ' ' && i < 15) name[i++] = *p++;
    name[i] = 0;
    while (*p == ' ') p++;
    /* Find or create metric slot */
    int idx = -1;
    /* Check if name is a number (index) */
    if (name[0] >= '0' && name[0] <= '9') {
        idx = atoi(name);
    } else {
        /* Find by name */
        int j;
        for (j = 0; j < metric_count; j++) {
            if (strncmp(metric_names[j], name, 15) == 0) { idx = j; break; }
        }
        if (idx < 0 && metric_count < MAX_METRICS) {
            idx = metric_count++;
            strncpy(metric_names[idx], name, 15);
            metric_names[idx][15] = 0;
        }
    }
    if (idx < 0 || idx >= MAX_METRICS) { spr("Metric full\r\n"); return; }
    if (idx >= metric_count) metric_count = idx + 1;
    /* Parse icon */
    i = 0;
    while (*p && *p != ' ' && i < 15) metric_icons[idx][i++] = *p++;
    metric_icons[idx][i] = 0;
    while (*p == ' ') p++;
    /* Parse value */
    i = 0;
    while (*p && *p != ' ' && i < 23) metric_values[idx][i++] = *p++;
    metric_values[idx][i] = 0;
    while (*p == ' ') p++;
    /* Parse trend (optional) */
    i = 0;
    while (*p && *p != ' ' && i < 7) metric_trends[idx][i++] = *p++;
    metric_trends[idx][i] = 0;
    /* Store name if it was an index */
    if (name[0] >= '0' && name[0] <= '9' && !metric_names[idx][0]) {
        snprintf(metric_names[idx], 16, "M%d", idx);
    }
    spr("Metric["); spr_num(idx); spr("]: "); spr(metric_names[idx]);
    spr(" = "); spr(metric_values[idx]); spr("\r\n");
}

/* Push notification: "notify [icon] <text>" */
static void cmd_notify_push(const char *args) {
    if (notif_count >= MAX_NOTIFS) {
        /* Drop oldest */
        int j;
        for (j = 0; j < MAX_NOTIFS - 1; j++) {
            memcpy(notif_texts[j], notif_texts[j + 1], 64);
            memcpy(notif_icons[j], notif_icons[j + 1], 16);
            notif_expires[j] = notif_expires[j + 1];
        }
        notif_count = MAX_NOTIFS - 1;
    }

    const char *p = args;
    char first_word[16];
    int i = 0;
    while (*p && *p != ' ' && i < 15) first_word[i++] = *p++;
    first_word[i] = 0;

    /* Check if first word is a known icon */
    if (find_icon(first_word)) {
        strncpy(notif_icons[notif_count], first_word, 15);
        notif_icons[notif_count][15] = 0;
        while (*p == ' ') p++;
    } else {
        strncpy(notif_icons[notif_count], "bell", 15);
        p = args; /* whole string is text */
    }

    /* Check for sound:<name> parameter before text */
    char sound_name[24] = "positive6"; /* default sound */
    if (strncmp(p, "sound:", 6) == 0) {
        p += 6; i = 0;
        while (*p && *p != ' ' && i < 23) sound_name[i++] = *p++;
        sound_name[i] = 0;
        while (*p == ' ') p++;
    }

    strncpy(notif_texts[notif_count], p, 63);
    notif_texts[notif_count][63] = 0;
    notif_expires[notif_count] = get_uptime_secs() + 30;
    notif_count++;
    spr("Notify: "); spr(notif_texts[notif_count - 1]); spr("\r\n");

    /* Play requested notification sound */
    char spath[64];
    snprintf(spath, 64, "/sounds/%s.wav", sound_name);
    if (file_exists(spath))
        play_wav_quiet(spath);
    else
        play_quick_tone(880, 200);
}

/* Dismiss oldest notification */
static void cmd_dismiss(void) {
    if (notif_count <= 0) { spr("No notifications\r\n"); return; }
    int j;
    for (j = 0; j < notif_count - 1; j++) {
        memcpy(notif_texts[j], notif_texts[j + 1], 64);
        memcpy(notif_icons[j], notif_icons[j + 1], 16);
        notif_expires[j] = notif_expires[j + 1];
    }
    notif_count--;
    spr("Dismissed\r\n");
}

/* Enhanced time set: "time HH:MM" or "time HH:MM:SS" */
static void cmd_carousel(void); /* forward declaration */

/* Render text with auto-scroll if too long for the display.
 * icon_cols = number of columns reserved for icon (usually 10).
 * scroll_pos = current pixel offset (caller increments this).
 * Returns text width in pixels so caller knows if scrolling is needed. */
static int render_text_scroll(const char *text, unsigned char pixels[8][37],
                              unsigned char bright, int icon_cols, int scroll_pos) {
    int text_width = 0;
    const char *p = text;
    while (*p) { text_width += 6; p++; }
    if (text_width > 0) text_width--;

    int avail = 37 - icon_cols;
    memset(pixels, 0, 8 * 37);

    if (text_width <= avail || scroll_mode == 4) {
        /* Fits or no-scroll mode — render static (clips if too long) */
        render_text_at(text, pixels, bright, icon_cols);
    } else {
        /* Render text into wide buffer */
        unsigned char wide[8][256];
        memset(wide, 0, sizeof(wide));
        int x = 0;
        p = text;
        while (*p && x < 256) {
            unsigned char ch = *p++;
            if (ch >= 128) continue;
            const unsigned char *glyph = font5x7[(unsigned char)ch];
            int col;
            for (col = 0; col < 5 && x < 256; col++, x++) {
                unsigned char bits = glyph[col];
                int row;
                for (row = 0; row < 7; row++) {
                    if (bits & (1 << row))
                        wide[row + 1][x] = bright;
                }
            }
            x++;
        }

        int overflow = text_width - avail;
        int off;

        if (scroll_mode == 3 || (scroll_mode == 0 && overflow <= avail)) {
            /* BOUNCE mode: ping-pong. Pause 8 frames at each end. */
            int pause = 8;
            int cycle = overflow + pause + overflow + pause;
            int pos = scroll_pos % cycle;
            if (pos < pause) off = 0;
            else if (pos < pause + overflow) off = pos - pause;
            else if (pos < pause + overflow + pause) off = overflow;
            else off = overflow - (pos - pause - overflow - pause);
        } else if (scroll_mode == 2) {
            /* Right-to-left continuous scroll */
            off = scroll_pos % (text_width + 10);
        } else if (scroll_mode == 1) {
            /* Left-to-right continuous scroll (reverse direction) */
            int cycle = text_width + 10;
            off = cycle - (scroll_pos % cycle) - avail;
            if (off < 0) off = 0;
        } else {
            /* Auto / default: wrap for long text */
            off = scroll_pos % (text_width + 10);
        }

        int r, c;
        for (r = 0; r < 8; r++) {
            for (c = 0; c < avail; c++) {
                int src = off + c;
                if (src >= 0 && src < 256) pixels[r][icon_cols + c] = wide[r][src];
            }
        }
    }
    return text_width;
}

static void cmd_time(const char *arg) {
    int h = 0, m = 0, s = 0;
    const char *p = arg;
    while (*p >= '0' && *p <= '9') { h = h * 10 + (*p - '0'); p++; }
    if (*p == ':') p++;
    while (*p >= '0' && *p <= '9') { m = m * 10 + (*p - '0'); p++; }
    if (*p == ':') { p++; while (*p >= '0' && *p <= '9') { s = s * 10 + (*p - '0'); p++; } }
    time_hours = h; time_minutes = m; time_seconds = s;
    time_set_uptime = get_uptime_secs();
    spr("Time: ");
    char tb[16];
    snprintf(tb, sizeof(tb), "%02d:%02d:%02d", h, m, s);
    spr(tb); spr("\r\n");
    /* Auto-start carousel if not already running */
    if (!carousel_active) {
        spr("Auto-starting carousel...\r\n");
        cmd_carousel();
    }
}

/* Process command while carousel is running.
 * Returns: 0=handled, 1=exit carousel */
static int carousel_process_cmd(char *l) {
    while (*l == ' ') l++;
    if (!*l) return 0;

    if (strncmp(l, "exit", 4) == 0 || strncmp(l, "stop", 4) == 0) return 1;
    if (strncmp(l, "weather ", 8) == 0) { cmd_weather_set(l + 8); return 0; }
    if (strncmp(l, "metric ", 7) == 0) { cmd_metric(l + 7); return 0; }
    if (strncmp(l, "sounds ", 7) == 0) {
        /* Play a named sound: "sounds positive1" → /sounds/positive1.wav */
        char path[64]; snprintf(path, 64, "/sounds/%s.wav", l + 7);
        if (file_exists(path)) play_wav_quiet(path);
        else { spr("Sound not found: "); spr(l + 7); spr("\r\n"); }
        return 0;
    }
    if (strncmp(l, "notify ", 7) == 0)  { cmd_notify_push(l + 7); return 0; }
    if (strncmp(l, "dismiss", 7) == 0)  { cmd_dismiss(); return 0; }
    if (strncmp(l, "timer", 5) == 0)   { char *a=l+5; while(*a==' ')a++; cmd_timer(a); return 0; }
    if (strncmp(l, "time ", 5) == 0)    { cmd_time(l + 5); return 0; }
    if (strncmp(l, "settime ", 8) == 0) { cmd_settime(l + 8); return 0; }
    if (strncmp(l, "bright ", 7) == 0)  { cmd_bright(l + 7); return 0; }
    if (strncmp(l, "testtone", 8) == 0) {
        char *a = l + 8; while (*a == ' ') a++;
        cmd_testtone(a);
        return 0;
    }
    if (strncmp(l, "beep", 4) == 0) {
        char *a = l + 4; while (*a == ' ') a++;
        cmd_beep(a);
        pa_gain_off();
        return 0;
    }
    if (strncmp(l, "icon ", 5) == 0) {
        /* Show icon on S1 temporarily */
        spi_init_display();
        if (spi_ready) {
            unsigned char fr[FRAME_SIZE];
            memset(fr, 0, FRAME_SIZE);
            render_icon(l + 5, fr);
            if (shared_frame) memcpy(shared_frame, fr, FRAME_SIZE);
        }
        return 0;
    }
    if (strncmp(l, "icontext ", 9) == 0) {
        /* Parse icon + text and show */
        char *iname = l + 9;
        char *txt = iname;
        while (*txt && *txt != ' ') txt++;
        if (*txt) *txt++ = 0;
        unsigned char px[8][37];
        render_text_at(txt, px, text_brightness, 10);
        unsigned char fr[FRAME_SIZE];
        build_frame(px, fr);
        render_icon(iname, fr);
        if (shared_frame) memcpy(shared_frame, fr, FRAME_SIZE);
        if (spi_child <= 0) start_spi_loop();
        return 0;
    }

    spr("? "); spr(l); spr("\r\n");
    return 0;
}

/* Production widget carousel */
static void cmd_carousel(void) {
    spi_init_display();
    if (!spi_ready) { spr("SPI not ready\r\n"); return; }
    if (time_hours < 0) { spr("Set time first: time HH:MM\r\n"); return; }

    int btn_pins[] = {35, 34, 36, 192, 203};
    int num_btns = 5, i;
    for (i = 0; i < num_btns; i++) { gpio_export(btn_pins[i]); gpio_direction(btn_pins[i], "in"); }

    int prev_btn[5] = {1, 1, 1, 1, 1};
    unsigned int last_lux_time = 0;
    int blink = 0, scroll_pos = 0, scroll_frame = 0;
    carousel_active = 1;

    const char *app_names[] = {"Clock", "Weather", "Metrics", "Timer", "Notifs", "Sounds"};
    spr("\r\n========================================\r\n");
    spr("  LaMetric App Launcher\r\n");
    spr("  O=next app | <>=in-app | vol+/-\r\n");
    spr("========================================\r\n\r\n");

    int flags = fcntl(ser_fd, F_GETFL, 0);
    fcntl(ser_fd, F_SETFL, flags | O_NONBLOCK);
    char cmd_buf[256]; int cmd_pos = 0, running = 1;

    while (running) {
        unsigned int now = get_uptime_secs();

        /* Expire old notifications */
        while (notif_count > 0 && notif_expires[0] && now >= notif_expires[0]) {
            int j;
            for (j = 0; j < notif_count - 1; j++) {
                memcpy(notif_texts[j], notif_texts[j + 1], 64);
                memcpy(notif_icons[j], notif_icons[j + 1], 16);
                notif_expires[j] = notif_expires[j + 1];
            }
            notif_count--;
            if (notif_idx >= notif_count) notif_idx = 0;
        }

        /* Button polling */
        int vals[5];
        for (i = 0; i < num_btns; i++) { int v = gpio_read(btn_pins[i]); vals[i] = (v == 0) ? 0 : 1; }
        int btn_left  = (vals[0] == 0 && prev_btn[0] == 1);
        int btn_act   = (vals[1] == 0 && prev_btn[1] == 1);
        int btn_right = (vals[2] == 0 && prev_btn[2] == 1);
        int btn_voldn = (vals[3] == 0 && prev_btn[3] == 1);
        int btn_volup = (vals[4] == 0 && prev_btn[4] == 1);
        for (i = 0; i < num_btns; i++) prev_btn[i] = vals[i];

        /* O = next app */
        if (btn_act) {
            current_app = (current_app + 1) % NUM_APPS;
            scroll_pos = 0;
            spr("App: "); spr(app_names[current_app]); spr("\r\n");
        }

        /* < > = in-app navigation */
        if (btn_right) {
            switch (current_app) {
                case APP_CLOCK:   clock_mode = (clock_mode + 1) % 4; break;
                case APP_WEATHER: weather_mode = (weather_mode + 1) % 4; break;
                case APP_METRICS: if (metric_count > 0) metric_idx = (metric_idx + 1) % metric_count; break;
                case APP_TIMER:   cmd_timer(""); break; /* start/stop */
                case APP_NOTIF:   if (notif_count > 0) notif_idx = (notif_idx + 1) % notif_count; break;
                case APP_SOUNDS: {
                    /* > on Sounds = next sound + play it */
                    int cnt = 0; while (sound_list[cnt]) cnt++;
                    sound_idx = (sound_idx + 1) % cnt;
                    char sp[64]; snprintf(sp, 64, "/sounds/%s.wav", sound_list[sound_idx]);
                    if (file_exists(sp)) play_wav_quiet(sp);
                    break;
                }
            }
            scroll_pos = 0;
        }
        if (btn_left) {
            switch (current_app) {
                case APP_CLOCK:   clock_mode = (clock_mode + 3) % 4; break;
                case APP_WEATHER: weather_mode = (weather_mode + 3) % 4; break;
                case APP_METRICS: if (metric_count > 0) metric_idx = (metric_idx + metric_count - 1) % metric_count; break;
                case APP_TIMER:   cmd_timer("reset"); break;
                case APP_NOTIF:   cmd_dismiss(); break;
                case APP_SOUNDS: {
                    int cnt = 0; while (sound_list[cnt]) cnt++;
                    sound_idx = (sound_idx + cnt - 1) % cnt;
                    char sp[64]; snprintf(sp, 64, "/sounds/%s.wav", sound_list[sound_idx]);
                    if (file_exists(sp)) play_wav_quiet(sp);
                    break;
                }
            }
            scroll_pos = 0;
        }

        /* Volume buttons */
        if (btn_voldn || btn_volup) {
            if (btn_voldn) { audio_volume -= 8; if (audio_volume < 0) audio_volume = 0; }
            if (btn_volup) { audio_volume += 8; if (audio_volume > 63) audio_volume = 63; }
            vol_display_until = now + 2;
            spr("Vol:"); spr_num(audio_volume); spr("\r\n");
        }

        /* Auto-brightness every 10 seconds */
        if (i2c_fd >= 0 && now - last_lux_time >= 10) {
            unsigned char lx;
            ioctl(i2c_fd, I2C_SLAVE, 0x29);
            lx = 0x04; write(i2c_fd, &lx, 1); usleep(10000);
            lx = 0x08; write(i2c_fd, &lx, 1); usleep(200000);
            lx = 0x30; write(i2c_fd, &lx, 1); usleep(10000);
            unsigned char lx_buf[2]; int lx_n = read(i2c_fd, lx_buf, 2);
            lx = 0x8C; write(i2c_fd, &lx, 1);
            ioctl(i2c_fd, I2C_SLAVE, 0x21);
            if (lx_n == 2) {
                int raw = (lx_buf[0] | (lx_buf[1] << 8)) & 0x7FFF;
                int lux = raw / 6, br;
                if (lux < 5) br = lux_bright_min;
                else if (lux < 300) br = lux_bright_min + (lux * (lux_bright_max - lux_bright_min) / 300);
                else br = lux_bright_max;
                i2c_cmd(0x20, br > 127 ? 127 : br);
                text_brightness = (unsigned char)(br > 255 ? 255 : br);
                if (text_brightness < 15) text_brightness = 15;
            }
            last_lux_time = now;
        }

        /* === RENDER === */
        unsigned char pixels[8][37];
        unsigned char frame[FRAME_SIZE];
        memset(pixels, 0, sizeof(pixels));
        memset(frame, 0, FRAME_SIZE);

        /* Volume overlay takes priority */
        if (vol_display_until && now < vol_display_until) {
            char vb[16]; snprintf(vb, 16, "Vol %d", audio_volume);
            render_text_at(vb, pixels, text_brightness, 8);
            build_frame(pixels, frame);
            render_icon("music", frame);
            if (shared_frame) memcpy(shared_frame, frame, FRAME_SIZE);
            if (spi_child <= 0) start_spi_loop();
            usleep(100000); continue;
        }
        if (vol_display_until && now >= vol_display_until) vol_display_until = 0;

        unsigned int elapsed = now - time_set_uptime;
        int total_secs = (time_hours * 3600 + time_minutes * 60 + time_seconds + elapsed) % 86400;
        int cur_h = total_secs / 3600, cur_m = (total_secs % 3600) / 60;
        blink = (total_secs % 2 == 0);

        switch (current_app) {
        case APP_CLOCK: {
            if (clock_mode == 0) {
                /* 24h: HH:MM */
                render_clock_face(pixels, cur_h, cur_m, blink, text_brightness);
                build_frame(pixels, frame);
                render_icon("clock", frame);
            } else if (clock_mode == 1) {
                /* 12h: H:MM AM/PM */
                int h12 = cur_h % 12; if (h12 == 0) h12 = 12;
                char tb[12]; snprintf(tb, 12, "%d:%02d%s", h12, cur_m, cur_h < 12 ? "am" : "pm");
                render_text_scroll(tb, pixels, text_brightness, 10, scroll_pos);
                build_frame(pixels, frame);
                render_icon("clock", frame);
            } else if (clock_mode == 2) {
                /* Uptime display */
                char tb[20]; snprintf(tb, 20, "Up %dh%dm", (int)(elapsed/3600), (int)((elapsed%3600)/60));
                render_text_scroll(tb, pixels, text_brightness, 10, scroll_pos);
                build_frame(pixels, frame);
                render_icon("info", frame);
            } else {
                /* Seconds display */
                int cur_s = total_secs % 60;
                char tb[16]; snprintf(tb, 16, "%02d:%02d:%02d", cur_h, cur_m, cur_s);
                render_text_scroll(tb, pixels, text_brightness, 10, scroll_pos);
                build_frame(pixels, frame);
                render_icon("star", frame);
            }
            break;
        }
        case APP_WEATHER: {
            const char *txt = weather_temp_str;
            const char *ico = weather_icon_name[0] ? weather_icon_name : "cloud";
            if (weather_mode == 1 && weather_cond_str[0]) txt = weather_cond_str;
            else if (weather_mode == 2 && weather_hilo_str[0]) txt = weather_hilo_str;
            else if (weather_mode == 3 && weather_extra_str[0]) txt = weather_extra_str;
            if (!txt[0]) txt = "No data";
            render_text_scroll(txt, pixels, text_brightness, 10, scroll_pos);
            build_frame(pixels, frame);
            render_icon(ico, frame);
            break;
        }
        case APP_METRICS: {
            if (metric_count == 0) {
                render_text_scroll("No metrics", pixels, text_brightness, 10, scroll_pos);
                build_frame(pixels, frame);
                render_icon("chart", frame);
            } else {
                if (metric_idx >= metric_count) metric_idx = 0;
                /* Show: icon + "value trend_arrow" */
                char mb[32];
                const char *arrow = "";
                if (strncmp(metric_trends[metric_idx], "up", 2) == 0) arrow = "^";
                else if (strncmp(metric_trends[metric_idx], "down", 4) == 0) arrow = "v";
                else if (strncmp(metric_trends[metric_idx], "stable", 6) == 0) arrow = "-";
                snprintf(mb, 32, "%s%s", metric_values[metric_idx], arrow);
                render_text_scroll(mb, pixels, text_brightness, 10, scroll_pos);
                build_frame(pixels, frame);
                const char *mico = metric_icons[metric_idx][0] ? metric_icons[metric_idx] : "chart";
                render_icon(mico, frame);
            }
            break;
        }
        case APP_TIMER: {
            int tsecs = timer_get_display();
            if (timer_target_secs > 0 && tsecs <= 0 && timer_running) {
                if (file_exists("/sounds/alarm.wav")) play_wav_quiet("/sounds/alarm.wav");
                else { play_quick_tone(1000, 200); play_quick_tone(1200, 200); play_quick_tone(1000, 200); }
                timer_running = 0;
            }
            render_timer_face(pixels, tsecs, blink, text_brightness);
            build_frame(pixels, frame);
            render_icon(timer_target_secs > 0 ? "clock" : "fire", frame);
            break;
        }
        case APP_NOTIF: {
            if (notif_count == 0) {
                render_text_scroll("No notifs", pixels, text_brightness, 10, scroll_pos);
                build_frame(pixels, frame);
                render_icon("bell", frame);
            } else {
                if (notif_idx >= notif_count) notif_idx = 0;
                render_text_scroll(notif_texts[notif_idx], pixels, text_brightness, 10, scroll_pos);
                build_frame(pixels, frame);
                const char *nico = notif_icons[notif_idx][0] ? notif_icons[notif_idx] : "bell";
                render_icon(nico, frame);
            }
            break;
        }
        case APP_SOUNDS: {
            /* Show current sound name, < > to browse + play */
            render_text_scroll(sound_list[sound_idx], pixels, text_brightness, 10, scroll_pos);
            build_frame(pixels, frame);
            render_icon("music", frame);
            break;
        }
        }

        if (shared_frame) memcpy(shared_frame, frame, FRAME_SIZE);
        if (spi_child <= 0) start_spi_loop();

        /* Process serial input inline */
        char c;
        while (read(ser_fd, &c, 1) > 0) {
            if (c == '\r' || c == '\n') {
                cmd_buf[cmd_pos] = 0;
                if (cmd_pos > 0) {
                    if (carousel_process_cmd(cmd_buf) == 1) running = 0;
                }
                cmd_pos = 0;
            } else if (cmd_pos < 254) {
                cmd_buf[cmd_pos++] = c;
            }
        }

        scroll_frame++;
        scroll_pos++; /* scroll every frame (100ms per pixel) */
        usleep(100000);
    }

    fcntl(ser_fd, F_SETFL, flags);
    carousel_active = 0;
    spr("Carousel stopped\r\n> ");
}

int main(void) {
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/dev", 0755);
    mkdir("/tmp", 0755);
    mount("proc", "/proc", "proc", 0, 0);
    mount("sysfs", "/sys", "sysfs", 0, 0);
    mount("devtmpfs", "/dev", "devtmpfs", 0, 0);
    mkdir("/dev/pts", 0755);
    mount("devpts", "/dev/pts", "devpts", 0, 0);

    mount("debugfs", "/sys/kernel/debug", "debugfs", 0, 0);

    /* Create ALSA config at the path the static amixer/aplay expects */
    mkdir("/home", 0755);
    mkdir("/home/kernel", 0755);
    mkdir("/home/kernel/alsa-sysroot", 0755);
    mkdir("/home/kernel/alsa-sysroot/share", 0755);
    mkdir("/home/kernel/alsa-sysroot/share/alsa", 0755);
    {
        int afd = open("/home/kernel/alsa-sysroot/share/alsa/alsa.conf", O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (afd >= 0) {
            const char *cfg =
                "pcm.hw {\n"
                "  @args [ CARD DEV SUBDEV ]\n"
                "  @args.CARD { type string }\n"
                "  @args.DEV { type integer; default 0 }\n"
                "  @args.SUBDEV { type integer; default -1 }\n"
                "  type hw; card $CARD; device $DEV; subdevice $SUBDEV\n"
                "}\n"
                "pcm.plughw {\n"
                "  @args [ CARD DEV SUBDEV ]\n"
                "  @args.CARD { type string }\n"
                "  @args.DEV { type integer; default 0 }\n"
                "  @args.SUBDEV { type integer; default -1 }\n"
                "  type plug; slave.pcm { type hw; card $CARD; device $DEV; subdevice $SUBDEV }\n"
                "}\n"
                "pcm.boosted {\n"
                "  type softvol\n"
                "  slave.pcm \"hw:0,0\"\n"
                "  control { name \"Boost\"; card 0 }\n"
                "  max_dB 20.0\n"
                "}\n"
                "pcm.!default { type plug; slave.pcm boosted }\n"
                "ctl.hw {\n"
                "  @args [ CARD ]\n"
                "  @args.CARD { type string }\n"
                "  type hw; card $CARD\n"
                "}\n"
                "ctl.!default { type hw; card 0 }\n";
            write(afd, cfg, strlen(cfg));
            close(afd);
        }
    }

    /* Wait for ttyGS0 */
    int tries;
    for (tries = 0; tries < 30; tries++) {
        if (file_exists("/dev/ttyGS0")) break;
        sleep(1);
    }
    ser_fd = open("/dev/ttyGS0", O_RDWR | O_NOCTTY);

    /* Open I2C to STM32 (I2C2 controller = /dev/i2c-1) */
    i2c_fd = open("/dev/i2c-1", O_RDWR);
    if (i2c_fd >= 0) {
        if (ioctl(i2c_fd, I2C_SLAVE, 0x21) < 0) {
            close(i2c_fd);
            i2c_fd = -1;
        }
    }
    /* Try i2c-0 as fallback */
    if (i2c_fd < 0) {
        i2c_fd = open("/dev/i2c-0", O_RDWR);
        if (i2c_fd >= 0 && ioctl(i2c_fd, I2C_SLAVE, 0x21) < 0) {
            close(i2c_fd); i2c_fd = -1;
        }
    }

    /* Audio init — set up amp once, leave it on to avoid pops */
    /* PA is ACTIVE_HIGH so it defaults to OFF at boot (GPIO LOW).
     * First audio play will initialize via audio_ensure_init(). */

    spr("\r\n========================================\r\n");
    spr("  LaMetric Time Recovery Console\r\n");
    spr("  5.15 kernel + USB serial + I2C display\r\n");
    spr("========================================\r\n\r\n");
    cmd_info();

    /* Startup sound — short ascending chime */
    play_quick_tone(523, 100); /* C */
    play_quick_tone(659, 100); /* E */
    play_quick_tone(784, 150); /* G */
    spr("\r\nCommands:\r\n");
    spr("  carousel       Start production widget carousel\r\n");
    spr("  time HH:MM:SS  Set time (also: settime HH:MM)\r\n");
    spr("  weather I T    Set weather (icon + temp)\r\n");
    spr("  notify [I] T   Push notification\r\n");
    spr("  dismiss        Dismiss notification\r\n");
    spr("  text <msg>     Display text\r\n");
    spr("  icon <name>    Show RGBW icon\r\n");
    spr("  icontext I T   Icon + text\r\n");
    spr("  clock          Clock display\r\n");
    spr("  scroll <msg>   Scroll text\r\n");
    spr("  bright N       Brightness (0-255)\r\n");
    spr("  beep [f] [ms]  Play tone\r\n");
    spr("  demo           Button demo\r\n");
    spr("  lux / luxmon   Light sensor\r\n");
    spr("  info           System info\r\n");
    spr("  reboot         Reboot\r\n");
    spr("> ");

    char line[256]; int pos = 0;
    while (1) {
        char c; int n = read(ser_fd, &c, 1);
        if (n <= 0) { sleep(1); continue; }
        if (c == '\r' || c == '\n') {
            spr("\r\n"); line[pos] = 0;
            char *l = line; while (*l == ' ') l++;
            if (!*l) {}
            else if (strncmp(l,"info",4)==0) cmd_info();
            else if (strncmp(l,"dmesg",5)==0) cmd_dmesg();
            else if (strncmp(l,"ls",2)==0) { char *a=l+2; while(*a==' ')a++; cmd_ls(a); }
            else if (strncmp(l,"cat ",4)==0) cmd_cat(l+4);
            else if (strncmp(l,"color ",6)==0) cmd_color(l+6);
            else if (strncmp(l,"text ",5)==0) cmd_text(l+5);
            else if (strncmp(l,"white",5)==0) cmd_white();
            else if (strncmp(l,"colormap",8)==0) {
                /* Show sections in different colors/patterns */
                spi_init_display();
                if (spi_ready) {
                    unsigned char frame[FRAME_SIZE];
                    memset(frame, 0, FRAME_SIZE);
                    int i;
                    /* Section 1 (4-259): RED only — R=255, others=0 */
                    for (i = 4; i < 260; i += 4) {
                        frame[i+0] = 0;    /* B */
                        frame[i+1] = 200;  /* R */
                        frame[i+2] = 0;    /* G */
                        frame[i+3] = 0;    /* W */
                    }
                    /* Section 2 (260-363): BRIGHT white */
                    for (i = 260; i < 364; i++) frame[i] = 255;
                    /* Section 3 (364-487): DIM white (half brightness) */
                    for (i = 364; i < 488; i++) frame[i] = 80;
                    spi_load_and_refresh(frame);
                    spr("Colormap: RED=sect1, BRIGHT=sect2, DIM=sect3\r\n");
                }
            }
            else if (strncmp(l,"coltest",7)==0) {
                /* Alternating bright/dim columns in section 3 to check direction */
                spi_init_display();
                if (spi_ready) {
                    unsigned char frame[FRAME_SIZE];
                    memset(frame, 0, FRAME_SIZE);
                    int row, col;
                    /* Section 2: all bright */
                    for (row = 0; row < 8; row++)
                        for (col = 0; col < 13; col++)
                            frame[260 + row * 13 + col] = 200;
                    /* Section 3: alternating columns bright/dim */
                    for (row = 0; row < 8; row++)
                        for (col = 0; col < 16; col++)
                            frame[364 + row * 16 + col] = (col % 2 == 0) ? 255 : 40;
                    spi_load_and_refresh(frame);
                    spr("Coltest: sect3 alternating cols (bright=even byte col, dim=odd)\r\n");
                }
            }
            else if (strncmp(l,"range ",6)==0) {
                /* range START END — light up bytes START through END */
                spi_init_display();
                if (spi_ready) {
                    int start = 0, end = 0;
                    sscanf(l+6, "%d %d", &start, &end);
                    unsigned char frame[FRAME_SIZE];
                    memset(frame, 0, FRAME_SIZE);
                    int i;
                    for (i = start; i <= end && i < FRAME_SIZE; i++) frame[i] = 0xFF;
                    for (i = 0; i < 3000; i++) spi_send_frame(frame, FRAME_SIZE);
                    spr("Range "); spr_num(start); spr("-"); spr_num(end); spr("\r\n");
                }
            }
            else if (strncmp(l,"stride ",7)==0) {
                /* stride START STRIDE COUNT — light up bytes at START, START+STRIDE, ... */
                spi_init_display();
                if (spi_ready) {
                    int start = 0, stride = 0, count = 0;
                    sscanf(l+7, "%d %d %d", &start, &stride, &count);
                    unsigned char frame[FRAME_SIZE];
                    memset(frame, 0, FRAME_SIZE);
                    int i;
                    for (i = 0; i < count; i++) {
                        int idx = start + i * stride;
                        if (idx >= 0 && idx < FRAME_SIZE) frame[idx] = 0xFF;
                    }
                    for (i = 0; i < 3000; i++) spi_send_frame(frame, FRAME_SIZE);
                    spr("Stride "); spr_num(start); spr(" +"); spr_num(stride); spr(" x"); spr_num(count); spr("\r\n");
                }
            }
            else if (strncmp(l,"multi ",6)==0) {
                /* multi B1 B2 B3 ... — light up specific bytes */
                spi_init_display();
                if (spi_ready) {
                    unsigned char frame[FRAME_SIZE];
                    memset(frame, 0, FRAME_SIZE);
                    const char *p = l+6;
                    while (*p) {
                        while (*p == ' ') p++;
                        if (!*p) break;
                        int idx = atoi(p);
                        if (idx >= 0 && idx < FRAME_SIZE) frame[idx] = 0xFF;
                        while (*p && *p != ' ') p++;
                    }
                    int i;
                    for (i = 0; i < 3000; i++) spi_send_frame(frame, FRAME_SIZE);
                    spr("Multi set\r\n");
                }
            }
            else if (strncmp(l,"gradient",8)==0) cmd_gradient();
            else if (strncmp(l,"checker",7)==0) {
                spi_init_display();
                if (spi_ready) {
                    unsigned char pixels[8][37];
                    int r, c;
                    for (r = 0; r < 8; r++)
                        for (c = 0; c < 37; c++)
                            pixels[r][c] = ((r + c) % 2 == 0) ? 200 : 0;
                    unsigned char frame[FRAME_SIZE];
                    build_frame(pixels, frame);
                    /* Override S1 with colored columns for identification:
                     * Col 0=Red, 1=Green, 2=Blue, 3=White, 4=Red, 5=Green, 6=Blue, 7=White
                     * Only on checkerboard "on" pixels */
                    for (r = 0; r < 8; r++) {
                        for (c = 0; c < 8; c++) {
                            if (((r + c) % 2) != 0) continue; /* skip "off" pixels */
                            int idx = r*32 + c*4; /* S1 starts at byte 0 */
                            /* Color by column: B,G,R,W */
                            frame[idx+0] = (c%4==2) ? 200 : 0; /* Blue: cols 2,6 */
                            frame[idx+1] = (c%4==1) ? 200 : 0; /* Green: cols 1,5 */
                            frame[idx+2] = (c%4==0) ? 200 : 0; /* Red: cols 0,4 */
                            frame[idx+3] = (c%4==3) ? 200 : 0; /* White: cols 3,7 */
                        }
                    }
                    spi_load_and_refresh(frame);
                    spr("Checker: S1 cols R,G,B,W,R,G,B,W\r\n");
                }
            }
            else if (strncmp(l,"test ",5)==0) {
                /* test N — light up only byte N of the 488-byte frame */
                spi_init_display();
                if (spi_ready) {
                    int byte_n = atoi(l+5);
                    unsigned char frame[FRAME_SIZE];
                    memset(frame, 0, FRAME_SIZE);
                    if (byte_n >= 0 && byte_n < FRAME_SIZE) frame[byte_n] = 0xFF;
                    int i;
                    for (i = 0; i < 3000; i++) spi_send_frame(frame, FRAME_SIZE);
                    spr("Byte "); spr_num(byte_n); spr(" set\r\n");
                }
            }
            else if (strncmp(l,"sect ",5)==0) {
                /* sect N — light up only section N (1,2,3) */
                spi_init_display();
                if (spi_ready) {
                    int sect = atoi(l+5);
                    unsigned char frame[FRAME_SIZE];
                    memset(frame, 0, FRAME_SIZE);
                    int i;
                    if (sect == 1) for (i=4;i<260;i++) frame[i]=0xFF;
                    else if (sect == 2) for (i=260;i<364;i++) frame[i]=0xFF;
                    else if (sect == 3) for (i=364;i<488;i++) frame[i]=0xFF;
                    for (i = 0; i < 3000; i++) spi_send_frame(frame, FRAME_SIZE);
                    spr("Section "); spr_num(sect); spr(" lit\r\n");
                }
            }
            else if (strncmp(l,"audiodebug",10)==0) {
                /* Play audio via /dev/dsp AND dump registers during playback */
                spr("Setting mixer...\r\n");
                pid_t ap; int st2;
                ap=fork(); if(ap==0){execl("/amixer","amixer","cset","numid=1","63",NULL);_exit(1);}waitpid(ap,&st2,0);
                ap=fork(); if(ap==0){execl("/amixer","amixer","cset","numid=8","1",NULL);_exit(1);}waitpid(ap,&st2,0);
                ap=fork(); if(ap==0){execl("/amixer","amixer","cset","numid=13","1",NULL);_exit(1);}waitpid(ap,&st2,0);
                ap=fork(); if(ap==0){execl("/amixer","amixer","cset","numid=14","1",NULL);_exit(1);}waitpid(ap,&st2,0);
                ap=fork(); if(ap==0){execl("/amixer","amixer","cset","numid=15","1",NULL);_exit(1);}waitpid(ap,&st2,0);
                ap=fork(); if(ap==0){execl("/amixer","amixer","cset","numid=16","1",NULL);_exit(1);}waitpid(ap,&st2,0);
                ap=fork(); if(ap==0){execl("/amixer","amixer","cset","numid=17","1",NULL);_exit(1);}waitpid(ap,&st2,0);

                /* PA gain GPIOs */
                gpio_export(121); gpio_direction(121, "out"); gpio_set(121, 1);
                gpio_export(122); gpio_direction(122, "out"); gpio_set(122, 1);
                spr("Opening /dev/dsp...\r\n");
                mknod("/dev/dsp", S_IFCHR|0666, makedev(14,3));
                int dsp2 = open("/dev/dsp", O_WRONLY);
                if (dsp2 < 0) { spr("Cannot open /dev/dsp\r\n"); }
                else {
                    int v=0x10; ioctl(dsp2,0xC0045005,&v);
                    v=2; ioctl(dsp2,0xC0045006,&v);
                    v=48000; ioctl(dsp2,0xC0045002,&v);
                    spr("Configured. Writing data...\r\n");

                    /* Write audio in small chunks to prevent DMA underrun.
                     * 4800 frames = 100ms per chunk, 50 chunks = 5 seconds. */
                    short *buf2 = malloc(4800*4);
                    if (buf2) {
                        int k, chunk;
                        /* Generate 800Hz square wave */
                        for(k=0;k<4800*2;k++) {
                            short s = ((k/2/30)&1) ? 32000 : -32000;
                            buf2[k] = s;
                        }

                        /* Write first chunk then force bare-metal ACTL */
                        write(dsp2, buf2, 4800*4);
                        int mfd2 = open("/dev/mem", O_RDWR|O_SYNC);
                        if (mfd2 >= 0) {
                            volatile unsigned int *cp2 = mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,mfd2,0x01C22000);
                            if (cp2 != MAP_FAILED) {
                                volatile unsigned int *c2 = cp2+(0xC00/4);
                                /* Force EXACT bare-metal ACTL: DACAENR+DACAENL+MIXEN+PA_MUTE+PA_VOL_MAX */
                                c2[0x10/4] = 0xE000007F;
                                c2[0x28/4] |= (1<<4); /* PA_EN */
                                char h2[16];
                                spr("ACTL forced to 0xE000007F\r\n");
                                spr("FIFOC="); snprintf(h2,16,"0x%08X",c2[0x04/4]); spr(h2);
                                spr(" FIFOS="); snprintf(h2,16,"0x%08X",c2[0x08/4]); spr(h2);
                                spr("\r\n");
                                munmap((void*)cp2,0x1000);
                            }
                            close(mfd2);
                        }

                        /* HYBRID: Use ALSA for clocks, then PIO for data.
                         * 1. Write minimal data via /dev/dsp (triggers DMA + correct PLL2)
                         * 2. Stop DMA by clearing FIFOC DRQ_EN
                         * 3. Write PIO data directly to DAC_TXDATA
                         * This matches the bare-metal approach that produced LOUD sound. */
                        write(dsp2, buf2, 4800*4); /* Trigger DMA */
                        usleep(50000); /* Let DMA start */

                        int mfd3 = open("/dev/mem", O_RDWR|O_SYNC);
                        if (mfd3 >= 0) {
                            volatile unsigned int *cp3 = mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,mfd3,0x01C22000);
                            if (cp3 != MAP_FAILED) {
                                volatile unsigned int *c3 = cp3+(0xC00/4);
                                char h3[16];
                                spr("PLL2 from ALSA active\r\n");

                                /* Disable DMA, enable PIO */
                                unsigned int fifoc = c3[0x04/4];
                                fifoc &= ~(1<<4); /* Clear DRQ_EN */
                                c3[0x04/4] = fifoc;

                                /* Flush FIFO */
                                c3[0x04/4] |= (1<<0);
                                usleep(10000);
                                /* Reconfigure FIFO for PIO: bare-metal settings */
                                c3[0x04/4] = (0<<29)|(1<<26)|(1<<24)|(0xF<<8);

                                /* Force CONFIRMED WORKING ACTL from RECOVERY_NOTES
                                 * 0xE000C17F = DACAENR+DACAENL+MIXEN+LDACLMIXS+RDACLMIXS+DACPAS+PA_MUTE+PA_VOL_MAX */
                                c3[0x10/4] = (1U<<31)|(1U<<30)|(1U<<29)|(1<<15)|(1<<14)|(1<<8)|(1<<6)|0x3F;
                                c3[0x28/4] |= (1<<4);

                                spr("FIFOC="); snprintf(h3,16,"0x%08X",c3[0x04/4]); spr(h3);
                                spr(" ACTL="); snprintf(h3,16,"0x%08X",c3[0x10/4]); spr(h3);
                                spr("\r\n");

                                /* PIO write: 5 seconds of 800Hz square wave */
                                spr("PIO playing 5s...\r\n");
                                int total = 48000 * 5;
                                int toggle = 0, cnt = 0, j;
                                for (j = 0; j < total; j++) {
                                    while (((c3[0x08/4]>>8)&0x7FFF) == 0);
                                    cnt++;
                                    if (cnt >= 30) { cnt = 0; toggle = !toggle; }
                                    c3[0x0C/4] = toggle ? 0x7FFF7FFF : 0x80008000;
                                }
                                spr("PIO done\r\n");
                                munmap((void*)cp3, 0x1000);
                            }
                            close(mfd3);
                        }
                        spr("Done\r\n");
                        free(buf2);
                    }
                    close(dsp2);
                    spr("Done\r\n");
                }
            }
            else if (strncmp(l,"icon ",5)==0) {
                /* icon NAME — show an 8x8 RGBW icon on S1 */
                spi_init_display();
                if (spi_ready) {
                    unsigned char frame3[FRAME_SIZE];
                    memset(frame3, 0, FRAME_SIZE);
                    if (render_icon(l+5, frame3) == 0) {
                        spi_load_and_refresh(frame3);
                        spr("Icon: "); spr(l+5); spr("\r\n");
                    } else {
                        spr("Unknown icon. Available: ");
                        const char **n = icon_names;
                        while (*n) { spr(*n); spr(" "); n++; }
                        spr("\r\n");
                    }
                }
            }
            else if (strncmp(l,"icontext ",9)==0) {
                /* icontext ICON TEXT — show icon on S1 + text on white sections */
                spi_init_display();
                if (spi_ready) {
                    /* Parse: first word = icon name, rest = text */
                    char *iname = l+9;
                    char *txt = iname;
                    while (*txt && *txt != ' ') txt++;
                    if (*txt) *txt++ = 0;
                    unsigned char pixels3[8][37];
                    render_text_at(txt, pixels3, text_brightness, 12);
                    unsigned char frame3[FRAME_SIZE];
                    build_frame(pixels3, frame3);
                    render_icon(iname, frame3); /* overlay icon on S1 */
                    spi_load_and_refresh(frame3);
                    spr("Icon+text: "); spr(iname); spr(" "); spr(txt); spr("\r\n");
                }
            }
            else if (strncmp(l,"lux",3)==0 && strncmp(l,"luxmon",6)!=0) {
                /* Read JSA1127 light sensor at I2C addr 0x29 */
                if (i2c_fd < 0) { spr("No I2C\r\n"); }
                else {
                    unsigned char cmd, buf[2];
                    /* Switch to JSA1127 */
                    ioctl(i2c_fd, I2C_SLAVE, 0x29);
                    cmd = 0x04; write(i2c_fd, &cmd, 1); /* one-time mode */
                    usleep(10000);
                    cmd = 0x08; write(i2c_fd, &cmd, 1); /* start integration */
                    usleep(200000); /* 200ms integration */
                    cmd = 0x30; write(i2c_fd, &cmd, 1); /* stop */
                    usleep(10000);
                    int n = read(i2c_fd, buf, 2);
                    cmd = 0x8C; write(i2c_fd, &cmd, 1); /* standby */
                    /* Restore STM32 address */
                    ioctl(i2c_fd, I2C_SLAVE, 0x21);
                    if (n == 2) {
                        int raw = buf[0] | (buf[1] << 8);
                        int valid = (raw >> 15) & 1;
                        raw &= 0x7FFF;
                        int lux = raw / 6; /* approximate */
                        spr("Raw="); spr_num(raw);
                        spr(" Valid="); spr_num(valid);
                        spr(" Lux~"); spr_num(lux); spr("\r\n");
                    } else {
                        spr("Read failed ("); spr_num(n); spr(")\r\n");
                    }
                }
            }
            else if (strncmp(l,"luxmon",6)==0) {
                /* Live lux monitor: shows value on display, auto-adjusts brightness */
                spi_init_display();
                if (!spi_ready || i2c_fd < 0) { spr("Need SPI+I2C\r\n"); }
                else {
                    spr("Lux monitor (any key to stop)\r\n");
                    int flags2 = fcntl(ser_fd, F_GETFL, 0);
                    fcntl(ser_fd, F_SETFL, flags2 | O_NONBLOCK);
                    int last_bright = -1;
                    while (1) {
                        /* Read JSA1127 */
                        unsigned char cmd2, rb[2];
                        ioctl(i2c_fd, I2C_SLAVE, 0x29);
                        cmd2 = 0x04; write(i2c_fd, &cmd2, 1);
                        usleep(10000);
                        cmd2 = 0x08; write(i2c_fd, &cmd2, 1);
                        usleep(200000);
                        cmd2 = 0x30; write(i2c_fd, &cmd2, 1);
                        usleep(10000);
                        int nr = read(i2c_fd, rb, 2);
                        cmd2 = 0x8C; write(i2c_fd, &cmd2, 1);
                        ioctl(i2c_fd, I2C_SLAVE, 0x21);

                        int lux_val = 0;
                        if (nr == 2) {
                            int raw2 = (rb[0] | (rb[1] << 8)) & 0x7FFF;
                            lux_val = raw2 / 6;
                        }

                        /* Map lux to brightness using configurable range */
                        int bright2;
                        if (lux_val < 5) bright2 = lux_bright_min;
                        else if (lux_val < 300) bright2 = lux_bright_min + (lux_val * (lux_bright_max - lux_bright_min) / 300);
                        else bright2 = lux_bright_max;

                        /* Update display brightness if changed */
                        if (bright2 != last_bright) {
                            i2c_cmd(0x20, bright2);
                            last_bright = bright2;
                        }

                        /* Show lux value on display */
                        char lbuf[16];
                        snprintf(lbuf, sizeof(lbuf), "L:%d", lux_val);
                        unsigned char pixels2[8][37];
                        render_text_at(lbuf, pixels2, text_brightness, 12);
                        unsigned char frame2[FRAME_SIZE];
                        build_frame(pixels2, frame2);
                        if (shared_frame) memcpy(shared_frame, frame2, FRAME_SIZE);
                        if (spi_child <= 0) start_spi_loop();

                        /* Check for keypress */
                        char ck;
                        if (read(ser_fd, &ck, 1) > 0) break;
                        usleep(300000); /* Update ~3x per second */
                    }
                    fcntl(ser_fd, F_SETFL, flags2);
                    spr("Lux monitor stopped\r\n");
                }
            }
            else if (strncmp(l,"regdump",7)==0) {
                int mfd = open("/dev/mem", O_RDWR | O_SYNC);
                if (mfd >= 0) {
                    volatile unsigned int *cp = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
                                                      MAP_SHARED, mfd, 0x01C22000);
                    if (cp != MAP_FAILED) {
                        volatile unsigned int *c = cp + (0xC00/4);
                        char h[16];
                        spr("DAC_DPC="); snprintf(h,16,"0x%08X",c[0x00/4]); spr(h);
                        spr(" FIFOC="); snprintf(h,16,"0x%08X",c[0x04/4]); spr(h);
                        spr(" FIFOS="); snprintf(h,16,"0x%08X",c[0x08/4]); spr(h);
                        spr("\r\nACTL="); snprintf(h,16,"0x%08X",c[0x10/4]); spr(h);
                        spr(" ADC_ACTL="); snprintf(h,16,"0x%08X",c[0x28/4]); spr(h);
                        spr("\r\n");
                        munmap((void*)cp, 0x1000);
                    } else spr("mmap fail\r\n");
                    close(mfd);
                }
            }
            else if (strncmp(l,"wifi ",5)==0) {
                /* wifi on/off */
                if (strncmp(l+5,"off",3)==0) {
                    int fd = open("/sys/class/net/wlan0/flags", O_WRONLY);
                    if (fd >= 0) { write(fd, "0x0", 3); close(fd); spr("WiFi off\r\n"); }
                    else spr("No wlan0\r\n");
                } else {
                    int fd = open("/sys/class/net/wlan0/flags", O_WRONLY);
                    if (fd >= 0) { write(fd, "0x1003", 6); close(fd); spr("WiFi on\r\n"); }
                    else spr("No wlan0\r\n");
                }
            }
            else if (strncmp(l,"carousel",8)==0) cmd_carousel();
            else if (strncmp(l,"timer",5)==0) { char *a=l+5; while(*a==' ')a++; cmd_timer(a); }
            else if (strncmp(l,"weather ",8)==0) cmd_weather_set(l+8);
            else if (strncmp(l,"metric ",7)==0) cmd_metric(l+7);
            else if (strncmp(l,"notify ",7)==0) cmd_notify_push(l+7);
            else if (strncmp(l,"dismiss",7)==0) cmd_dismiss();
            else if (strncmp(l,"time ",5)==0) cmd_time(l+5);
            else if (strncmp(l,"demo",4)==0) cmd_demo();
            else if (strncmp(l,"scroll ",7)==0) cmd_scroll(l+7);
            else if (strncmp(l,"buttons",7)==0) cmd_buttons();
            else if (strncmp(l,"fill ",5)==0) {
                /* fill B G R W — fill S1 with specific RGBW color (0-255 each) */
                spi_init_display();
                if (spi_ready) {
                    int b=0, g=0, r=0, w=0;
                    sscanf(l+5, "%d %d %d %d", &b, &g, &r, &w);
                    unsigned char frame[FRAME_SIZE];
                    memset(frame, 0, FRAME_SIZE);
                    /* Fill S1 with RGBW color using interleaved row mapping */
                    int row, col;
                    for (row = 0; row < 8; row++) {
                        int row_offset;
                        if (row % 2 == 0)
                            row_offset = (row / 2) * 32;
                        else
                            row_offset = (row / 2) * 32 + 128;
                        for (col = 0; col < 8; col++) {
                            int idx = 4 + row_offset + col * 4;
                            frame[idx+0] = b; frame[idx+1] = g;
                            frame[idx+2] = r; frame[idx+3] = w;
                        }
                    }
                    /* Also fill bytes 0-3 for col 0 row 0 */
                    frame[0] = b; frame[1] = g; frame[2] = r; frame[3] = w;
                    /* Fill white sections too */
                    int i;
                    for (i = 260; i < FRAME_SIZE; i++) frame[i] = w;
                    spi_load_and_refresh(frame);
                    spr("Fill: B="); spr_num(b); spr(" G="); spr_num(g);
                    spr(" R="); spr_num(r); spr(" W="); spr_num(w); spr("\r\n");
                }
            }
            else if (strncmp(l,"rawbeep",7)==0) { char *a=l+7; while(*a==' ')a++; cmd_rawbeep(a); }
            else if (strncmp(l,"beep",4)==0) { char *a=l+4; while(*a==' ')a++; cmd_beep(a); }
            else if (strncmp(l,"sounds",6)==0) {
                /* List and optionally play sounds */
                char *a = l+6; while (*a == ' ') a++;
                if (*a) {
                    /* Play a specific sound: "sounds alarm" → /sounds/alarm.wav */
                    char path[64];
                    snprintf(path, sizeof(path), "/sounds/%s.wav", a);
                    if (file_exists(path)) { cmd_play(path); }
                    else { spr("Not found: "); spr(path); spr("\r\n"); }
                } else {
                    spr("Available sounds:\r\n");
                    cmd_ls("/sounds");
                }
            }
            else if (strncmp(l,"gptone",6)==0) { char *a=l+6; while(*a==' ')a++; cmd_gptone(a); }
            else if (strncmp(l,"atest ",6)==0) { cmd_atest(l+6); }
            else if (strncmp(l,"testtone",8)==0) { char *a=l+8; while(*a==' ')a++; cmd_testtone(a); }
            else if (strncmp(l,"play ",5)==0) cmd_play(l+5);
            else if (strncmp(l,"volume",6)==0) cmd_volume_max();
            else if (strncmp(l,"codec",5)==0) cmd_codec_dump();
            else if (strncmp(l,"bright ",7)==0) cmd_bright(l+7);
            else if (strncmp(l,"scrollmode ",11)==0) {
                int m = atoi(l+11);
                if (m >= 0 && m <= 4) {
                    scroll_mode = m;
                    char buf[64];
                    const char *names[] = {"auto","left-to-right","right-to-left","bounce","no-scroll"};
                    snprintf(buf, sizeof(buf), "Scroll mode: %s (%d)\r\n", names[m], m);
                    spr(buf);
                } else spr("scrollmode 0-4: 0=auto 1=ltr 2=rtl 3=bounce 4=noscroll\r\n");
            }
            else if (strncmp(l,"luxrange ",9)==0) {
                int mn = 0, mx = 0;
                sscanf(l+9, "%d %d", &mn, &mx);
                if (mn >= 0 && mn <= 255 && mx >= mn && mx <= 255) {
                    lux_bright_min = mn;
                    lux_bright_max = mx;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Lux range: %d-%d\r\n", mn, mx);
                    spr(buf);
                } else spr("luxrange <min> <max> (0-255)\r\n");
            }
            else if (strncmp(l,"settime ",8)==0) cmd_settime(l+8);
            else if (strncmp(l,"clock",5)==0) cmd_clock();
            else if (strncmp(l,"anim",4)==0) cmd_anim();
            else if (strncmp(l,"i2c ",4)==0) cmd_i2c_write(l+4);
            else if (strncmp(l,"stop",4)==0) {
                /* Stop persistent display loop */
                if (spi_child > 0) {
                    kill(spi_child, 9);
                    spi_child = 0;
                    spr("Display loop stopped\r\n");
                } else spr("No loop running\r\n");
            }
            else if (strncmp(l,"run ",4)==0) {
                /* run <command> — execute a binary and show output */
                spr("Running: "); spr(l+4); spr("\r\n");
                int pfd[2]; pipe(pfd);
                pid_t pid = fork();
                if (pid == 0) {
                    close(pfd[0]);
                    dup2(pfd[1], 1); dup2(pfd[1], 2);
                    close(pfd[1]);
                    execl("/bin/sh", "sh", "-c", l+4, NULL);
                    /* No /bin/sh — try direct exec */
                    char *argv[16]; int ac = 0;
                    char *cmd = strdup(l+4);
                    char *tok = cmd;
                    while (*tok && ac < 15) {
                        while (*tok == ' ') tok++;
                        if (!*tok) break;
                        argv[ac++] = tok;
                        while (*tok && *tok != ' ') tok++;
                        if (*tok) *tok++ = 0;
                    }
                    argv[ac] = NULL;
                    if (ac > 0) execv(argv[0], argv);
                    _exit(127);
                }
                close(pfd[1]);
                char rb[256]; int rn;
                while ((rn = read(pfd[0], rb, sizeof(rb)-1)) > 0) {
                    rb[rn] = 0; spr(rb);
                }
                close(pfd[0]);
                waitpid(pid, NULL, 0);
                spr("\r\n> ");
            }
            else if (strncmp(l,"reboot",6)==0) { spr("Rebooting...\r\n"); sync(); syscall(__NR_reboot,0xfee1dead,0x28121969,0x01234567,0); }
            else spr("Unknown command\r\n");
            pos = 0; spr("> ");
        } else if (c==127||c==8) { if(pos>0){pos--;spr("\b \b");} }
        else if (pos<254) { line[pos++]=c; write(ser_fd,&c,1); }
    }
}
