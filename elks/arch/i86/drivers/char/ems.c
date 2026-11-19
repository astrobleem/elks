/*
 * Optional /dev/ems bounded-copy service for Lo-tech style EMS boards
 * (four 16K windows, one write-only 8-bit page register per window at
 * base..base+3), including the PicoMEM emulation of that board.
 *
 * Nothing here runs unless /bootopts names the board: ems=PORT,FRAME[,PAGES]
 * e.g. ems=0x268,0xE000. There is no port scan and no EMM (INT 67h) call: a
 * DOS expanded-memory manager does not exist after a native boot.
 *
 * Only window 0 is used. Each copy sets the page register itself, so no
 * mapping is assumed to survive between calls, across a schedule or across
 * an interrupt; ELKS system calls are not preempted and no interrupt handler
 * touches the frame. An 8088 has no memory protection: any kernel code or
 * /dev/mem writer that pokes the frame or the ports can still corrupt pages.
 */
#ifndef EMS_HOST_TEST
#include <linuxmt/config.h>
#endif
#if defined(CONFIG_CHAR_DEV_EMS) || defined(EMS_HOST_TEST)
#ifndef EMS_HOST_TEST
#ifdef CONFIG_286_PMODE
#error /dev/ems currently requires real-mode IBM PC hardware
#endif
#include <linuxmt/kernel.h>
#include <linuxmt/types.h>
#include <linuxmt/errno.h>
#include <linuxmt/fs.h>
#include <linuxmt/mm.h>
#include <linuxmt/sched.h>
#include <linuxmt/major.h>
#include <linuxmt/memory.h>
#include <linuxmt/init.h>
#include <linuxmt/biosparm.h>
#include <linuxmt/ems.h>
#include <arch/io.h>
#include <arch/segment.h>

#define hw_map(page)            outb((page), ems_port)
#define hw_peekw(off, seg)      peekw((off), (seg))
#define hw_pokew(off, seg, v)   pokew((off), (seg), (v))
#define hw_peekb(off, seg)      peekb((off), (seg))
/* between the caller's data segment and window 0 */
#define hw_get(buf, off, n)     fmemcpyb((buf), current->t_regs.ds, (void *)(off), ems_frame, (n))
#define hw_put(buf, off, n)     fmemcpyb((void *)(off), ems_frame, (buf), current->t_regs.ds, (n))
#endif

unsigned int ems_port;          /* set from /bootopts; 0 leaves the driver inert */
unsigned int ems_frame;
unsigned int ems_limit;         /* optional page cap from /bootopts */

static unsigned int ems_pages;  /* verified usable pages; 0 means no service */
static unsigned char ems_busy;  /* the single client has the device open */
static unsigned char ems_owner[EMS_MAX_PAGES];  /* 0 free, else handle */
/* A handle is one contiguous run of pages; count 0 marks it unused. */
static struct { unsigned short first, count; } ems_run[EMS_MAX_HANDLES];

/* verify_area's 16-bit end-offset addition does not detect wrap. Keep the
 * one-past-end representable before asking it to check the process bounds.
 * Use an explicit 16-bit bound so host tests exercise the same arithmetic. */
static int ems_range_fits(unsigned int offset, unsigned int count)
{
    return offset <= 0xFFFFU && count <= 0xFFFFU - offset;
}

static unsigned int handle_first(unsigned int handle, unsigned int *count)
{
    if (!handle || handle > EMS_MAX_HANDLES) {
        *count = 0;
        return 0;
    }
    *count = ems_run[handle - 1].count;
    return ems_run[handle - 1].first;
}

static void ems_release_all(void)
{
    unsigned int page;

    for (page = 0; page < EMS_MAX_PAGES; page++)
        ems_owner[page] = 0;
    for (page = 0; page < EMS_MAX_HANDLES; page++)
        ems_run[page].count = 0;
}

static unsigned int ems_free_pages(void)
{
    unsigned int page, n = 0;

    for (page = 0; page < ems_pages; page++)
        if (!ems_owner[page]) n++;
    return n;
}

