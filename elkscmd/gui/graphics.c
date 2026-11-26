/*
 * VGA Graphics for ELKS - C86, OWC and IA16 compilers
 *  Supports 16-color VGA and 256 color palette modes
 *
 * 1 Feb 2025 Greg Haerr C86 support
 * 13 Mar 2025 Added OWC ASM routines from Rafael Diniz
 * 16 Mar 2025 Added IA16
 */

#include <stdio.h>
#include <stdlib.h>
#include "graphics.h"
#include "vgalib.h"
#ifdef __ia16__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linuxmt/mem.h>
#include <linuxmt/ntty.h>
#include <malloc.h>
#include <linuxmt/segment.h>
#include <arch/segment.h>
#include "vga_pm.h"
unsigned paint_video_seg = EGA_BASE;
static int pm_vga;
static int rm_console = -1;
static unsigned char __far *rm_text;
#define RM_TEXT_BYTES 32768U

static int save_bios_text(const char *tty)
{
    unsigned i;
    const unsigned char __far *bda = MK_FP(0x40, 0);
    const volatile unsigned char __far *text = MK_FP(0xb800, 0);
    if (bda[0x49] != TEXT_MODE || bda[0x4a] != 80 || bda[0x4b] != 0)
        return -1;
    rm_console = open(tty ? tty : "/dev/tty1", O_RDWR);
    if (rm_console < 0) return -1;
    if (ioctl(rm_console, DCGET_GRAPH, 0) == 0) {
        rm_text = fmemalloc(RM_TEXT_BYTES);
        if (rm_text) {
            for (i = 0; i < RM_TEXT_BYTES; ++i) rm_text[i] = text[i];
            return 0;
        }
        ioctl(rm_console, DCREL_GRAPH, 0);
    }
    close(rm_console);
    rm_console = -1;
    return -1;
}

static void restore_bios_text(void)
{
    unsigned i;
    volatile unsigned char __far *text = MK_FP(0xb800, 0);
    for (i = 0; i < RM_TEXT_BYTES; ++i) text[i] = rm_text[i];
    fmemfree(rm_text);
    rm_text = 0;
    ioctl(rm_console, DCREL_GRAPH, 0);
    close(rm_console);
    rm_console = -1;
}
#endif

int SCREENWIDTH;                /* initialized by graphics_open */
int SCREENHEIGHT;

int graphics_open(int mode)
{
#ifdef __ia16__
    unsigned ds;
    int fd = open("/dev/kmem", O_RDONLY);
    const char *tty;
    if (fd < 0 || ioctl(fd, MEM_GETDS, &ds) != 0) {
        if (fd >= 0) close(fd);
        fputs("Paint: cannot identify kernel video environment.\n", stderr);
        return -1;
    }
    close(fd);
    tty = ttyname(0);
    if (ds == SEL_KDATA) {
        /* EGA cannot use the VGA register snapshot path; never try the
         * real-mode BIOS fallback while running in protected mode. */
        if (mode != VGA_640x480x16 || vga_pm_enter(tty ? tty : "/dev/tty1")) {
            fputs("Paint: protected mode requires supported 640x480 VGA (no EGAMODE).\n", stderr);
            return -1;
        }
        paint_video_seg = vga_pm_graphics_seg;
        pm_vga = 1;
        SCREENWIDTH = 640;
        SCREENHEIGHT = 480;
        vga_init();
        return 0;
    }
    if ((mode == VGA_640x350x16 || mode == VGA_640x480x16 ||
         mode == PAL_320x200x256) && save_bios_text(tty)) {
        fputs("Paint: could not save the 80-column color console.\n", stderr);
        return -1;
    }
#endif
    switch (mode) {
    case VGA_640x350x16:
        SCREENWIDTH = 640;
        SCREENHEIGHT = 350;
        set_bios_mode(mode);
        vga_init();
        break;
    case VGA_640x480x16:
        SCREENWIDTH = 640;
        SCREENHEIGHT = 480;
        set_bios_mode(mode);
        vga_init();
        break;
    case PAL_320x200x256:
        SCREENWIDTH = 320;
        SCREENHEIGHT = 200;
        set_bios_mode(mode);
        break;
    default:
        printf("Unsupported mode: %02x\n", mode);
        return -1;
    }
    return 0;
}

int graphics_close(void)
{
#ifdef __ia16__
    if (pm_vga) {
        pm_vga = 0;
        paint_video_seg = EGA_BASE;
        return vga_pm_leave();
    }
#endif
    /* EGA BIOS reloads the standard text font. Its registers cannot all
     * be read back like VGA's, so keep this on the real-mode BIOS path. */
    set_bios_mode(TEXT_MODE);
#ifdef __ia16__
    if (rm_console >= 0) restore_bios_text();
#endif
    return 0;
}

