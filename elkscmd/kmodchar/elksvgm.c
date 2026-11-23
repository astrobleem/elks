/* One-shot VGM player for an idle PC-9801-26/K or Tandy EX PSG.
 * All commands are checked before I/O. No IRQ/timer, prescaler or SSG/GPIO
 * writes, loop replay, compressed inputs, or other chips.
 */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <unistd.h>

#define FM_ADDR 0x188
#define FM_DATA 0x18a
#define MAX_FILE 8192U
#define MAX_SAMPLES (120UL * 44100UL)

static unsigned char image[MAX_FILE + 1];
static volatile unsigned char stopped;
static unsigned char touched, psg;
static struct timeval start;
static unsigned int loop_pos;

static void on_signal(int sig)
{
    (void)sig;
    stopped = 1;
}

static unsigned long le32(unsigned int p)
{
    return (unsigned long)image[p] |
           ((unsigned long)image[p+1] << 8) |
           ((unsigned long)image[p+2] << 16) |
           ((unsigned long)image[p+3] << 24);
}

static int ready(void)
{
    unsigned char s;
    unsigned int i = 4096;
    do {
        asm volatile ("inb %w1,%0" : "=a" (s) : "d" (FM_ADDR));
        if (s == 0xff)
            return -1;
        if (!(s & 0x80))
            return 0;
    } while (--i);
    return -1;
}

static int put(unsigned char r, unsigned char v)
{
    if (ready())
        return -1;
    asm volatile ("outb %0,%w1" :: "a" (r), "d" (FM_ADDR));
    if (ready())
        return -1;
    asm volatile ("outb %0,%w1" :: "a" (v), "d" (FM_DATA));
    return 0;
}

static int real_mode(void)
{
    unsigned int f;
    asm volatile ("pushf; popw %0" : "=a" (f));
    if ((f & 0xf000) == 0xf000)
        return 1;                 /* 8086/186 flags, no SMSW instruction */
    asm volatile (".byte 0x0f,0x01,0xe0" : "=a" (f)); /* SMSW AX on 286+ */
    return !(f & 1);
}

static int tandy_rom(void)
{
    const unsigned char __far *rom = (const unsigned char __far *)0xf0000000UL;
    /* Family fingerprint; the explicit switch still requires a known EX. */
    return rom[0xc000U] == 0x21 && rom[0xfffeU] == 0xff;
}

static void psg_write(unsigned char v)
{
    /* SN76496 boards need time between back-to-back OUTs. */
    asm volatile ("outb %0,$0xc0; nop; nop; nop; nop; nop; nop;"
                  "nop; nop; nop; nop; nop; nop" :: "a" (v));
}

static int fm_reg(unsigned char r, unsigned char v)
{
    if (r == 0x28)
        return !(v & 0x0c) && (v & 3) != 3;
    if ((r & 3) == 3)
        return 0;
    return (r >= 0x30 && r <= 0x9e) ||
           (r >= 0xa0 && r <= 0xa6) ||
           (r >= 0xb0 && r <= 0xb2);
}

/* Absolute schedule: round to the first available ELKS tick, not per wait.
 * Chunking also keeps surplus-sleep and signal response bounded.
 */
static int until(unsigned long samples)
{
    struct timeval now;
    long secs, elapsed, deadline;
    deadline = (long)((samples / 441) * 10000UL +
                      ((samples % 441) * 10000UL + 440UL) / 441);
    while (!stopped) {
        if (gettimeofday(&now, 0))
            return -1;
        secs = now.tv_sec - start.tv_sec;
        if (secs < 0 || secs > 122)
            return -1;
        elapsed = secs * 1000000L + now.tv_usec - start.tv_usec;
        if (elapsed < 0)
            return -1;
        if (elapsed >= deadline)
            return 0;
        if (deadline - elapsed > 100000L)
            usleep(100000);
        else
            usleep(deadline - elapsed);
    }
    return -1;
}

