#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <kmodchar.h>

#define CTL "/dev/kmodctl"
#define DEV "/dev/kmoddemo"

static struct kmodchar_upload image;
static const char one[] = "ELKS LPT1 module first\r\n\f";
static const char two[] = "ELKS LPT1 module reload\r\n\f";

static void check(int ok, const char *name)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s (errno %d)\n", name, errno);
        exit(1);
    }
    printf("PASS %s\n", name);
}

int main(int argc, char **argv)
{
    int img, ctl, dev, dupfd;
    int n;
    unsigned char extra;

    if (argc != 2) {
        fprintf(stderr, "usage: lpttest lpt1.kmc\n");
        return 2;
    }
    img = open(argv[1], O_RDONLY);
    check(img >= 0, "printer module image available");
    n = read(img, image.image, sizeof(image.image));
    check(n > KMODCHAR_HEADER_LEN && !read(img, &extra, 1) && !close(img),
        "printer module image read");
    image.bytes = n;
    ctl = open(CTL, O_RDWR);
    check(ctl >= 0, "root control open");
    check(open(DEV, O_WRONLY) < 0 && errno == ENODEV,
        "printer device absent before load");
    extra = image.image[8];
    image.image[8] = 0x78 - 1;
    check(ioctl(ctl, KMODCHAR_LOAD, &image) < 0 && errno == EINVAL,
        "unsupported LPT base rejected");
    image.image[8] = extra;

    check(!ioctl(ctl, KMODCHAR_LOAD, &image), "printer module loaded");
    check(open(DEV, O_RDONLY) < 0 && errno == EACCES, "printer is write-only");
    dev = open(DEV, O_WRONLY);
    check(dev >= 0, "printer opened");
    check(open(DEV, O_WRONLY) < 0 && errno == EBUSY, "second writer refused");
    check(write(dev, one, sizeof(one)-1) == sizeof(one)-1, "first print accepted");
    dupfd = dup(dev);
    check(dupfd >= 0, "duplicate writer");
    check(ioctl(ctl, KMODCHAR_UNLOAD, 0) < 0 && errno == EBUSY,
        "unload refused while open");
    check(!close(dev) && ioctl(ctl, KMODCHAR_UNLOAD, 0) < 0 && errno == EBUSY,
        "duplicate still pins module");
    check(!close(dupfd) && !ioctl(ctl, KMODCHAR_UNLOAD, 0),
        "last close permits printer module unload");
    check(open(DEV, O_WRONLY) < 0 && errno == ENODEV,
        "printer unavailable after unload");

    check(!ioctl(ctl, KMODCHAR_LOAD, &image), "printer module reloaded");
    dev = open(DEV, O_WRONLY);
    check(dev >= 0 && write(dev, two, sizeof(two)-1) == sizeof(two)-1,
        "second print accepted");
    check(!close(dev) && !ioctl(ctl, KMODCHAR_UNLOAD, 0) && !close(ctl),
        "printer module closed and unloaded again");
    return 0;
}
