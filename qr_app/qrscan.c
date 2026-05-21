#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <zbar.h>

/* GPIO numbers (GPIOA base=0) */
#define GREEN_LED_PIN   13   /* PA13 */
#define RELAY_PIN       14   /* PA14 */
#define RED_LED_PIN     15   /* PA15 */

/* Heartbeat LED (onboard) */
#define HEARTBEAT_LED   "/sys/class/leds/heartbeat"

/* Config */
#define DEVICE          "/dev/video0"
#define WIDTH           640
#define HEIGHT          480
#define WHITELIST_FILE  "/etc/qr_whitelist.txt"
#define AUDIT_LOG       "/var/log/access.log"
#define COOLDOWN_SECS   5

/* ── Heartbeat LED helpers ── */
void led_set_trigger(const char *trigger) {
    char path[64];
    snprintf(path, sizeof(path), "%s/trigger", HEARTBEAT_LED);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, trigger, strlen(trigger)); close(fd); }
}

void led_brightness(int val) {
    char path[64];
    snprintf(path, sizeof(path), "%s/brightness", HEARTBEAT_LED);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, val ? "1" : "0", 1); close(fd); }
}

/* Blink LED n times with given on/off ms */
void led_blink(int times, int on_ms, int off_ms) {
    for (int i = 0; i < times; i++) {
        led_brightness(1);
        usleep(on_ms * 1000);
        led_brightness(0);
        usleep(off_ms * 1000);
    }
}

/* ── GPIO helpers (numeric, with export) ── */
void gpio_export(int pin) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", pin);
    if (access(path, F_OK) == 0) return;   /* already exported */
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) {
        char buf[8];
        int len = snprintf(buf, sizeof(buf), "%d", pin);
        write(fd, buf, len);
        close(fd);
        usleep(100000);   /* wait for sysfs entry */
    }
}

void gpio_direction(int pin, const char *dir) {
    gpio_export(pin);
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, dir, strlen(dir)); close(fd); }
}

void gpio_write(int pin, int val) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, val ? "1" : "0", 1); close(fd); }
}

/* ── Whitelist check ── */
int check_whitelist(const char *qr_data) {
    FILE *f = fopen(WHITELIST_FILE, "r");
    if (!f) {
        printf("Whitelist not found — allowing all\n");
        return 1;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, qr_data) == 0) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

/* ── Audit log ── */
void audit_log(const char *qr_data, int granted) {
    /* Write to file */
    FILE *f = fopen(AUDIT_LOG, "a");
    if (f) {
        time_t now = time(NULL);
        char *ts = ctime(&now);
        ts[strcspn(ts, "\n")] = 0;
        fprintf(f, "[%s] %s: %s\n", ts,
                granted ? "ACCESS GRANTED" : "ACCESS DENIED",
                qr_data);
        fclose(f);
    }
    /* Always print to terminal */
    time_t now = time(NULL);
    char *ts = ctime(&now);
    ts[strcspn(ts, "\n")] = 0;
    printf("[%s] %s: %s\n", ts,
           granted ? "ACCESS GRANTED" : "ACCESS DENIED",
           qr_data);
}

/* ── Access control action ── */
void handle_access(const char *qr_data) {
    int granted = check_whitelist(qr_data);
    audit_log(qr_data, granted);

    if (granted) {
        printf("✓ Access GRANTED — door open 3s\n");

        /* Heartbeat LED: 3 slow blinks = VALID */
        led_set_trigger("none");
        led_blink(3, 500, 200);   /* 3 × (500ms ON, 200ms OFF) */

        /* GPIO relay + green LED (if connected) */
        gpio_write(RELAY_PIN,     1);
        gpio_write(GREEN_LED_PIN, 1);
        sleep(3);
        gpio_write(RELAY_PIN,     0);
        gpio_write(GREEN_LED_PIN, 0);

        /* Restore heartbeat trigger */
        led_set_trigger("heartbeat");

    } else {
        printf("✗ Access DENIED\n");

        /* Heartbeat LED: 5 fast blinks = INVALID */
        led_set_trigger("none");
        led_blink(5, 100, 100);   /* 5 × (100ms ON, 100ms OFF) */

        /* GPIO red LED (if connected) */
        gpio_write(RED_LED_PIN, 1);
        sleep(2);
        gpio_write(RED_LED_PIN, 0);

        /* Restore heartbeat trigger */
        led_set_trigger("heartbeat");
    }
}

