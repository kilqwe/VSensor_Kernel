#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>

#define DEVICE "/dev/vsensor"
#define BUFFER_SIZE 128

int main(void)
{
    int fd;
    int epfd;
    struct epoll_event event;
    struct epoll_event events[1];
    char buffer[BUFFER_SIZE];

    fd = open(DEVICE, O_RDONLY);

    if (fd == -1) {
        perror("open");
        return 1;
    }

    epfd = epoll_create1(0);

    if (epfd == -1) {
        perror("epoll_create1");
        close(fd);
        return 1;
    }

    event.events = EPOLLIN;
    event.data.fd = fd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl");
        close(epfd);
        close(fd);
        return 1;
    }

    printf("Waiting for sensor events...\n");

    while (1) {
        int ready;
        ssize_t bytes_read;

        ready = epoll_wait(epfd, events, 1, -1);

        if (ready == -1) {
            if (errno == EINTR)
                continue;

            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < ready; i++) {
            if (events[i].events & EPOLLIN) {
                bytes_read = read(events[i].data.fd,
                                  buffer,
                                  BUFFER_SIZE - 1);

                if (bytes_read == -1) {
                    perror("read");
                    goto cleanup;
                }

                buffer[bytes_read] = '\0';

                printf("Sensor: %s", buffer);
            }
        }
    }

cleanup:
    close(epfd);
    close(fd);

    return 0;
}
