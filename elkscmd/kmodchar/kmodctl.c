#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <kmodchar.h>

#define CONTROL "/dev/kmodctl"

static int read_image(const char *path, struct kmodchar_upload *image)
{
    int fd, n, r;
    unsigned char extra;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    for (n = 0; n < (int)sizeof(image->image); n += r) {
        r = read(fd, image->image + n, sizeof(image->image) - n);
        if (r <= 0)
            break;
    }
    if (n == sizeof(image->image)) {
        r = read(fd, &extra, 1);
        if (r > 0) {
            errno = EFBIG;
            r = -1;
        }
    }
    if (r >= 0 && n <= KMODCHAR_HEADER_LEN) {
        errno = EINVAL;
        r = -1;
    }
    close(fd);
    image->bytes = n;
    return r < 0 ? -1 : 0;
}

int main(int argc, char **argv)
{
    struct kmodchar_upload image;
    int fd, rc;

    if (!((argc == 3 && !strcmp(argv[1], "load")) ||
          (argc == 2 && !strcmp(argv[1], "unload")))) {
        fprintf(stderr, "usage: kmodctl load module.kmc | unload\n");
        return 2;
    }
    if (argc == 3 && read_image(argv[2], &image) < 0) {
        perror(argv[2]);
        return 1;
    }
    fd = open(CONTROL, O_RDWR);
    if (fd < 0) {
        perror(CONTROL);
        return 1;
    }
    rc = argc == 3 ? ioctl(fd, KMODCHAR_LOAD, &image) :
        ioctl(fd, KMODCHAR_UNLOAD, 0);
    if (rc < 0)
        perror(argv[1]);
    close(fd);
    return rc < 0 ? 1 : 0;
}