static int walk(unsigned int p, unsigned int end, int play)
{
    unsigned long samples = 0;
    unsigned int wait;
    unsigned char cmd, r, v;
    int writes = 0, latched = -1;
    int loop_seen = !loop_pos;

    while (p < end && !stopped) {
        if (p == loop_pos)
            loop_seen = 1;
        cmd = image[p++];
        if (cmd == 0x66)
            return (p == end && writes && samples && loop_seen) ? 0 : -1;
        if (cmd == 0x50 && psg) {
            if (p >= end)
                return -1;
            v = image[p++];
            if (v & 0x80)
                latched = (v >> 4) & 7;
            else if ((v & 0x40) || latched < 0 ||
                     (latched & 1) || latched == 6)
                return -1;
            ++writes;
            if (play) {
                if (until(samples))
                    return -1;
                touched = 1;
                psg_write(v);
            }
            continue;
        }
        if (cmd == 0x55 && !psg) {
            if (end - p < 2)
                return -1;
            r = image[p++];
            v = image[p++];
            if (!fm_reg(r, v))
                return -1;
            ++writes;
            if (play) {
                touched |= 1 << (r == 0x28 ? (v & 3) : (r & 3));
                if (until(samples) || put(r, v))
                    return -1;
            }
            continue;
        }
        if (cmd == 0x61) {
            if (end - p < 2)
                return -1;
            wait = image[p] | ((unsigned int)image[p+1] << 8);
            p += 2;
        } else if (cmd == 0x62)
            wait = 735;
        else if (cmd == 0x63)
            wait = 882;
        else if (cmd >= 0x70 && cmd <= 0x7f)
            wait = (cmd & 15) + 1;
        else
            return -1;
        if (wait > MAX_SAMPLES - samples)
            return -1;
        samples += wait;
        if (play && until(samples))
            return -1;
    }
    return -1;
}

static int cleanup(void)
{
    unsigned int ch, op;
    int error = 0;
    if (psg) {
        if (touched)
            for (ch = 0; ch < 4; ++ch)
                psg_write(0x9f + (ch << 5));
        return 0;
    }
    for (ch = 0; ch < 3; ++ch)
        if (touched & (1 << ch)) {
            if (put(0x28, ch))
                error = 1;
            for (op = 0; op < 4; ++op)
                if (put(0x40 + 4 * op + ch, 0x7f))
                    error = 1;
        }
    return error;
}

int main(int argc, char **argv)
{
    unsigned int n = 0, first, end;
    unsigned long version, offset, gd3, loop;
    struct utsname os;
    int fd, got, result;

    if (argc == 3 && !strcmp(argv[1], "--tandy-ex"))
        psg = 1;
    else if (argc != 3 || strcmp(argv[1], "--pc98-26k")) {
        fputs("Usage: elksvgm --pc98-26k|--tandy-ex FILE.VGM (idle confirmed hardware)\n",
              stderr);
        return 2;
    }
    fd = open(argv[2], O_RDONLY);
    if (fd < 0) {
        perror(argv[2]);
        return 1;
    }
    do {
        got = read(fd, image + n, sizeof(image) - n);
        if (got > 0)
            n += got;
    } while (got > 0 && n <= MAX_FILE);
    close(fd);
    if (got || n < (psg ? 0x41 : 0x49) || memcmp(image, "Vgm ", 4) || le32(4) != n - 4)
        goto invalid;
    version = le32(8);
    offset = le32(0x34);
    gd3 = le32(0x14);
    loop = le32(0x1c);
    if (version < (psg ? 0x150 : 0x151) || version > 0x171 ||
        offset < (psg ? 0x0c : 0x14) || offset >= n - 0x34)
        goto invalid;
    if (psg) {
        if (le32(0x0c) != 3579545UL || image[0x28] != 6 ||
            image[0x29] || image[0x2a] != 15 || image[0x2b])
            goto invalid;
    } else if (le32(0x44) != 3993600UL)
        goto invalid;
    first = (unsigned int)offset + 0x34;
    end = n;
    if (gd3) {
        if (gd3 < first - 0x14 || gd3 > n - 0x20)
            goto invalid;
        end = (unsigned int)gd3 + 0x14;
        if (memcmp(image + end, "Gd3 ", 4) || le32(end + 8) != n - end - 12)
            goto invalid;
    }
    if (loop) {
        if (loop < first - 0x1c || loop >= end - 0x1c)
            goto invalid;
        loop_pos = (unsigned int)loop + 0x1c;
    }
    if (walk(first, end, 0))
        goto invalid;

    if (signal(SIGINT, on_signal) == SIG_ERR ||
        signal(SIGTERM, on_signal) == SIG_ERR)
        return 1;
    if (uname(&os) ||
        strcmp(os.machine, psg ? "ibmpc i8086" : "pc-98 i8086") ||
        !real_mode()) {
        fputs("Wrong machine or not a real-mode ELKS kernel; no I/O writes\n",
              stderr);
        return 1;
    }
    if (psg && !tandy_rom()) {
        fputs("Tandy BIOS signature missing; no I/O writes\n", stderr);
        return 1;
    }
    if (stopped || (!psg && ready())) {
        fputs("YM2203 not ready; no writes\n", stderr);
        return 1;
    }
    if (gettimeofday(&start, 0))
        return 1;
    result = walk(first, end, 1);
    if (cleanup()) {
        fputs("YM2203 cleanup did not complete\n", stderr);
        return 1;
    }
    if (result) {
        fputs("Playback did not complete; channels muted\n", stderr);
        return 1;
    }
    puts("Playback complete; channels muted");
    return 0;
invalid:
    fputs("Unsupported VGM for this target (raw, one chip, max 8KiB)\n",
          stderr);
    return 2;
}
