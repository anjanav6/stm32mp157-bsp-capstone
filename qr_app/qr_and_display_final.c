/*
 * qr_display.c  —  STM32MP157 QR Access Control with Live LCD Preview
 *
 * Combines:
 *   testcam3.c  → live YUYV→RGB565 display on /dev/fb0
 *   qrscan.c    → ZBar QR decode + whitelist + LED/GPIO feedback
 *
 * Extra: simple 8×8 bitmap OSD so scan results appear on the LCD.
 *
 * Build (on host, arm cross-compiler):
 *   arm-linux-gnueabihf-gcc -O2 -o qr_display qr_display.c \
 *       -lzbar -lm
 *
 * Build (on target):
 *   gcc -O2 -o qr_display qr_display.c -lzbar -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <zbar.h>

/* ─── Hardware / layout ──────────────────────────────────────────────── */
#define VIDEO_DEV       "/dev/video0"
#define FB_DEV          "/dev/fb0"

#define CAM_W           640
#define CAM_H           480

#define LCD_W           480
#define LCD_H           800

/* GPIO numeric pins (GPIOA base = 0) */
#define GREEN_LED_PIN   13      /* PA13 */
#define RELAY_PIN       14      /* PA14 */
#define RED_LED_PIN     15      /* PA15 */

#define HEARTBEAT_LED   "/sys/class/leds/heartbeat"

/* ─── App config ─────────────────────────────────────────────────────── */
#define WHITELIST_FILE  "/etc/qr_whitelist.txt"
#define AUDIT_LOG       "/var/log/access.log"
#define COOLDOWN_SECS   5

/* How many frames to show the OSD result banner */
#define OSD_FRAMES      50      /* ~5 s at 100 ms/frame */

/* ─── Colour palette (RGB565) ────────────────────────────────────────── */
#define COL_GREEN   0x07E0u
#define COL_RED     0xF800u
#define COL_YELLOW  0xFFE0u
#define COL_BLACK   0x0000u
#define COL_WHITE   0xFFFFu

