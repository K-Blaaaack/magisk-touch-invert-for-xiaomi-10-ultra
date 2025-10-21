/* touch_remap.c
   Simple evdev -> uinput remapper that can invert X/Y.
   Compile with: clang -O2 -static -o touch_remap touch_remap.c
   (静态链接会更保险，但在 Android 上可能需要其他处理)
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <time.h>

static void log_msg(const char *logfile, const char *fmt, ...) {
    if (!logfile) return;
    FILE *f = fopen(logfile, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    fprintf(f, "\n");
    va_end(ap);
    fclose(f);
}

static int open_input(const char *path) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    return fd;
}

static int setup_uinput(int src_fd, int max_x, int max_y, int min_x, int min_y) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_FINGER);

    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, sizeof(uidev.name), "touch_remap_uinput");
    uidev.id.bustype = BUS_VIRTUAL;
    uidev.id.vendor = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    // set abs ranges
    uidev.absmin[ABS_MT_POSITION_X] = min_x;
    uidev.absmax[ABS_MT_POSITION_X] = max_x;
    uidev.absfuzz[ABS_MT_POSITION_X] = 0;
    uidev.absflat[ABS_MT_POSITION_X] = 0;

    uidev.absmin[ABS_MT_POSITION_Y] = min_y;
    uidev.absmax[ABS_MT_POSITION_Y] = max_y;
    uidev.absfuzz[ABS_MT_POSITION_Y] = 0;
    uidev.absflat[ABS_MT_POSITION_Y] = 0;

    write(fd, &uidev, sizeof(uidev));
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int get_abs_range(int fd, int code, int *minv, int *maxv) {
    struct input_absinfo absinfo;
    if (ioctl(fd, EVIOCGABS(code), &absinfo) < 0) return -1;
    *minv = absinfo.minimum;
    *maxv = absinfo.maximum;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <input_dev> <invert_y 0|1> <invert_x 0|1> [logfile]\n", argv[0]);
        return 1;
    }
    const char *inpath = argv[1];
    int invert_y = atoi(argv[2]);
    int invert_x = atoi(argv[3]);
    const char *logfile = NULL;
    if (argc >= 5) logfile = argv[4];

    log_msg(logfile, "touch_remap starting. in=%s invert_y=%d invert_x=%d", inpath, invert_y, invert_x);

    int in_fd = open_input(inpath);
    if (in_fd < 0) {
        log_msg(logfile, "failed open input %s: %s", inpath, strerror(errno));
        return 2;
    }

    int min_x = 0, max_x = 32767, min_y = 0, max_y = 32767;
    if (get_abs_range(in_fd, ABS_MT_POSITION_X, &min_x, &max_x) < 0) {
        log_msg(logfile, "failed get abs X, using defaults");
    }
    if (get_abs_range(in_fd, ABS_MT_POSITION_Y, &min_y, &max_y) < 0) {
        log_msg(logfile, "failed get abs Y, using defaults");
    }
    log_msg(logfile, "abs X range: %d..%d, Y range: %d..%d", min_x, max_x, min_y, max_y);

    int out_fd = setup_uinput(in_fd, max_x, max_y, min_x, min_y);
    if (out_fd < 0) {
        log_msg(logfile, "failed setup uinput: %s", strerror(errno));
        close(in_fd);
        return 3;
    }

    struct input_event ev;
    while (1) {
        ssize_t r = read(in_fd, &ev, sizeof(ev));
        if (r == sizeof(ev)) {
            struct input_event oev = ev;
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_MT_POSITION_Y && invert_y) {
                    // invert Y relative to min/max
                    int val = ev.value;
                    int inv = max_y - (val - min_y);
                    oev.value = inv;
                } else if (ev.code == ABS_MT_POSITION_X && invert_x) {
                    int val = ev.value;
                    int inv = max_x - (val - min_x);
                    oev.value = inv;
                } else {
                    oev.value = ev.value;
                }
            } else {
                oev = ev;
            }
            // write to uinput
            ssize_t w = write(out_fd, &oev, sizeof(oev));
            if (w != sizeof(oev)) {
                log_msg(logfile, "write uinput failed: %s", strerror(errno));
                // try continue
            }
        } else {
            // no data; sleep a bit
            usleep(1000);
        }
    }

    // never reach
    ioctl(out_fd, UI_DEV_DESTROY);
    close(out_fd);
    close(in_fd);
    return 0;
}
