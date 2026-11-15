// SPDX-License-Identifier: GPL-2.0-only
// Device-only acceptance helper. Injects a released key or touchpad button.
#define _GNU_SOURCE
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "power") && strcmp(argv[1], "keyboard") && strcmp(argv[1], "touchpad") && strcmp(argv[1], "f10"))) return 2;
    int power = !strcmp(argv[1], "power");
    int touchpad = !strcmp(argv[1], "touchpad");
    int f10 = !strcmp(argv[1], "f10");
    const char *wanted = power ? "gpio-keys" : touchpad ? "W202DS ZTE Touchpad" : "W202DS ZTE Keyboard";
    glob_t paths = {0};
    if (glob("/dev/input/event*", 0, NULL, &paths)) return 1;
    int result = 1;
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        int fd = open(paths.gl_pathv[i], O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        char name[128] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 && !strcmp(name, wanted)) {
            struct input_event events[4] = {
                {.type = EV_KEY, .code = power ? KEY_POWER : touchpad ? BTN_LEFT : f10 ? KEY_F10 : KEY_LEFTSHIFT, .value = 1},
                {.type = EV_SYN, .code = SYN_REPORT},
                {.type = EV_KEY, .code = power ? KEY_POWER : touchpad ? BTN_LEFT : f10 ? KEY_F10 : KEY_LEFTSHIFT, .value = 0},
                {.type = EV_SYN, .code = SYN_REPORT},
            };
            result = write(fd, events, sizeof(events)) != sizeof(events);
            if (!result) printf("INPUT_INJECT_PASS device=%s kind=%s\n", name, argv[1]);
            else perror("input write");
            close(fd);
            break;
        }
        close(fd);
    }
    globfree(&paths);
    return result;
}