/* ─────────────────────────────────────────────────────────────────────── */
/*  Tiny 8×8 bitmap font (ASCII 32-127)                                   */
/*  Each character is 8 bytes; bit 7 of each byte = leftmost pixel.       */
/* ─────────────────────────────────────────────────────────────────────── */
static const uint8_t font8x8[96][8] = {
    /* 0x20 space */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x21 !     */ {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    /* 0x22 "     */ {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x23 #     */ {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    /* 0x24 $     */ {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
    /* 0x25 %     */ {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    /* 0x26 &     */ {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
    /* 0x27 '     */ {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    /* 0x28 (     */ {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
    /* 0x29 )     */ {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    /* 0x2A *     */ {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    /* 0x2B +     */ {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    /* 0x2C ,     */ {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},
    /* 0x2D -     */ {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    /* 0x2E .     */ {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
    /* 0x2F /     */ {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    /* 0x30 0     */ {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    /* 0x31 1     */ {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    /* 0x32 2     */ {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
    /* 0x33 3     */ {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    /* 0x34 4     */ {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    /* 0x35 5     */ {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    /* 0x36 6     */ {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
    /* 0x37 7     */ {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    /* 0x38 8     */ {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
    /* 0x39 9     */ {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    /* 0x3A :     */ {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},
    /* 0x3B ;     */ {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    /* 0x3C <     */ {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},
    /* 0x3D =     */ {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    /* 0x3E >     */ {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    /* 0x3F ?     */ {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    /* 0x40 @     */ {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},
    /* 0x41 A     */ {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    /* 0x42 B     */ {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    /* 0x43 C     */ {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    /* 0x44 D     */ {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    /* 0x45 E     */ {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    /* 0x46 F     */ {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    /* 0x47 G     */ {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    /* 0x48 H     */ {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
    /* 0x49 I     */ {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    /* 0x4A J     */ {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    /* 0x4B K     */ {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    /* 0x4C L     */ {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    /* 0x4D M     */ {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    /* 0x4E N     */ {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    /* 0x4F O     */ {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    /* 0x50 P     */ {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    /* 0x51 Q     */ {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    /* 0x52 R     */ {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    /* 0x53 S     */ {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    /* 0x54 T     */ {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    /* 0x55 U     */ {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    /* 0x56 V     */ {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
    /* 0x57 W     */ {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    /* 0x58 X     */ {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    /* 0x59 Y     */ {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    /* 0x5A Z     */ {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    /* 0x5B [     */ {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    /* 0x5C \     */ {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    /* 0x5D ]     */ {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    /* 0x5E ^     */ {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},
    /* 0x5F _     */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    /* 0x60 `     */ {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 0x61 a     */ {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    /* 0x62 b     */ {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    /* 0x63 c     */ {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    /* 0x64 d     */ {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00},
    /* 0x65 e     */ {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00},
    /* 0x66 f     */ {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00},
    /* 0x67 g     */ {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    /* 0x68 h     */ {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    /* 0x69 i     */ {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    /* 0x6A j     */ {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E},
    /* 0x6B k     */ {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    /* 0x6C l     */ {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    /* 0x6D m     */ {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    /* 0x6E n     */ {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},
    /* 0x6F o     */ {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    /* 0x70 p     */ {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    /* 0x71 q     */ {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    /* 0x72 r     */ {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    /* 0x73 s     */ {0x00,0x00,0x1E,0x03,0x1E,0x30,0x1F,0x00},
    /* 0x74 t     */ {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    /* 0x75 u     */ {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    /* 0x76 v     */ {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    /* 0x77 w     */ {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    /* 0x78 x     */ {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    /* 0x79 y     */ {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    /* 0x7A z     */ {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
    /* 0x7B {     */ {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    /* 0x7C |     */ {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    /* 0x7D }     */ {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    /* 0x7E ~     */ {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x7F DEL   */ {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
};

/* ─── Framebuffer globals ────────────────────────────────────────────── */
static uint16_t *g_fb      = NULL;
static int       g_fb_pitch = 0;   /* pixels per row */

/* ─── Colour helpers ─────────────────────────────────────────────────── */
static inline uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) |
                      ((g & 0xFC) << 3) |
                      (b >> 3));
}

static inline void yuv_to_rgb(int y, int u, int v,
                               int *r, int *g, int *b)
{
    int c = y - 16, d = u - 128, e = v - 128;
    int rt = (298*c + 409*e + 128) >> 8;
    int gt = (298*c - 100*d - 208*e + 128) >> 8;
    int bt = (298*c + 516*d + 128) >> 8;
    *r = rt < 0 ? 0 : rt > 255 ? 255 : rt;
    *g = gt < 0 ? 0 : gt > 255 ? 255 : gt;
    *b = bt < 0 ? 0 : bt > 255 ? 255 : bt;
}

/* ─── OSD text renderer ──────────────────────────────────────────────── */
/*
 * Draw a scaled character (scale=2 → 16×16 px) at (px,py).
 * Each source bit expands to scale×scale destination pixels.
 */
static void osd_draw_char(int px, int py, char ch,
                           uint16_t fg, uint16_t bg, int scale)
{
    if (ch < 0x20 || ch > 0x7F) ch = '?';
    const uint8_t *bitmap = font8x8[ch - 0x20];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            uint16_t colour = (bitmap[row] & (0x80 >> col)) ? fg : bg;
            for (int sy = 0; sy < scale; sy++) {
                int fy = py + row * scale + sy;
                if (fy < 0 || fy >= LCD_H) continue;
                for (int sx = 0; sx < scale; sx++) {
                    int fx = px + col * scale + sx;
                    if (fx < 0 || fx >= LCD_W) continue;
                    g_fb[fy * g_fb_pitch + fx] = colour;
                }
            }
        }
    }
}

/*
 * Draw a string at (px,py).  scale=2 → 16px tall glyphs.
 * Returns pixel width of the rendered string.
 */
static int osd_draw_str(int px, int py, const char *str,
                         uint16_t fg, uint16_t bg, int scale)
{
    int x = px;
    while (*str) {
        osd_draw_char(x, py, *str++, fg, bg, scale);
        x += 8 * scale;
    }
    return x - px;
}

/*
 * Draw a filled rectangle (for banner background).
 */
static void osd_fill_rect(int x, int y, int w, int h, uint16_t colour)
{
    for (int ry = y; ry < y + h && ry < LCD_H; ry++)
        for (int rx = x; rx < x + w && rx < LCD_W; rx++)
            g_fb[ry * g_fb_pitch + rx] = colour;
}

/* ─── Heartbeat LED helpers ──────────────────────────────────────────── */
static void led_set_trigger(const char *trigger)
{
    char path[80];
    snprintf(path, sizeof(path), "%s/trigger", HEARTBEAT_LED);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, trigger, strlen(trigger)); close(fd); }
}

static void led_brightness(int val)
{
    char path[80];
    snprintf(path, sizeof(path), "%s/brightness", HEARTBEAT_LED);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, val ? "1" : "0", 1); close(fd); }
}

static void led_blink(int times, int on_ms, int off_ms)
{
    for (int i = 0; i < times; i++) {
        led_brightness(1);
        usleep(on_ms  * 1000);
        led_brightness(0);
        usleep(off_ms * 1000);
    }
}

/* ─── GPIO helpers ───────────────────────────────────────────────────── */
static void gpio_export(int pin)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", pin);
    if (access(path, F_OK) == 0) return;
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) {
        char buf[8];
        int  len = snprintf(buf, sizeof(buf), "%d", pin);
        write(fd, buf, len);
        close(fd);
        usleep(100000);
    }
}

static void gpio_direction(int pin, const char *dir)
{
    gpio_export(pin);
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, dir, strlen(dir)); close(fd); }
}

static void gpio_write(int pin, int val)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, val ? "1" : "0", 1); close(fd); }
}

/* ─── Whitelist check ────────────────────────────────────────────────── */
static int check_whitelist(const char *qr_data)
{
    FILE *f = fopen(WHITELIST_FILE, "r");
    if (!f) { printf("Whitelist not found — allowing all\n"); return 1; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, qr_data) == 0) { fclose(f); return 1; }
    }
    fclose(f);
    return 0;
}

/* ─── Audit log ──────────────────────────────────────────────────────── */
static void audit_log(const char *qr_data, int granted)
{
    FILE *f = fopen(AUDIT_LOG, "a");
    time_t now = time(NULL);
    char  *ts  = ctime(&now);
    ts[strcspn(ts, "\n")] = 0;
    if (f) {
        fprintf(f, "[%s] %s: %s\n", ts,
                granted ? "ACCESS GRANTED" : "ACCESS DENIED", qr_data);
        fclose(f);
    }
    printf("[%s] %s: %s\n", ts,
           granted ? "ACCESS GRANTED" : "ACCESS DENIED", qr_data);
}

/* ─── OSD state ──────────────────────────────────────────────────────── */
typedef struct {
    char     line1[64];    /* e.g. "ACCESS GRANTED" */
    char     line2[64];    /* QR data (truncated)   */
    uint16_t banner_col;   /* COL_GREEN or COL_RED  */
    int      frames_left;  /* countdown to blank     */
} OSDState;

static OSDState g_osd = { "", "", COL_BLACK, 0 };

/*
 * Render (or clear) the OSD banner at the bottom of the LCD.
 * Call once per frame after the camera blit.
 */
static void osd_render(void)
{
    int banner_y  = LCD_H - 60;   /* bottom 60 rows */
    int banner_h  = 60;
    int text_scale = 2;           /* 16×16 px glyphs */

    if (g_osd.frames_left <= 0) {
        /* Clear banner area to black */
        osd_fill_rect(0, banner_y, LCD_W, banner_h, COL_BLACK);
        return;
    }

    /* Coloured background */
    osd_fill_rect(0, banner_y, LCD_W, banner_h, g_osd.banner_col);

    /* Line 1: status */
    osd_draw_str(4, banner_y + 4,  g_osd.line1,
                 COL_WHITE, g_osd.banner_col, text_scale);

    /* Line 2: QR data (clipped to fit) */
    char clip[28] = {0};
    strncpy(clip, g_osd.line2, sizeof(clip) - 1);
    osd_draw_str(4, banner_y + 4 + 8 * text_scale + 2, clip,
                 COL_WHITE, g_osd.banner_col, text_scale);

    g_osd.frames_left--;
}

/* ─── Access handler ─────────────────────────────────────────────────── */
static void handle_access(const char *qr_data)
{
    int granted = check_whitelist(qr_data);
    audit_log(qr_data, granted);

    /* Populate OSD */
    strncpy(g_osd.line1,
            granted ? "ACCESS GRANTED" : "ACCESS DENIED",
            sizeof(g_osd.line1) - 1);
    strncpy(g_osd.line2, qr_data, sizeof(g_osd.line2) - 1);
    g_osd.banner_col  = granted ? COL_GREEN : COL_RED;
    g_osd.frames_left = OSD_FRAMES;

    if (granted) {
        printf("Access GRANTED — door open 3 s\n");
        led_set_trigger("none");
        led_blink(3, 500, 200);
        gpio_write(RELAY_PIN,     1);
        gpio_write(GREEN_LED_PIN, 1);
        sleep(3);
        gpio_write(RELAY_PIN,     0);
        gpio_write(GREEN_LED_PIN, 0);
        led_set_trigger("heartbeat");
    } else {
        printf("Access DENIED\n");
        led_set_trigger("none");
        led_blink(5, 100, 100);
        gpio_write(RED_LED_PIN, 1);
        sleep(2);
        gpio_write(RED_LED_PIN, 0);
        led_set_trigger("heartbeat");
    }
}

/* ─── V4L2 buffer struct ─────────────────────────────────────────────── */
struct buffer { void *start; size_t length; };

/* ════════════════════════════════════════════════════════════════════════
 *  MAIN
 * ════════════════════════════════════════════════════════════════════════ */
int main(void)
{
    printf("=== STM32MP157 QR Access Control + LCD Preview ===\n");
    printf("Cooldown : %d s\n", COOLDOWN_SECS);
    printf("OSD      : %d frames (~%d s)\n", OSD_FRAMES, OSD_FRAMES / 10);
    printf("Heartbeat: 3 slow blinks=VALID | 5 fast blinks=INVALID\n\n");

    /* ── 1. Open & map framebuffer ── */
    int fbfd = open(FB_DEV, O_RDWR);
    if (fbfd < 0) { perror("open fb0"); return -1; }

    {
        int sfd = open("/sys/class/graphics/fb0/stride", O_RDONLY);
        char tmp[32] = {0};
        if (sfd < 0) { perror("fb0/stride"); return -1; }
        read(sfd, tmp, sizeof(tmp) - 1);
        close(sfd);
        int stride_bytes = atoi(tmp);
        g_fb_pitch       = stride_bytes / 2;   /* bytes → pixels (RGB565=2B) */
        size_t fb_size   = (size_t)stride_bytes * LCD_H;
        g_fb = mmap(NULL, fb_size,
                    PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
        if (g_fb == MAP_FAILED) { perror("fb mmap"); return -1; }
        printf("FB mapped: stride=%d px (%d bytes)\n",
               g_fb_pitch, stride_bytes);
    }

    /* ── 2. GPIO + heartbeat LED ── */
    led_set_trigger("none");
    led_brightness(0);
    gpio_direction(GREEN_LED_PIN, "out");
    gpio_direction(RELAY_PIN,     "out");
    gpio_direction(RED_LED_PIN,   "out");
    gpio_write(GREEN_LED_PIN, 0);
    gpio_write(RELAY_PIN,     0);
    gpio_write(RED_LED_PIN,   0);
    printf("GPIO initialised  PA%d=green  PA%d=relay  PA%d=red\n",
           GREEN_LED_PIN, RELAY_PIN, RED_LED_PIN);

    /* ── 3. Open camera ── */
    int camfd = open(VIDEO_DEV, O_RDWR);
    if (camfd < 0) { perror("open camera"); return -1; }

    struct v4l2_format fmt = {0};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = CAM_W;
    fmt.fmt.pix.height      = CAM_H;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (ioctl(camfd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT"); return -1;
    }
    printf("Camera: %dx%d %.4s\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           (char *)&fmt.fmt.pix.pixelformat);

    /* Request + mmap buffers */
    struct v4l2_requestbuffers req = {0};
    req.count  = 2;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(camfd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS"); return -1;
    }

    struct buffer buffers[2];
    for (int i = 0; i < 2; i++) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        ioctl(camfd, VIDIOC_QUERYBUF, &buf);
        buffers[i].length = buf.length;
        buffers[i].start  = mmap(NULL, buf.length,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED, camfd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("camera mmap"); return -1;
        }
        ioctl(camfd, VIDIOC_QBUF, &buf);
    }

    enum v4l2_buf_type btype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(camfd, VIDIOC_STREAMON, &btype);
    printf("Camera streaming on %s\n\n", VIDEO_DEV);

    /* ── 4. ZBar scanner ── */
    zbar_image_scanner_t *scanner = zbar_image_scanner_create();
    zbar_image_scanner_set_config(scanner, 0, ZBAR_CFG_ENABLE, 1);

    /* ── 5. Cooldown tracking ── */
    char   last_qr[256] = "";
    time_t last_time    = 0;

    /* ── 6. Main loop ── */
    printf("Running — Ctrl-C to quit.\n");

    while (1)
    {
        /* Dequeue one camera frame */
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(camfd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
            usleep(50000);
            continue;
        }

        unsigned char *yuyv = (unsigned char *)buffers[buf.index].start;

        /* ── 6a. Blit YUYV → RGB565 → framebuffer (H-flipped) ── */
        for (int ly = 0; ly < LCD_H; ly++) {
            int sy = ly * CAM_H / LCD_H;
            for (int lx = 0; lx < LCD_W; lx++) {
                int sx   = (LCD_W - 1 - lx) * CAM_W / LCD_W;   /* H-flip */
                int pair = (sy * CAM_W + sx) & ~1;
                unsigned char *p = yuyv + pair * 2;
                int yy = (sx & 1) ? p[2] : p[0];
                int r, g, b;
                yuv_to_rgb(yy, p[1], p[3], &r, &g, &b);
                g_fb[ly * g_fb_pitch + lx] = rgb565(r, g, b);
            }
        }

        /* ── 6b. OSD overlay (drawn on top of camera image) ── */
        osd_render();

        /* ── 6c. ZBar scan (Y-channel only) ── */
        unsigned char *gray = malloc(CAM_W * CAM_H);
        if (gray) {
            for (int i = 0; i < CAM_W * CAM_H; i++)
                gray[i] = yuyv[i * 2];   /* Y byte at even offsets */

            zbar_image_t *img = zbar_image_create();
            zbar_image_set_format(img, zbar_fourcc('Y','8','0','0'));
            zbar_image_set_size(img, CAM_W, CAM_H);
            zbar_image_set_data(img, gray, CAM_W * CAM_H,
                                zbar_image_free_data);

            if (zbar_scan_image(scanner, img) > 0) {
                const zbar_symbol_t *sym =
                    zbar_image_first_symbol(img);
                while (sym) {
                    const char *data = zbar_symbol_get_data(sym);
                    time_t now = time(NULL);

                    int fresh = (strcmp(data, last_qr) != 0) ||
                                (difftime(now, last_time) > COOLDOWN_SECS);
                    if (fresh) {
                        printf("QR: %s\n", data);
                        handle_access(data);
                        strncpy(last_qr, data, sizeof(last_qr) - 1);
                        last_qr[sizeof(last_qr) - 1] = '\0';
                        last_time = now;
                    } else {
                        int rem = COOLDOWN_SECS -
                                  (int)difftime(now, last_time);
                        printf("Cooldown: %d s\n", rem);
                    }
                    sym = zbar_symbol_next(sym);
                }
            }

            zbar_image_destroy(img);   /* also frees gray via callback */
        }

        /* Re-queue the buffer */
        ioctl(camfd, VIDIOC_QBUF, &buf);

        /* ~10 fps cap */
        usleep(100000);
    }

    /* ── Cleanup (unreachable in practice) ── */
    ioctl(camfd, VIDIOC_STREAMOFF, &btype);
    zbar_image_scanner_destroy(scanner);
    close(camfd);
    close(fbfd);
    gpio_write(GREEN_LED_PIN, 0);
    gpio_write(RELAY_PIN,     0);
    gpio_write(RED_LED_PIN,   0);
    led_set_trigger("heartbeat");
    return 0;
}
