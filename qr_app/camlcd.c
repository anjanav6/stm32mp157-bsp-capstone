#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/fb.h>
#include<string.h>

#define WIDTH   640
#define HEIGHT  480

struct buffer {
    void *start;
    size_t length;
};

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) |
           ((g & 0xFC) << 3) |
           ( b >> 3);
}

int main(void)
{
    /* Open framebuffer */
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) {
        perror("fb0");
        return 1;
    }

    uint16_t *fb = mmap(NULL,
                        480 * 800 * 2,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        fbfd,
                        0);

    if (fb == MAP_FAILED) {
        perror("fb mmap");
        return 1;
    }

    /* Open camera */
    int cam = open("/dev/video0", O_RDWR);
    if (cam < 0) {
        perror("video0");
        return 1;
    }

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    ioctl(cam, VIDIOC_S_FMT, &fmt);

    struct v4l2_requestbuffers req = {0};
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    ioctl(cam, VIDIOC_REQBUFS, &req);

    struct buffer buffers[2];

    for (int i = 0; i < 2; i++) {
        struct v4l2_buffer buf = {0};

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        ioctl(cam, VIDIOC_QUERYBUF, &buf);

        buffers[i].length = buf.length;
        buffers[i].start =
            mmap(NULL,
                 buf.length,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 cam,
                 buf.m.offset);

        ioctl(cam, VIDIOC_QBUF, &buf);
    }

    enum v4l2_buf_type type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ioctl(cam, VIDIOC_STREAMON, &type);

    printf("Displaying camera on LCD...\n");

    while (1) {

        struct v4l2_buffer buf = {0};

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(cam, VIDIOC_DQBUF, &buf) < 0)
            continue;

        uint8_t *yuyv = buffers[buf.index].start;

        /* Clear LCD */
        memset(fb, 0, 480 * 800 * 2);

        /* Display 480x480 centered */
        for (int y = 0; y < 480; y++) {

            int lcd_y = y + 160;

            for (int x = 0; x < 480; x++) {

                int cam_x = x * WIDTH / 480;

                uint8_t Y =
                    yuyv[(y * WIDTH + cam_x) * 2];

                fb[lcd_y * 480 + x] =
                    rgb565(Y, Y, Y);
            }
        }

        ioctl(cam, VIDIOC_QBUF, &buf);
    }

    return 0;
}
