#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <kmodchar.h>

#define CTL "/dev/kmodctl"
#define DEV "/dev/dsp"

static unsigned char silence[512];

int main(int argc, char **argv)
{
    int ctl, dsp, duplicate;

    if (argc != 2)
        goto usage;
    if (!strcmp(argv[1], "absent")) {
        dsp = open(DEV, O_WRONLY);
        if (dsp >= 0 || errno != ENODEV)
            goto fail;
        puts("PASS /dev/dsp unavailable");
        return 0;
    }
    if (strcmp(argv[1], "busy"))
        goto usage;
    ctl = open(CTL, O_RDWR);
    if (ctl < 0)
        goto fail;
    dsp = open(DEV, O_WRONLY);
    if (dsp < 0)
        goto fail;
    memset(silence, 0x80, sizeof(silence));
    if (write(dsp, silence, sizeof(silence)) != sizeof(silence))
        goto fail;
    if (ioctl(ctl, KMODCHAR_UNLOAD, 0) >= 0 || errno != EBUSY)
        goto fail;
    duplicate = dup(dsp);
    if (duplicate < 0 || close(dsp) ||
        ioctl(ctl, KMODCHAR_UNLOAD, 0) >= 0 || errno != EBUSY ||
        close(duplicate) || close(ctl))
        goto fail;
    puts("PASS queued DMA and final open reference prevent unload");
    return 0;

usage:
    fprintf(stderr, "usage: sbmodtest absent | busy\n");
    return 2;
fail:
    fprintf(stderr, "FAIL sbmodtest %s (errno %d)\n", argv[1], errno);
    return 1;
}
