/* SPDX-License-Identifier: MPL-1.1
 * ELKS direct VGA mode 12h, with console and font preservation.
 */
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <arch/io.h>
#include <arch/segment.h>
#include <linuxmt/mem.h>
#include <linuxmt/ntty.h>
#include <linuxmt/segment.h>
#include "vga_pm.h"

#define SEQ 0x3c4
#define GC 0x3ce
#define CRT 0x3d4
#define ATTR 0x3c0
#define STATUS 0x3da
#define FONT_SIZE 8192U
#define TEXT_SIZE 32768U
/* This test kernel implements a selector query in addition to MEM_GETDS. */
#define MEM_GET_VGA_SELECTOR 0x5600

struct regs {
    unsigned char misc, seq[5], crt[25], gc[9], attr[21];
};
static const struct regs mode12 = {
    0xe3,
    {3,1,0x0f,0,6},
    {0x5f,0x4f,0x50,0x82,0x54,0x80,0x0b,0x3e,0,0x40,0,0,0,0,0,0,
      0xea,0x8c,0xdf,0x28,0,0xe7,4,0xe3,0xff},
    {0,0,0,0,0,0,5,0x0f,0xff},
    {0,1,2,3,4,5,0x14,7,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
      1,0,0x0f,0,0}
};
static struct regs saved;
static unsigned char seq_index, gc_index, crt_index, attr_index;
static unsigned char __far *backup;
static int console_fd = -1;
unsigned vga_pm_graphics_seg;
void (*vga_pm_checkpoint)(unsigned);

static unsigned char read_reg(unsigned port, unsigned i)
{
    outb(i, port);
    return inb(port + 1);
}
static void reg(unsigned port, unsigned i, unsigned char value)
{
    outb(i, port);
    outb(value, port + 1);
}
/* PAS is bit 5 of the attribute address. Reset the AC flip-flop first. */
static void ac_index(unsigned i)
{
    (void)inb(STATUS);
    outb(i, ATTR);
    (void)inb(STATUS);
}
static void indexes(void)
{
    outb(seq_index, SEQ);
    outb(gc_index, GC);
    outb(crt_index, CRT);
    ac_index((attr_index & 0x1f) | 0x20);
}
/* Keep the sequencer blanked while the CPU owns the attribute palette. */
static void blank(void)
{
    reg(SEQ, 1, read_reg(SEQ, 1) | 0x20);
    ac_index(0);
}

static void show(const struct regs *r)
{
    ac_index(0x20);
    reg(SEQ, 1, r->seq[1] & ~0x20);
}

static void write_regs(const struct regs *r)
{
    unsigned i;
    reg(SEQ, 0, 1);
    outb(r->misc, 0x3c2);
    for (i = 1; i < 5; ++i)
        reg(SEQ, i, r->seq[i] | (i == 1 ? 0x20 : 0));
    reg(SEQ, 0, 3);
    reg(CRT, 0x11, r->crt[0x11] & 0x7f);
    for (i = 0; i < 25; ++i)
        if (i != 0x11)
            reg(CRT, i, r->crt[i]);
    reg(CRT, 0x11, r->crt[0x11]);
    for (i = 0; i < 9; ++i)
        reg(GC, i, r->gc[i]);
    for (i = 0; i < 21; ++i) {
        (void)inb(STATUS);
        outb(i, ATTR);
        outb(r->attr[i], ATTR);
    }
}
/* Linear A000, plane 2 only, read mode 0/write mode 0, no odd/even. */
static void map_font(void)
{
    reg(SEQ, 0, 1);
    reg(SEQ, 2, 4);
    reg(SEQ, 4, 7);
    reg(SEQ, 0, 3);
    reg(GC, 1, 0);
    reg(GC, 3, 0);
    reg(GC, 4, 2);
    reg(GC, 5, 0);
    reg(GC, 6, 4);
    reg(GC, 8, 0xff);
}

