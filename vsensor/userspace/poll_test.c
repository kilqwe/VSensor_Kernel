#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

#define DEVICE "/dev/vsensor"
#define BUFFER_SIZE 128

int main(void)
{
    int fd;
    struct pollfd pfd;
    char buffer[BUFFER_SIZE];

    fd = open(DEVICE, O_RDONLY);

    if (fd == -1) {
        perror("open");
        return 1;
    }

    printf("Waiting for sensor data...\n");

    pfd.fd = fd;
    pfd.events = POLLIN;

    while (1) {
        int ret;
        ssize_t bytes_read;

        ret = poll(&pfd, 1, -1);

        if (ret == -1) {
            perror("poll");
            break;
        }

        if (pfd.revents & POLLIN) {
            bytes_read = read(fd, buffer, BUFFER_SIZE - 1);

            if (bytes_read == -1) {
                perror("read");
                break;
            }

            buffer[bytes_read] = '\0';

            printf("Sensor: %s", buffer);
        }
    }

    close(fd);
    return 0;
}

