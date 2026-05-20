#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define GPIO_PATH "/sys/class/gpio"

/* GPIO Numbers from our debug output */
#define GREEN_LED  "PA13"
#define RELAY      "PA14"
#define RED_LED    "PA15"

int gpio_export(const char *name) {
    /* Already exported by device tree — just verify */
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", GPIO_PATH, name);
    if (access(path, F_OK) == 0) {
        printf("GPIO %s already available\n", name);
        return 0;
    }
    printf("GPIO %s not found!\n", name);
    return -1;
}

int gpio_set_direction(const char *name, const char *direction) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s/direction", GPIO_PATH, name);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("open direction");
        return -1;
    }
    write(fd, direction, strlen(direction));
    close(fd);
    return 0;
}

int gpio_write(const char *name, int value) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s/value", GPIO_PATH, name);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("open value");
        return -1;
    }
    write(fd, value ? "1" : "0", 1);
    close(fd);
    return 0;
}

int gpio_read(const char *name) {
    char path[64];
    char buf[4];
    snprintf(path, sizeof(path), "%s/%s/value", GPIO_PATH, name);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open value");
        return -1;
    }
    read(fd, buf, sizeof(buf));
    close(fd);
    return atoi(buf);
}

int main() {
    printf("=== STM32MP157 GPIO Test ===\n");

    /* Setup all pins as output */
    gpio_set_direction(GREEN_LED, "out");
    gpio_set_direction(RELAY,     "out");
    gpio_set_direction(RED_LED,   "out");

    /* Test sequence */
    printf("Green LED ON\n");
    gpio_write(GREEN_LED, 1);
    sleep(1);

    printf("Green LED OFF, Red LED ON\n");
    gpio_write(GREEN_LED, 0);
    gpio_write(RED_LED, 1);
    sleep(1);

    printf("Red LED OFF, Relay ON\n");
    gpio_write(RED_LED, 0);
    gpio_write(RELAY, 1);
    sleep(1);

    printf("Relay OFF\n");
    gpio_write(RELAY, 0);

    /* Blink green LED 5 times */
    printf("Blinking Green LED 5 times...\n");
    for (int i = 0; i < 5; i++) {
        gpio_write(GREEN_LED, 1);
        usleep(300000);  /* 300ms */
        gpio_write(GREEN_LED, 0);
        usleep(300000);
    }

    printf("GPIO test complete!\n");
    return 0;
}
