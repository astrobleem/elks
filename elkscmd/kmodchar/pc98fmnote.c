/* Standalone PC-9801-26/K diagnostic, not a driver.
 * Run only on a confirmed idle YM2203 at the standard port pair.
 * Channel 1 is deliberately left silent, not restored to a previous voice.
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FM_ADDR 0x188
#define FM_DATA 0x18a

static volatile unsigned char stop;

static void interrupted(int sig)
{
    (void)sig;
    stop = 1;
}

static int ready(void)
{
    unsigned char s;
    unsigned int n = 4096;

    do {
        asm volatile ("inb %w1,%0" : "=a" (s) : "d" (FM_ADDR));
        if (s == 0xff)
            return -1;
        if (!(s & 0x80))
            return 0;
    } while (--n);
    return -1;
}

static int reg(unsigned char r, unsigned char v)
{
    if (ready())
        return -1;
    asm volatile ("outb %0,%w1" :: "a" (r), "d" (FM_ADDR));
    if (ready())
        return -1;
    asm volatile ("outb %0,%w1" :: "a" (v), "d" (FM_DATA));
    return 0;
}

int main(int argc, char **argv)
{
    static const unsigned char voice[][2] = {
        {0x28, 0x00}, /* key off channel 1 before programming */
        {0x44, 0x7f}, {0x48, 0x7f}, {0x4c, 0x7f},
        {0xb0, 0x07}, /* algorithm 7; unplayed operators stay muted */
        {0x30, 0x01}, {0x40, 0x25}, {0x50, 0x1f},
        {0x60, 0x0e}, {0x70, 0x0e}, {0x80, 0x4f},
        {0x90, 0x00},
        {0xa4, 0x24}, {0xa0, 0x70}
    };
    unsigned int i, error;
    int off_error, mute_error;

    if (argc != 2 || strcmp(argv[1], "--pc98-26k")) {
        fputs("Usage: pc98fmnote --pc98-26k (idle confirmed board only)\n",
              stderr);
        return 2;
    }
    if (signal(SIGINT, interrupted) == SIG_ERR ||
        signal(SIGTERM, interrupted) == SIG_ERR)
        return 1;
    if (ready()) {
        fputs("YM2203 absent or busy; no writes\n", stderr);
        return 1;
    }
    for (i = 0; i < sizeof(voice)/sizeof(voice[0]) && !stop; ++i)
        if (reg(voice[i][0], voice[i][1]))
            break;
    error = stop || i != sizeof(voice)/sizeof(voice[0]);
    if (!error) {
        error = reg(0x28, 0x10) != 0;
        if (!error && !stop)
            usleep(150000);
    }
    off_error = reg(0x28, 0);
    mute_error = reg(0x40, 0x7f);
    if (off_error || mute_error) {
        fputs("YM2203 cleanup did not complete\n", stderr);
        return 1;
    }
    if (error || stop) {
        fputs("YM2203 note interrupted or timed out; channel muted\n", stderr);
        return 1;
    }
    puts("YM2203 note finished; channel muted");
    return 0;
}