static int graphics_selector(void)
{
    unsigned ds = 0, seg = 0;
    int fd = open("/dev/kmem", O_RDONLY);
    int ok;
    if (fd < 0)
        return -1;
    ok = !ioctl(fd, MEM_GETDS, &ds) && ds == SEL_KDATA;
    if (ok && (ioctl(fd, MEM_GET_VGA_SELECTOR, &seg) || seg != 0xa000)) {
#ifdef SEL_GRAPHICS
        /* ELKS with the upstream fixed A000 memory mapping (#2869).
         * Do not equate a protected-mode selector with a real-mode segment. */
        seg = SEL_GRAPHICS;
#else
        ok = 0; /* older headers: require the explicit legacy A000 query */
#endif
    }
    close(fd);
    if (!ok)
        return -1;
    vga_pm_graphics_seg = seg;
    return 0;
}
static void release_console(void)
{
    ioctl(console_fd, DCREL_GRAPH, 0);
    close(console_fd);
    console_fd = -1;
}

int vga_pm_enter(const char *tty)
{
    unsigned i;
    volatile unsigned char __far *font;
    volatile unsigned char __far *text;
    if (console_fd >= 0 || graphics_selector())
        return -1;
    console_fd = open(tty, O_RDWR);
    if (console_fd < 0)
        return -1;
    if (ioctl(console_fd, DCGET_GRAPH, 0) < 0) {
        close(console_fd);
        console_fd = -1;
        return -1;
    }
    saved.misc = inb(0x3cc);
    if (!(saved.misc & 1)) {
        release_console();
        return -1;
    }
    seq_index = inb(SEQ);
    gc_index = inb(GC);
    crt_index = inb(CRT);
    attr_index = inb(ATTR);
    for (i = 0; i < 5; ++i)
        saved.seq[i] = read_reg(SEQ, i);
    for (i = 0; i < 25; ++i)
        saved.crt[i] = read_reg(CRT, i);
    for (i = 0; i < 9; ++i)
        saved.gc[i] = read_reg(GC, i);

    /* Deliberately accept only colour 80-column 16-scanline text with
     * normal odd/even mapping and both character-map selects at zero. */
    if ((saved.seq[0] & 3) != 3 || (saved.seq[1] & 0x20) ||
        saved.seq[3] != 0 || (saved.seq[4] & 0x0c) ||
        (saved.crt[9] & 0x9f) != 0x0f || saved.crt[1] != 79 ||
        (saved.gc[6] & 0x0d) != 0x0c) {
        indexes();
        release_console();
        return -1;
    }
    blank();
    for (i = 0; i < 21; ++i) {
        /* IBM Type 1 VGA blocks palette access with PAS set. QEMU
         * permits it; saving with PAS set can restore an all-black palette. */
        ac_index(i);
        saved.attr[i] = inb(0x3c1);
    }
    show(&saved);
    if (saved.attr[0x10] & 1) {
        indexes();
        release_console();
        return -1;
    }
    backup = fmemalloc((unsigned long)FONT_SIZE + TEXT_SIZE);
    if (!backup) {
        indexes();
        release_console();
        return -1;
    }

    text = _MK_FP(SEL_VIDEO, 0);
    font = _MK_FP(vga_pm_graphics_seg, 0);
    blank();
    for (i = 0; i < TEXT_SIZE; ++i)
        backup[i] = text[i];
    map_font();
    for (i = 0; i < FONT_SIZE; ++i)
        backup[TEXT_SIZE + i] = font[i];
    write_regs(&mode12);
    for (i = 0; i < 38400U; ++i)
        font[i] = 0; /* Clears all four enabled planes, including plane 2. */
    show(&mode12);
    return 0;
}

int vga_pm_leave(void)
{
    unsigned i;
    int mismatch = 0;
    volatile unsigned char __far *font;
    volatile unsigned char __far *text;
    if (console_fd < 0)
        return -1;
    font = _MK_FP(vga_pm_graphics_seg, 0);
    text = _MK_FP(SEL_VIDEO, 0);
    blank();
    map_font();
    for (i = 0; i < FONT_SIZE; ++i)
        font[i] = backup[TEXT_SIZE + i];
    for (i = 0; i < FONT_SIZE; ++i)
        if (font[i] != backup[TEXT_SIZE + i])
            mismatch = 1;
    if (vga_pm_checkpoint) vga_pm_checkpoint(2);
    write_regs(&saved);
    for (i = 0; i < TEXT_SIZE; ++i)
        text[i] = backup[i];
    for (i = 0; i < TEXT_SIZE; ++i)
        if (text[i] != backup[i])
            mismatch = 1;
    fmemfree(backup);
    backup = 0;
    show(&saved);
    indexes();
    release_console();
    if (vga_pm_checkpoint) vga_pm_checkpoint(3);
    return mismatch ? -1 : 0;
}