#ifdef __WATCOMC__
static unsigned char mask[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

void vga_init(void)
{
    set_enable_sr(0xff);
    set_op(0);
    set_write_mode(0);
}

void drawpixel(int x, int y, int color)
{
    set_color(color);
    set_mask(mask[x & 7]);
    asm_orbyte((y << 6) + (y << 4) + (x >> 3)); /* y * 80 + x / 8 */
}

int readpixel(int x, int y)
{
    int c = 0;
    int offset = (y<<6) + (y<<4) + (x >> 3);    /* y * 80  + x / 8*/
    for (int plane=0; plane<4; plane++) {
        set_read_plane(plane);
        if (asm_getbyte(offset) & mask[x&7])
            c |= 1 << plane;
    }
    return c;
}

// Draw a horizontal line from x1,1 to x2,y including final point
void drawhline(int x1, int x2, int y, int c)
{
    set_color(c);
    unsigned int dst = (y<<6) + (y<<4) + (x1>>3);  /* y * 80 + x / 8 */
    if ((x1 >> 3) == (x2 >> 3)) {
        set_mask((0xff >> (x1 & 7)) & (0xff << (7 - (x2 & 7))));
        asm_orbyte(dst);
    } else {
        set_mask(0xff >> (x1 & 7));
        asm_orbyte(dst); dst++;
        set_mask(0xff);
        unsigned int last = (y<<6) + (y<<4) + (x2>>3);
        while (dst < last) {
            asm_orbyte(dst); dst++;
        }
        set_mask(0xff << (7 - (x2 & 7)));
        asm_orbyte(dst);
    }
}

void drawvline(int x, int y1, int y2, int c)
{
    while (y1 <= y2)
        drawpixel(x, y1++, c);
}
#endif /* __WATCOMC__ */

#if defined(__WATCOMC__) || defined(__ia16__)
/* draw 8 bits horizontally using XOR and one or two memory writes w/clipping */
static void drawbits(int x, int y, unsigned char bits)
{
    set_op(0x18);
    set_color(15);
    unsigned int dst = (y<<6) + (y<<4) + (x>>3);  /* y * 80 + x / 8 */
    if (x < SCREENWIDTH) {
        set_mask(bits >> (x & 7));
        asm_orbyte(dst);
    }
    if (x < SCREENWIDTH-8) {
        set_mask(bits << (8 - (x & 7)));
        asm_orbyte(dst+1);
    }
    set_op(0);
}

/* fast cursor draw for EGA/VGA hardware */
void vga_drawcursor(int x, int y, int height, unsigned short *mask)
{
    for (int i=0; i<height; i++) {
        unsigned int bits = *mask++;
        if (bits >> 8)
            drawbits(x, y, bits >> 8);
        if (bits & 0xff)
            drawbits(x+8, y, bits);
        y++;
    }
}
#endif

void fillrect(int x1, int y1, int x2, int y2, int c)
{
    while(y1 <= y2)
        drawhline(x1, x2, y1++, c);
}

#ifdef __C86__

/* use BIOS to set video mode */
void set_bios_mode(int mode)
{
    asm(
        "push   si\n"
        "push   di\n"
        "push   ds\n"
        "push   es\n"
        "mov    ax,[bp+4]\n"    /* AH=0, AL=mode */
        "int    0x10\n"
        "pop    es\n"
        "pop    ds\n"
        "pop    di\n"
        "pop    si\n"
    );
}

int asm_getbyte(int offset)
{
    asm(
        "mov dx,ds\n"
        "mov bx,[bp+4]\n"
        "mov ax,#0xa000\n"
        "mov ds,ax\n"
        "mov al,[bx]\n"         /* offset */
        "xor ah,ah\n"
        "mov ds,dx\n"
    );
}

#if UNUSED
/* PAL write color byte at video offset */
void pal_writevid(unsigned int offset, int c)
{
    asm(
        "mov    dx,ds\n"
        "mov    ax,#0xA000\n"
        "mov    ds,ax\n"
        "mov    bx,[bp+4]\n"    /* offset */
        "mov    al,[bp+6]\n"    /* color */
        "mov    [bx],al\n"
        "mov    ds,dx\n"
    );
}

/* PAL draw a pixel at x, y with 8-bit color c */
void pal_drawpixel(int x,int y, int color)
{
    pal_writevid(y*SCREENWIDTH + x, color);
}

int pal_readpixel(int x, int y)
{
    return 0;       /* FIXME */
}

/* PAL draw horizontal line */
void pal_drawhline(int x1, int x2, int y, int c)
{
    int x, offset;

    offset = y*SCREENWIDTH + x1;
    while(x1++ <= x2)
        pal_writevid(offset++, c);
}

void pal_drawvline(int x, int y1, int y2, int c)
{
    while (y1 <= y2)
        pal_drawpixel(x, y1++, c);
}
#endif

#endif /* __C86__ */
