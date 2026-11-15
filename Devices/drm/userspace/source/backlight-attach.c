// SPDX-License-Identifier: GPL-2.0-only
/* Register the deferred backlight provider without changing its brightness. */
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    int fd = open("/dev/w202blctl", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open backlight control");
        return 1;
    }
    int result = ioctl(fd, _IO('W', 2));
    if (result < 0)
        perror("attach backlight provider");
    close(fd);
    return result < 0;
}