/* First fit. Returns a handle, or a negative error. */
static int ems_alloc(unsigned int want)
{
    unsigned int handle, page, run = 0, unused;

    if (!want || want > ems_pages)
        return want ? -ENOMEM : -EINVAL;
    for (handle = 1; handle <= EMS_MAX_HANDLES; handle++) {
        handle_first(handle, &unused);
        if (!unused) break;
    }
    if (handle > EMS_MAX_HANDLES)
        return -ENFILE;
    for (page = 0; page < ems_pages; page++) {
        run = ems_owner[page] ? 0 : run + 1;
        if (run == want) {
            ems_run[handle - 1].first = page + 1 - want;
            ems_run[handle - 1].count = want;
            while (run--) ems_owner[page - run] = (unsigned char)handle;
            return (int)handle;
        }
    }
    return -ENOMEM;
}

static int ems_free(unsigned int handle)
{
    unsigned int page, n;

    handle_first(handle, &n);
    if (!n) return -EBADF;
    for (page = 0; page < ems_pages; page++)
        if (ems_owner[page] == handle) ems_owner[page] = 0;
    ems_run[handle - 1].count = 0;
    return 0;
}

/* Copy count bytes between buf (caller's data segment, already verified)
 * and the handle's storage. The whole range is validated before any page
 * register is written, then copied one page-sized piece at a time. */
static int ems_transfer(unsigned int handle, unsigned long offset, char *buf,
                        unsigned int count, int writing)
{
    unsigned int first, pages, page, within, piece;

    first = handle_first(handle, &pages);
    if (!pages) return -EBADF;
    /* 16K pages: shifts, not the 32-bit divide an 8088 would grind through */
    if (offset > ((unsigned long)pages << 14) ||
        count > ((unsigned long)pages << 14) - offset)
        return -EINVAL;
    page = first + (unsigned int)(offset >> 14);
    within = (unsigned int)offset & (EMS_PAGE_SIZE - 1);
    while (count) {
        piece = EMS_PAGE_SIZE - within;
        if (piece > count) piece = count;
        hw_map(page);
        if (writing) hw_put(buf, within, piece);
        else hw_get(buf, within, piece);
        buf += piece;
        count -= piece;
        within = 0;
        page++;
    }
    return 0;
}

#ifndef EMS_HOST_TEST
static int ems_user_area(int type, char *buf, unsigned int count)
{
    if (!ems_range_fits((unsigned int)buf, count)) return -EFAULT;
    return count ? verify_area(type, buf, count) : 0;
}

static int ems_open(struct inode *inode, struct file *filp)
{
    if (!ems_pages) return -ENODEV;
    if (ems_busy) return -EBUSY;        /* one client: no shared page state */
    ems_busy = 1;
    return 0;
}

static void ems_close(struct inode *inode, struct file *filp)
{
    /* Reached on the last close, including when the client is killed. */
    ems_release_all();
    ems_busy = 0;
}

static int ems_ioctl(struct inode *inode, struct file *filp, int cmd, char *arg)
{
    struct ems_info info;
    struct ems_xfer xfer;
    unsigned short pages;
    int result;

    switch (cmd) {
    case EMS_GETINFO:
        if (ems_user_area(VERIFY_WRITE, arg, sizeof(info))) return -EFAULT;
        info.pages = ems_pages;
        info.free = ems_free_pages();
        info.frame = ems_frame;
        info.port = ems_port;
        return verified_memcpy_tofs(arg, &info, sizeof(info));
    case EMS_ALLOC:
        /* Check the result buffer before allocating: a failed copy-out must
         * not leave an unreachable handle behind. */
        if (ems_user_area(VERIFY_READ, arg, sizeof(pages)) ||
            ems_user_area(VERIFY_WRITE, arg, sizeof(pages))) return -EFAULT;
        if (verified_memcpy_fromfs(&pages, arg, sizeof(pages))) return -EFAULT;
        result = ems_alloc(pages);
        if (result < 0) return result;
        pages = (unsigned short)result;
        return verified_memcpy_tofs(arg, &pages, sizeof(pages));
    case EMS_FREE:
        return ems_free((unsigned int)arg);
    case EMS_READ:
    case EMS_WRITE:
        if (ems_user_area(VERIFY_READ, arg, sizeof(xfer))) return -EFAULT;
        if (verified_memcpy_fromfs(&xfer, arg, sizeof(xfer))) return -EFAULT;
        if (ems_user_area(cmd == EMS_READ ? VERIFY_WRITE : VERIFY_READ,
                          xfer.buf, xfer.count))
            return -EFAULT;
        return ems_transfer(xfer.handle, xfer.offset, xfer.buf, xfer.count,
                            cmd == EMS_WRITE);
    }
    return -EINVAL;
}

