/*
 * Host test of the /dev/ems driver logic: cc -o t test_ems_host.c && ./t
 * The driver source is compiled unchanged against a simulated Lo-tech board
 * (write-only page registers, banked window, address aliasing) and a
 * simulated ROM area. It proves the logic, not real hardware timing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define EMS_HOST_TEST
#define INITPROC
#include "../../../elks/include/linuxmt/ems.h"

static unsigned char bank[EMS_MAX_PAGES][EMS_PAGE_SIZE];
static unsigned char plain[EMS_PAGE_SIZE];      /* a frame with no board behind it */
static unsigned sim_pages = 128, sim_page, sim_writes, sim_port_writes, sim_frame_present = 1;
static unsigned rom_seg, rom_size = 16, pm_seg, pm_port, pm_frame, sim_scramble, sim_log;
static unsigned char *data_segment;
extern unsigned int ems_port, ems_frame;
static char last_message[160];

static void hw_map(unsigned page) { sim_page = page & 255; ++sim_port_writes; }
static unsigned char *window(unsigned off)
{
    if (!sim_frame_present) return &plain[off];
    return &bank[sim_page % sim_pages][off];
}
static unsigned hw_peekw(unsigned off, unsigned seg)
{
    static const char id[] = "PMBIOS";
    if (seg == rom_seg && rom_seg && !off) return 0xAA55;
    if (seg == pm_seg && pm_seg) {
        if (!off) return 0xAA55;
        if (off >= 5 && off < 10) return (unsigned char)id[off - 5] | ((unsigned char)id[off - 4] << 8);
    }
    if (seg == ems_frame && off < EMS_PAGE_SIZE - 1)
        return window(off)[0] | (window(off)[1] << 8);
    return 0xFFFF;                                  /* open bus */
}
static unsigned hw_peekb(unsigned off, unsigned seg)
{
    if (off == 2 && seg == rom_seg && rom_seg) return rom_size;
    if (off == 2 && seg == pm_seg && pm_seg) return 32; /* 16K ROM */
    return hw_peekw(off, seg) & 0xFF;
}
static void hw_pokew(unsigned off, unsigned seg, unsigned v)
{
    if (seg != ems_frame) { fprintf(stderr, "write outside the frame: %x\n", seg); abort(); }
    window(off)[0] = (unsigned char)v; window(off)[1] = (unsigned char)(v >> 8); ++sim_writes;
}
static int hw_picomem_query(unsigned seg) { (void)seg; return pm_port == ems_port && pm_frame == ems_frame ? 1 : -1; }
static void scramble(void) { if (sim_scramble) sim_page = (unsigned)rand() & 255; }
static void hw_get(char *buf, unsigned off, unsigned n) { memcpy(buf, window(off), n); scramble(); }
static void hw_put(char *buf, unsigned off, unsigned n) { memcpy(window(off), buf, n); scramble(); }
static void printk(const char *format, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, format);
    vsnprintf(last_message, sizeof(last_message), format, args);
    __builtin_va_end(args);
    if (sim_log) fputs(last_message, stdout);
}

