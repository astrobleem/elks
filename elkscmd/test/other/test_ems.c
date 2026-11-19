/*
 * Native test of /dev/ems. Run on a kernel booted with ems=PORT,FRAME.
 * Prints one line per group and "EMS TEST PASSED" or the first failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linuxmt/ems.h>

static int fd;
static char buf[4096], back[4096];

static void fail(const char *what)
{
    printf("EMS TEST FAILED: %s (errno %d)\n", what, errno);
    exit(1);
}
#define MUST(c, what) do { if (!(c)) fail(what); } while (0)

static int xfer(int cmd, unsigned handle, unsigned long offset, char *p, unsigned n)
{
    struct ems_xfer x;
    x.handle = handle; x.count = n; x.offset = offset; x.buf = p;
    return ioctl(fd, cmd, &x);
}
static int alloc(unsigned pages)
{
    unsigned short v = pages;
    return ioctl(fd, EMS_ALLOC, &v) < 0 ? -1 : (int)v;
}
static void fill(char *p, unsigned n, unsigned seed)
{
    while (n--) { seed = seed * 25173U + 13849U; *p++ = (char)(seed >> 8); }
}

int main(void)
{
    struct ems_info info;
    unsigned pages, i, n;
    int a, b, status;
    unsigned long at;
    time_t start;
    pid_t pid;

    fd = open("/dev/ems", O_RDWR);
    if (fd < 0) { printf("EMS not available (errno %d): nothing to test\n", errno); return 2; }
    MUST(ioctl(fd, EMS_GETINFO, &info) == 0, "getinfo");
    pages = info.pages;
    printf("info: %u pages, %u free, frame %x, port %x\n", pages, info.free, info.frame, info.port);
    MUST(pages >= 9 && info.free == pages, "test requires at least 9 free pages");

    /* single client */
    MUST(open("/dev/ems", O_RDWR) < 0 && errno == EBUSY, "second open must be EBUSY");
    pid = fork();
    MUST(pid != (pid_t)-1, "fork competing opener");
    if (!pid) _exit(open("/dev/ems", O_RDWR) < 0 && errno == EBUSY ? 0 : 1);
    wait(&status); MUST(WIFEXITED(status) && !WEXITSTATUS(status), "other process must get EBUSY");
    puts("single client: ok");

    /* invalid handles and arguments */
    errno = 0; MUST(alloc(0) < 0 && errno == EINVAL, "alloc 0");
    MUST(alloc(pages + 1) < 0 && errno == ENOMEM, "alloc too many");
    MUST(ioctl(fd, EMS_FREE, 0) < 0 && errno == EBADF, "free 0");
    MUST(ioctl(fd, EMS_FREE, 7) < 0 && errno == EBADF, "free unowned");
    MUST(xfer(EMS_READ, 3, 0, back, 16) < 0 && errno == EBADF, "read unowned handle");
    MUST(xfer(EMS_WRITE, 0xFFFF, 0, back, 16) < 0 && errno == EBADF, "write wild handle");
    MUST(ioctl(fd, 99, 0) < 0, "unknown ioctl");
    MUST(ioctl(fd, EMS_GETINFO, (void *)0xFFFC) < 0 && errno == EFAULT,
         "wrapping info structure");
    MUST(ioctl(fd, EMS_ALLOC, (void *)0xFFFF) < 0 && errno == EFAULT,
         "wrapping allocation result");
    MUST(ioctl(fd, EMS_READ, (void *)0xFFFC) < 0 && errno == EFAULT,
         "wrapping read structure");
    MUST(ioctl(fd, EMS_WRITE, (void *)0xFFFC) < 0 && errno == EFAULT,
         "wrapping write structure");
    MUST(ioctl(fd, EMS_GETINFO, &info) == 0 && info.free == pages,
         "invalid ioctl buffers allocated nothing");
    puts("invalid handles: ok");

    /* bank boundaries and bounds */
    a = alloc(3); b = alloc(2);
    MUST(a > 0 && b > 0 && a != b, "alloc");
    MUST(xfer(EMS_WRITE, a, 49152UL, buf, 1) < 0 && errno == EINVAL, "write past end");
    MUST(xfer(EMS_WRITE, a, 49151UL, buf, 2) < 0 && errno == EINVAL, "write crossing end");
    MUST(xfer(EMS_READ, a, 0xFFFFFFFFUL, buf, 2) < 0 && errno == EINVAL, "wrapping offset");
    MUST(xfer(EMS_READ, b, 32768UL, buf, 1) < 0 && errno == EINVAL, "read past end");
    MUST(xfer(EMS_READ, a, 0, (char *)0xFFF0, 4096) < 0 && errno == EFAULT, "buffer wrapping the data segment");
    for (at = 0, i = 0; at < 49152UL; at += sizeof(buf), i++) {
        fill(buf, sizeof(buf), i);
        MUST(xfer(EMS_WRITE, a, at, buf, sizeof(buf)) == 0, "fill a");
    }
    memset(buf, 0xB5, sizeof(buf));
    for (at = 0; at < 32768UL; at += sizeof(buf)) MUST(xfer(EMS_WRITE, b, at, buf, sizeof(buf)) == 0, "fill b");
    /* straddle each 16K boundary with odd lengths, and rewrite across it */
    for (at = 16384UL - 1500; at < 16384UL + 1500; at += 333) {
        fill(buf, 2000, (unsigned)at);
        MUST(xfer(EMS_WRITE, a, at, buf, 2000) == 0, "straddle write");
        MUST(xfer(EMS_READ, a, at, back, 2000) == 0 && !memcmp(buf, back, 2000), "straddle read");
        MUST(xfer(EMS_WRITE, a, at + 16384UL, buf, 2000) == 0, "second boundary write");
        MUST(xfer(EMS_READ, a, at + 16384UL, back, 2000) == 0 && !memcmp(buf, back, 2000), "second boundary read");
    }
    MUST(xfer(EMS_READ, a, 0, back, sizeof(back)) == 0, "read a start");
    fill(buf, sizeof(buf), 0); MUST(!memcmp(buf, back, sizeof(buf)), "page 0 kept its data");
    MUST(xfer(EMS_READ, a, 45056UL, back, sizeof(back)) == 0, "read a end");
    fill(buf, sizeof(buf), 11); MUST(!memcmp(buf, back, sizeof(buf)), "last 4K kept its data");
    for (at = 0; at < 32768UL; at += sizeof(back)) {
        MUST(xfer(EMS_READ, b, at, back, sizeof(back)) == 0, "read b");
        for (i = 0; i < sizeof(back); i++) MUST((unsigned char)back[i] == 0xB5, "neighbour handle was overwritten");
    }
    puts("bank boundaries and bounds: ok");

    /* exhaustion and reuse */
    MUST(ioctl(fd, EMS_GETINFO, &info) == 0 && info.free == pages - 5, "free count");
    MUST(alloc(pages - 4) < 0 && errno == ENOMEM, "exhaustion");
    n = alloc(pages - 5); MUST((int)n > 0, "take the rest");
    MUST(alloc(1) < 0 && errno == ENOMEM, "nothing left");
    MUST(ioctl(fd, EMS_FREE, a) == 0 && ioctl(fd, EMS_FREE, a) < 0 && errno == EBADF, "double free");
    MUST(xfer(EMS_READ, a, 0, back, 1) < 0 && errno == EBADF, "freed handle is dead");
    MUST(alloc(4) < 0 && alloc(3) > 0, "hole is reusable, no larger");
    puts("exhaustion: ok");

    /* competing activity: another process computes and does file I/O */
    a = alloc(0); MUST(a < 0, "alloc 0 again");
    MUST(ioctl(fd, EMS_FREE, n) == 0, "free rest");
    a = alloc(4); MUST(a > 0, "alloc for stress");
    pid = fork();
    MUST(pid != (pid_t)-1, "fork competing I/O");
    if (!pid) {
        for (i = 0; i < 40; i++) {
            int f = open("/tmp/emstest.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (f >= 0) { write(f, buf, sizeof(buf)); close(f); }
            sync();
        }
        unlink("/tmp/emstest.tmp");
        _exit(0);
    }
    start = time(NULL);
    for (i = 0; i < 600; i++) {
        at = ((unsigned long)i * 5237UL) % (65536UL - 160);
        fill(buf, 160, i);
        MUST(xfer(EMS_WRITE, a, at, buf, 160) == 0, "stress write");
        MUST(xfer(EMS_READ, a, at, back, 160) == 0 && !memcmp(buf, back, 160), "stress compare");
    }
    printf("1200 row-sized copies beside a busy process: ok, %ld s\n", (long)(time(NULL) - start));
    wait(&status); MUST(WIFEXITED(status) && !WEXITSTATUS(status), "competing I/O exited");

    start = time(NULL);
    for (i = 0; i < 3000; i++)
        MUST(xfer(EMS_READ, a, ((unsigned long)i * 5237UL) % (65536UL - 160), back, 160) == 0, "timed read");
    printf("3000 uncontended 160-byte reads: %ld s\n", (long)(time(NULL) - start));

    /* cleanup: a client that exits without freeing leaves nothing behind */
    close(fd);
    pid = fork();
    MUST(pid != (pid_t)-1, "fork owner cleanup");
    if (!pid) {
        unsigned short all = pages;
        fd = open("/dev/ems", O_RDWR);
        if (fd < 0 || ioctl(fd, EMS_ALLOC, &all) < 0) _exit(1);
        kill(getpid(), SIGKILL);
        _exit(2);
    }
    wait(&status); MUST(WIFSIGNALED(status), "child was killed while owning every page");
    fd = open("/dev/ems", O_RDWR); MUST(fd >= 0, "reopen after owner died");
    MUST(ioctl(fd, EMS_GETINFO, &info) == 0 && info.free == pages, "pages returned after owner died");
    close(fd);
    puts("cleanup: ok");
    puts("EMS TEST PASSED");
    return 0;
}