static struct file_operations ems_fops = {
    NULL,                       /* lseek */
    NULL,                       /* read */
    NULL,                       /* write */
    NULL,                       /* readdir */
    NULL,                       /* select */
    ems_ioctl,                  /* ioctl */
    ems_open,                   /* open */
    ems_close                   /* release */
};
#endif /* !EMS_HOST_TEST */

/* Find an expansion ROM overlapping [base, base+len), including a ROM
 * whose header precedes the frame. ROM size is in 512-byte units. Never
 * write to the proposed frame while checking. A zero-sized header below
 * its end is ambiguous, so fail closed rather than probe through it. */
static unsigned int INITPROC rom_in(unsigned int base, unsigned int len)
{
    unsigned int seg, size;
    unsigned long end = (unsigned long)base + len;

    for (seg = 0xC000; seg < end; seg += 0x80) {
        if (hw_peekw(0, seg) != 0xAA55) continue;
        size = hw_peekb(2, seg);
        if (seg >= base || !size ||
            (unsigned long)seg + ((unsigned long)size << 5) > base)
            return seg;
    }
    return 0;
}

#if !defined(EMS_HOST_TEST) && (defined(CONFIG_BLK_DEV_BHD) || defined(CONFIG_BLK_DEV_BFD))
#define EMS_ASK_PICOMEM
#endif

/* If a PicoMEM BIOS is installed, its own record of the EMS port and frame
 * must agree with /bootopts. Found by reading ROM headers only. The query is
 * INT 13h AX=6001h DX=1234h, the same call path every BIOS disk read already
 * takes; AX=6000h is never issued because that function reinstalls the
 * PicoMEM IRQ vectors. Returns 0 no PicoMEM, 1 agrees, -1 disagrees. */
static int INITPROC picomem_agrees(void)
{
    unsigned int seg, i;

    for (seg = 0xC000; seg < 0xF000; seg += 0x80) {
        if (hw_peekw(0, seg) != 0xAA55) continue;
        for (i = 3; i < 8; i++)
            if (hw_peekw(i, seg) == 0x4D50 && hw_peekw(i + 2, seg) == 0x4942 &&
                hw_peekw(i + 4, seg) == 0x534F)         /* "PMBIOS" */
                goto found;
    }
    return 0;
found:
#ifdef EMS_ASK_PICOMEM
    {
        struct biosparms bp;

        bp.irq = BIOSHD_INT;
        bp.ax = 0x6001;
        bp.bx = bp.cx = bp.si = bp.di = bp.bp = 0;
        bp.dx = 0x1234;
        bp.es = bp.ds = kernel_ds;
        bp.fl = 0;
        call_bios(&bp);
        printk("ems: PicoMEM BIOS at %x reports port %x frame %x\n", seg, bp.cx, bp.dx);
        return bp.cx == ems_port && bp.dx == ems_frame ? 1 : -1;
    }
#elif defined(EMS_HOST_TEST)
    return hw_picomem_query(seg);
#else
    /* A direct-disk kernel has no call_bios trampoline. Do not guess the
     * PicoMEM settings or fall through to a destructive frame probe. */
    return -1;
#endif
}

