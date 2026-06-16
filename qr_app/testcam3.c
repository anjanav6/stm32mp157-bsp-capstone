#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define VIDEO_DEV "/dev/video0"
#define FB_DEV    "/dev/fb0"

#define CAM_W 640
#define CAM_H 480

#define LCD_W 480
#define LCD_H 800

struct buffer {
    void *start;
    size_t length;
};

static inline uint16_t rgb565(int r, int g, int b)
{
    return ((r & 0xF8) << 8) |
           ((g & 0xFC) << 3) |
           (b >> 3);
}

static inline void yuv_to_rgb(
    int y, int u, int v,
    int *r, int *g, int *b)
{
    int c = y - 16;
    int d = u - 128;
    int e = v - 128;

    int rt = (298 * c + 409 * e + 128) >> 8;
    int gt = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int bt = (298 * c + 516 * d + 128) >> 8;

    if (rt < 0) rt = 0;
    if (rt > 255) rt = 255;

    if (gt < 0) gt = 0;
    if (gt > 255) gt = 255;

    if (bt < 0) bt = 0;
    if (bt > 255) bt = 255;

    *r = rt;
    *g = gt;
    *b = bt;
}

int main(void)
{
    printf("=== STM32 Camera LCD Test ===\n");

    /* Open framebuffer */
    int fbfd = open(FB_DEV, O_RDWR);
    if (fbfd < 0) {
        perror("open fb0");
        return -1;
    }

    /* Read stride */
    int stride_fd = open("/sys/class/graphics/fb0/stride", O_RDONLY);
    char tmp[32] = {0};

    if (stride_fd < 0) {
        perror("stride");
        return -1;
    }

    read(stride_fd, tmp, sizeof(tmp)-1);
    close(stride_fd);

    int stride_bytes = atoi(tmp);
    int fb_pitch = stride_bytes / 2;

    printf("Framebuffer stride = %d bytes (%d pixels)\n",
           stride_bytes,
           fb_pitch);

    size_t fb_size = stride_bytes * LCD_H;

    uint16_t *fb = mmap(NULL,
                        fb_size,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        fbfd,
                        0);

    if (fb == MAP_FAILED) {
        perror("fb mmap");
        return -1;
    }

    printf("Framebuffer mapped\n");

    /* Open camera */
    int camfd = open(VIDEO_DEV, O_RDWR);
    if (camfd < 0) {
        perror("open camera");
        return -1;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAM_W;
    fmt.fmt.pix.height = CAM_H;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

    if (ioctl(camfd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return -1;
    }

    printf("Camera format:\n");
    printf(" Width  = %d\n", fmt.fmt.pix.width);
    printf(" Height = %d\n", fmt.fmt.pix.height);
    printf(" FourCC = %.4s\n",
           (char*)&fmt.fmt.pix.pixelformat);

    /* Request buffers */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));

    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(camfd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    struct buffer buffers[2];

    for (int i = 0; i < 2; i++) {

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(camfd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        buffers[i].length = buf.length;

        buffers[i].start =
            mmap(NULL,
                 buf.length,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 camfd,
                 buf.m.offset);

        if (buffers[i].start == MAP_FAILED) {
            perror("camera mmap");
            return -1;
        }

        ioctl(camfd, VIDIOC_QBUF, &buf);
    }

    enum v4l2_buf_type type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ioctl(camfd, VIDIOC_STREAMON, &type);

    printf("Displaying camera on LCD...\n");

    int printed = 0;

    while (1)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(camfd, VIDIOC_DQBUF, &buf) < 0)
            continue;

        unsigned char *src =
            (unsigned char *)buffers[buf.index].start;

        if (!printed) {
            printf("First bytes:\n");
            for (int i = 0; i < 16; i++)
                printf("%02X ", src[i]);
            printf("\n");
            printed = 1;
        }

        for (int y = 0; y < LCD_H; y++)
        {
            int sy = y * CAM_H / LCD_H;

            for (int x = 0; x < LCD_W; x++)
            {
                int sx = x * CAM_W / LCD_W;

                int pair = (sy * CAM_W + sx) & ~1;

                unsigned char *p = src + pair * 2;

                int y0 = p[0];
                int u  = p[1];
                int y1 = p[2];
                int v  = p[3];

                int yy = (sx & 1) ? y1 : y0;

                int r, g, b;

                yuv_to_rgb(
                    yy, u, v,
                    &r, &g, &b);

                fb[y * fb_pitch + x] =
                    rgb565(r, g, b);
            }
        }

        ioctl(camfd, VIDIOC_QBUF, &buf);
    }

    return 0;
}