/* ── V4L2 buffer struct ── */
struct buffer { void *start; size_t length; };

int main() {
    printf("=== STM32MP157 QR Access Control ===\n");
    printf("Cooldown : %d seconds\n", COOLDOWN_SECS);
    printf("Heartbeat: 3 slow blinks = VALID | 5 fast blinks = INVALID\n\n");

    /* Cooldown tracking */
    char last_qr[256] = "";
    time_t last_scan_time = 0;

    /* Take over heartbeat LED */
    led_set_trigger("none");
    led_brightness(0);
    printf("Heartbeat LED ready\n");

    /* Setup GPIO (safe even if nothing connected) */
    gpio_direction(GREEN_LED_PIN, "out");
    gpio_direction(RELAY_PIN,     "out");
    gpio_direction(RED_LED_PIN,   "out");
    gpio_write(GREEN_LED_PIN, 0);
    gpio_write(RELAY_PIN,     0);
    gpio_write(RED_LED_PIN,   0);
    printf("GPIO initialized (PA13=green, PA14=relay, PA15=red)\n");

    /* Open camera */
    int fd = open(DEVICE, O_RDWR);
    if (fd < 0) { perror("open camera"); return -1; }

    /* Set format YUYV 640x480 */
    struct v4l2_format fmt = {0};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = WIDTH;
    fmt.fmt.pix.height      = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    ioctl(fd, VIDIOC_S_FMT, &fmt);

    /* Request + mmap buffers */
    struct v4l2_requestbuffers req = {0};
    req.count  = 2;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(fd, VIDIOC_REQBUFS, &req);

    struct buffer buffers[2];
    for (int i = 0; i < 2; i++) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        ioctl(fd, VIDIOC_QUERYBUF, &buf);
        buffers[i].length = buf.length;
        buffers[i].start  = mmap(NULL, buf.length,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, buf.m.offset);
        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    /* Start streaming */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMON, &type);
    printf("Camera streaming started on %s\n", DEVICE);
    printf("Waiting for QR codes...\n\n");

    /* ZBar scanner */
    zbar_image_scanner_t *scanner = zbar_image_scanner_create();
    zbar_image_scanner_set_config(scanner, 0, ZBAR_CFG_ENABLE, 1);

    /* ── Main scan loop ── */
    while (1) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
            break;
        }

        /* Extract Y (luma) channel from YUYV for ZBar */
        zbar_image_t *image = zbar_image_create();
        zbar_image_set_format(image, zbar_fourcc('Y','8','0','0'));
        zbar_image_set_size(image, WIDTH, HEIGHT);

        unsigned char *gray = malloc(WIDTH * HEIGHT);
        unsigned char *yuyv = buffers[buf.index].start;
        for (int i = 0; i < WIDTH * HEIGHT; i++)
            gray[i] = yuyv[i * 2];   /* Y byte at even offsets */
        zbar_image_set_data(image, gray, WIDTH * HEIGHT, zbar_image_free_data);

        /* Scan */
        int n = zbar_scan_image(scanner, image);
        if (n > 0) {
            const zbar_symbol_t *sym = zbar_image_first_symbol(image);
            while (sym) {
                const char *data = zbar_symbol_get_data(sym);
                time_t now = time(NULL);

                if (strcmp(data, last_qr) != 0 ||
                    difftime(now, last_scan_time) > COOLDOWN_SECS) {

                    printf("QR detected: %s\n", data);
                    handle_access(data);

                    strncpy(last_qr, data, sizeof(last_qr) - 1);
                    last_qr[sizeof(last_qr) - 1] = '\0';
                    last_scan_time = now;

                } else {
                    int rem = COOLDOWN_SECS - (int)difftime(now, last_scan_time);
                    printf("Cooldown: %d s remaining\n", rem);
                }

                sym = zbar_symbol_next(sym);
            }
        }

        zbar_image_destroy(image);
        ioctl(fd, VIDIOC_QBUF, &buf);
        usleep(100000);   /* 100 ms between frames */
    }

    /* Cleanup */
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    zbar_image_scanner_destroy(scanner);
    close(fd);

    gpio_write(GREEN_LED_PIN, 0);
    gpio_write(RELAY_PIN,     0);
    gpio_write(RED_LED_PIN,   0);
    led_set_trigger("heartbeat");   /* restore normal heartbeat */

    return 0;
}