/* Does window 0 follow the page register? Two pages must hold different
 * words at the same address. The four words touched are put back. */
static int INITPROC window_switches(void)
{
    unsigned int keep0, keep1, ok;

    hw_map(0); keep0 = hw_peekw(0, ems_frame);
    hw_map(1); keep1 = hw_peekw(0, ems_frame);
    hw_map(0); hw_pokew(0, ems_frame, 0x5AC3);
    hw_map(1); hw_pokew(0, ems_frame, 0xA53C);
    hw_map(0); ok = hw_peekw(0, ems_frame) == 0x5AC3;
    hw_map(1); ok = ok && hw_peekw(0, ems_frame) == 0xA53C;
    hw_pokew(0, ems_frame, keep1);
    hw_map(0); hw_pokew(0, ems_frame, keep0);
    return ok;
}

/* Count distinct pages. Tags go on from the top down so that, where the
 * board decodes fewer address bits, the lowest alias is written last and
 * the first page that reads back another page's tag ends the count. */
static unsigned int INITPROC count_pages(unsigned int limit)
{
    unsigned int page;

    for (page = limit; page--;) {
        hw_map(page);
        hw_pokew(0, ems_frame, 0xE300 | page);
        hw_pokew(2, ems_frame, ~page);
    }
    for (page = 0; page < limit; page++) {
        hw_map(page);
        if (hw_peekw(0, ems_frame) != (0xE300 | page) ||
            hw_peekw(2, ems_frame) != (~page & 0xFFFFU))
            break;
    }
    return page;
}

/* Called for each umb= range before it joins the kernel allocator: the
 * frame must not be memory ELKS hands out. */
void INITPROC ems_exclude(unsigned int base, unsigned int len)
{
    if (ems_port && base < ems_frame + 0x1000U &&
        (unsigned long)base + len > ems_frame) {
        printk("ems: disabled, frame %x overlaps umb=\n", ems_frame);
        ems_port = 0;
    }
}

void INITPROC ems_init(void)
{
    static const unsigned int ports[] =
        { 0x260, 0x264, 0x268, 0x26C, 0x288, 0x298, 0x2A8 };
    unsigned int i, limit;
    const char *why;

    if (!ems_port) return;                      /* not requested: touch nothing */
    why = "unsupported port";
    for (i = 0; i < sizeof(ports)/sizeof(ports[0]); i++)
        if (ports[i] == ems_port) break;
    if (i == sizeof(ports)/sizeof(ports[0])) goto refuse;
    /* 64K frame above video memory and below the system ROM, on a 16K step */
    why = "frame must be C000-E000 on a 16K boundary";
    if (ems_frame < 0xC000 || ems_frame > 0xE000 || (ems_frame & 0x3FF)) goto refuse;
    why = "an option ROM overlaps the frame";
    if (rom_in(ems_frame, 0x1000)) goto refuse;
    why = "PicoMEM BIOS settings could not be confirmed";
    if (picomem_agrees() < 0) goto refuse;
    why = "frame is not bank-switched RAM";
    if (!window_switches()) goto refuse;
    limit = ems_limit && ems_limit < EMS_MAX_PAGES ? ems_limit : EMS_MAX_PAGES;
    ems_pages = count_pages(limit);
    why = "fewer than two usable pages";
    if (ems_pages < 2) { ems_pages = 0; goto refuse; }
#ifndef EMS_HOST_TEST
    if (register_chrdev(EMS_MAJOR, "ems", &ems_fops)) {
        ems_pages = 0;
        why = "cannot register device";
        goto refuse;
    }
#endif
    printk("ems: port %x frame %x, %u pages (%uK)\n", ems_port, ems_frame,
           ems_pages, ems_pages << 4);
    return;
refuse:
    printk("ems: disabled, %s (port %x frame %x)\n", why, ems_port, ems_frame);
}
#endif /* CONFIG_CHAR_DEV_EMS */
