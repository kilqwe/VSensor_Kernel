#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "../include/vsensor_ioctl.h"


#define DEVICE "/dev/vsensor"
#define BUFFER_SIZE 128
#define SENSOR_INTERVAL_MS 2000

int main(void)
{
    char buffer[BUFFER_SIZE];
    unsigned int interval;
    int fd;

    fd = open(DEVICE, O_RDONLY);

    if (fd == -1) {
        perror("open");
        return 1;
    }

    printf("Opened %s\n", DEVICE);

    if (ioctl(fd, VSENSOR_GET_INTERVAL, &interval) == -1) {
        perror("ioctl GET_INTERVAL");
        close(fd);
        return 1;
    }

    printf("Current interval: %u ms\n", interval);

    interval = 100;

    if (ioctl(fd, VSENSOR_SET_INTERVAL, &interval) == -1) {
        perror("ioctl SET_INTERVAL");
        close(fd);
        return 1;
    }

    printf("Set interval to %u ms\n", interval);

    while (1) {
        ssize_t bytes_read;

        bytes_read = read(fd, buffer, BUFFER_SIZE - 1);

        if (bytes_read == -1) {
            perror("read");
            break;
        }

        buffer[bytes_read] = '\0';

        printf("Sensor reading: %s", buffer);
    }

    close(fd);

    return 0;
}