#include "../../../elks/arch/i86/drivers/char/ems.c"

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { printf("FAIL line %d: %s  [%s]\n", __LINE__, #c, last_message); exit(1); } } while (0)

static void boot(unsigned port, unsigned frame, unsigned limit)
{
    ems_release_all();
    ems_pages = 0; ems_busy = 0;
    ems_port = port; ems_frame = frame; ems_limit = limit;
    sim_port_writes = sim_writes = 0; last_message[0] = 0;
    ems_init();
}

int main(void)
{
    static char big[3 * EMS_PAGE_SIZE], back[3 * EMS_PAGE_SIZE];
    unsigned i, n;
    int h, a, b, c;

    /* Not requested: no port write, no memory write, no service. */
    boot(0, 0xE000, 0);
    CHECK(!ems_pages && !sim_port_writes && !sim_writes);

    /* Configuration that must be refused before any hardware is touched. */
    boot(0x3F8, 0xE000, 0); CHECK(!ems_pages && !sim_port_writes && strstr(last_message, "unsupported port"));
    boot(0x268, 0xB800, 0); CHECK(!ems_pages && !sim_port_writes && !sim_writes);   /* video */
    boot(0x268, 0x9800, 0); CHECK(!ems_pages && !sim_port_writes);                   /* Tandy video RAM */
    boot(0x268, 0xF000, 0); CHECK(!ems_pages && !sim_port_writes);                   /* system ROM */
    boot(0x268, 0xE400, 0); CHECK(!ems_pages && !sim_port_writes);                   /* would reach F000 */
    boot(0x268, 0xD100, 0); CHECK(!ems_pages && !sim_port_writes);                   /* not on 16K */
    rom_seg = 0xD800; boot(0x268, 0xD000, 0); rom_seg = 0;
    CHECK(!ems_pages && !sim_port_writes && !sim_writes && strstr(last_message, "option ROM"));
    /* The ROM header need not be inside the frame for its body to overlap. */
    rom_seg = 0xC800; rom_size = 65; boot(0x268, 0xD000, 0);
    CHECK(!ems_pages && !sim_port_writes && !sim_writes);
    rom_size = 64; boot(0x268, 0xD000, 0); CHECK(ems_pages == 128); /* adjacent */
    rom_seg = 0xE000; boot(0x268, 0xD000, 0); CHECK(ems_pages == 128); /* after */
    rom_seg = 0xC000; rom_size = 255; boot(0x268, 0xD000, 0);
    CHECK(!ems_pages && !sim_port_writes && !sim_writes);
    rom_size = 0; boot(0x268, 0xD000, 0); /* malformed size must fail closed */
    CHECK(!ems_pages && !sim_port_writes && !sim_writes);
    rom_seg = 0; rom_size = 16;
    ems_port = 0x268; ems_frame = 0xD000; ems_exclude(0xD400, 0x800);                /* umb= inside frame */
    CHECK(!ems_port && strstr(last_message, "umb"));
    ems_port = 0x268; ems_frame = 0xD000; ems_exclude(0xC000, 0x1000); CHECK(ems_port);   /* adjacent is fine */
    ems_exclude(0xE000, 0x1000); CHECK(ems_port);

    /* PicoMEM BIOS present: its record must match /bootopts. */
    pm_seg = 0xC800; pm_port = 0x268; pm_frame = 0xD000;
    boot(0x268, 0xE000, 0); CHECK(!ems_pages && !sim_port_writes && strstr(last_message, "PicoMEM"));
    boot(0x288, 0xD000, 0); CHECK(!ems_pages && !sim_port_writes);
    boot(0x268, 0xD000, 0); CHECK(ems_pages == 128);
    pm_seg = 0;

    /* User data pointers: include all possible 16-bit starting offsets and
     * both ioctl-structure sizes and copies crossing the segment boundary. */
    for (i = 0; i <= 65535U; i++) {
        CHECK(ems_range_fits(i, 0));
        CHECK(ems_range_fits(i, 2) == (i <= 65533U));
        CHECK(ems_range_fits(i, 8) == (i <= 65527U));
        CHECK(ems_range_fits(i, 10) == (i <= 65525U));
        CHECK(ems_range_fits(i, 4096) == (i <= 61439U));
    }
    CHECK(!ems_range_fits(65535U, 1));
    CHECK(ems_range_fits(0, 65535U));
    CHECK(!ems_range_fits(1, 65535U));

    /* A frame that is ordinary RAM or empty must be refused and left as found. */
    sim_frame_present = 0; memset(plain, 0x77, sizeof(plain));
    boot(0x268, 0xE000, 0);
    CHECK(!ems_pages && strstr(last_message, "not bank-switched"));
    for (i = 0; i < sizeof(plain); i++) CHECK(plain[i] == 0x77);
    sim_frame_present = 1;

    /* Capacity: aliasing boards report only distinct pages. */
    for (n = 2; n <= 256; n *= 2) {
        sim_pages = n; boot(0x260, 0xE000, 0); CHECK(ems_pages == n);
    }
    sim_pages = 256; boot(0x268, 0xE000, 40); CHECK(ems_pages == 40);      /* pages= cap */
    sim_pages = 1; boot(0x268, 0xE000, 0); CHECK(!ems_pages);
    /* The window test puts back what it displaced. */
    sim_pages = 128; memset(bank, 0x5A, sizeof(bank));
    ems_release_all(); ems_pages = 0; ems_port = 0x268; ems_frame = 0xE000;
    CHECK(window_switches());
    for (i = 0; i < 128; i++) CHECK(bank[i][0] == 0x5A && bank[i][1] == 0x5A);

    sim_pages = 128; boot(0x268, 0xE000, 0); CHECK(ems_pages == 128);
    sim_log = 1; boot(0x268, 0xE000, 0); sim_log = 0;

    /* Allocation, exhaustion and invalid handles. */
    CHECK(ems_alloc(0) == -EINVAL);
    CHECK(ems_alloc(129) == -ENOMEM);
    a = ems_alloc(100); CHECK(a > 0 && ems_free_pages() == 28);
    CHECK(ems_alloc(29) == -ENOMEM);
    b = ems_alloc(28); CHECK(b > 0 && b != a && !ems_free_pages());
    CHECK(ems_alloc(1) == -ENOMEM);
    CHECK(ems_free(0) == -EBADF && ems_free(25) == -EBADF && ems_free(0xFFFF) == -EBADF);
    CHECK(ems_free(a) == 0 && ems_free(a) == -EBADF);          /* double free */
    CHECK(ems_free_pages() == 100);
    c = ems_alloc(60); CHECK(c > 0);                            /* first fit reuses the hole */
    h = ems_alloc(40); CHECK(h > 0 && !ems_free_pages());
    CHECK(ems_free(b) == 0 && ems_free(c) == 0 && ems_free(h) == 0 && ems_free_pages() == 128);
    for (i = 0; i < EMS_MAX_HANDLES; i++) CHECK(ems_alloc(1) == (int)i + 1);
    CHECK(ems_alloc(1) == -ENFILE);                             /* handle table full */
    ems_release_all(); CHECK(ems_free_pages() == 128);
    CHECK(ems_transfer(1, 0, big, 1, 0) == -EBADF);             /* released handle is dead */

    /* Transfers: bounds first, then data across page boundaries. */
    a = ems_alloc(3); b = ems_alloc(2);
    for (i = 0; i < sizeof(big); i++) big[i] = (char)(i * 7 + (i >> 8));
    n = sim_port_writes;
    CHECK(ems_transfer(a, 3UL * EMS_PAGE_SIZE, big, 1, 1) == -EINVAL);
    CHECK(ems_transfer(a, 3UL * EMS_PAGE_SIZE - 1, big, 2, 1) == -EINVAL);
    CHECK(ems_transfer(a, 0xFFFFFFFFUL, big, 2, 1) == -EINVAL);            /* offset+count wraps */
    CHECK(ems_transfer(a, 0x10000UL * 0x10000UL - 1, big, 65535U, 0) == -EINVAL);
    CHECK(ems_transfer(b, 2UL * EMS_PAGE_SIZE, big, 1, 0) == -EINVAL);
    CHECK(ems_transfer(0, 0, big, 1, 1) == -EBADF && ems_transfer(99, 0, big, 1, 1) == -EBADF);
    CHECK(sim_port_writes == n);                                 /* rejected calls never map */
    CHECK(ems_transfer(a, 3UL * EMS_PAGE_SIZE, big, 0, 1) == 0); /* empty copy at the end is legal */
    memset(bank, 0, sizeof(bank));
    CHECK(ems_transfer(a, 0, big, 49152U, 1) == 0);
    CHECK(ems_transfer(a, 0, back, 49152U, 0) == 0 && !memcmp(big, back, 49152U));
    CHECK(!memcmp(bank[0], big, EMS_PAGE_SIZE) && !memcmp(bank[2], big + 2 * EMS_PAGE_SIZE, EMS_PAGE_SIZE));
    for (i = 0; i < EMS_PAGE_SIZE; i++) CHECK(bank[3][i] == 0);  /* b's pages untouched */
    sim_scramble = 1;       /* something else moves the page register between pieces */
    for (i = 0; i < 4000; i++) {
        unsigned long at = (unsigned long)(rand() % (3 * EMS_PAGE_SIZE));
        unsigned len = (unsigned)rand() % 400;
        if (at + len > 3UL * EMS_PAGE_SIZE) len = (unsigned)(3UL * EMS_PAGE_SIZE - at);
        if (rand() & 1) {
            memset(big + at, rand(), len);
            CHECK(ems_transfer(a, at, big + at, len, 1) == 0);
        } else {
            CHECK(ems_transfer(a, at, back, len, 0) == 0 && !memcmp(back, big + at, len));
        }
    }
    sim_scramble = 0;
    CHECK(ems_transfer(a, 0, back, 49152U, 0) == 0 && !memcmp(big, back, 49152U));
    /* Exactly the last byte of one page and the first of the next. */
    back[0] = 0x11; back[1] = 0x22;
    CHECK(ems_transfer(b, EMS_PAGE_SIZE - 1, back, 2, 1) == 0);
    CHECK(bank[3][EMS_PAGE_SIZE - 1] == 0x11 && bank[4][0] == 0x22);
    printf("ems host test: %u checks passed\n", checks);
    (void)data_segment;
    return 0;
}
