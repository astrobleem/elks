#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <kmodchar.h>

#define CTL "/dev/kmodctl"
#define DEV "/dev/kmoddemo"

static struct kmodchar_upload image;
static const char message[] = "ELKS module OK\n";

static void check(int ok, const char *name)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s (errno %d)\n", name, errno);
        exit(1);
    }
    printf("PASS %s\n", name);
}

static void test_read(int fd)
{
    char out[sizeof(message)];
    int a = read(fd, out, 3);
    int b = read(fd, out + 3, sizeof(out) - 3);

    check(a == 3 && b == sizeof(message) - 4 &&
        !memcmp(out, message, sizeof(message) - 1) &&
        read(fd, out, 1) == 0, "module bytes, partial reads and EOF");
}

int main(int argc, char **argv)
{
    int img, ctl, dev, dupfd, n, status;
    pid_t child;
    unsigned char v;

    if (argc != 2) {
        fprintf(stderr, "usage: kmodtest hello.kmc\n");
        return 2;
    }
    img = open(argv[1], O_RDONLY);
    check(img >= 0, "module image on disk");
    n = read(img, image.image, sizeof(image.image));
    check(n > KMODCHAR_HEADER_LEN, "module image read");
    check(!read(img, &v, 1) && !close(img), "module image fits fixture");
    image.bytes = n;
    ctl = open(CTL, O_RDWR);
    check(ctl >= 0, "root control");
    check(open(DEV, O_RDONLY) < 0 && errno == ENODEV, "device absent at boot");

    v = image.image[3];
    image.image[3] = '9';
    check(ioctl(ctl, KMODCHAR_LOAD, &image) < 0 && errno == EINVAL,
        "unrecognized ABI rejected");
    image.image[3] = v;
    v = image.image[6];
    image.image[6] = 0xff;
    check(ioctl(ctl, KMODCHAR_LOAD, &image) < 0 && errno == EINVAL,
        "out-of-range entry rejected");
    image.image[6] = v;

    check(!ioctl(ctl, KMODCHAR_LOAD, &image), "separate module loaded");
    check(ioctl(ctl, KMODCHAR_LOAD, &image) < 0 && errno == EBUSY,
        "second load rejected");
    child = fork();
    check(child != (pid_t)-1, "fork for inherited control test");
    if (!child) {
        if (setuid(1))
            _exit(2);
        _exit(ioctl(ctl, KMODCHAR_UNLOAD, 0) < 0 && errno == EPERM ? 0 : 3);
    }
    check(wait(&status) == child && status == 0, "nonroot inherited ioctl denied");
    dev = open(DEV, O_RDONLY);
    check(dev >= 0, "loaded device opens");
    test_read(dev);

    dupfd = dup(dev);
    check(dupfd >= 0, "duplicated open description");
    check(ioctl(ctl, KMODCHAR_UNLOAD, 0) < 0 && errno == EBUSY,
        "unload refused while open");
    check(!close(dev) && ioctl(ctl, KMODCHAR_UNLOAD, 0) < 0 &&
        errno == EBUSY, "last reference still pins module");
    check(!close(dupfd) && !ioctl(ctl, KMODCHAR_UNLOAD, 0),
        "final close permits unload");
    check(open(DEV, O_RDONLY) < 0 && errno == ENODEV, "unloaded device unavailable");
    check(!ioctl(ctl, KMODCHAR_LOAD, &image), "module reload");
    dev = open(DEV, O_RDONLY);
    check(dev >= 0, "reloaded device opens");
    test_read(dev);
    check(!close(dev) && !ioctl(ctl, KMODCHAR_UNLOAD, 0) && !close(ctl),
        "second unload and return");
    return 0;
}
