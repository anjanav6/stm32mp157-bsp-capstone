#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define DEVICE "/dev/video0"
#define WIDTH  640
#define HEIGHT 480
#define OUTPUT "/tmp/frame.jpg"

struct buffer {
    void   *start;
    size_t  length;
};

int main() {
    int fd;
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    struct buffer *buffers;
    enum v4l2_buf_type type;
    int i;

    printf("=== STM32MP157 Camera Capture ===\n");

    /* Open device */
    fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    printf("Camera opened: %s\n", DEVICE);

    /* Query capabilities */
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        return -1;
    }
    printf("Driver: %s\nCard: %s\n", cap.driver, cap.card);

    /* Set format */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = WIDTH;
    fmt.fmt.pix.height      = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        /* Try YUYV if MJPEG fails */
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
            perror("VIDIOC_S_FMT");
            return -1;
        }
        printf("Format: YUYV %dx%d\n", WIDTH, HEIGHT);
    } else {
        printf("Format: MJPEG %dx%d\n", WIDTH, HEIGHT);
    }

    /* Request buffers */
    memset(&req, 0, sizeof(req));
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    /* Map buffers */
    buffers = calloc(req.count, sizeof(*buffers));
    for (i = 0; i < req.count; i++) {
        memset(&buf, 0, sizeof(buf));
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
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMON, &type);
    printf("Streaming started...\n");

    /* Capture one frame */
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    ioctl(fd, VIDIOC_DQBUF, &buf);
    printf("Frame captured: %d bytes\n", buf.bytesused);

    /* Save to file */
    FILE *f = fopen(OUTPUT, "wb");
    fwrite(buffers[buf.index].start, buf.bytesused, 1, f);
    fclose(f);
    printf("Saved to %s\n", OUTPUT);

    /* Stop streaming */
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    close(fd);

    printf("Camera capture complete!\n");
    return 0;
}


