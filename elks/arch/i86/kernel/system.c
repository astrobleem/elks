#include <linuxmt/config.h>
#include <linuxmt/types.h>
#include <linuxmt/kernel.h>
#include <linuxmt/wait.h>
#include <linuxmt/sched.h>
#include <linuxmt/fs.h> /* for ROOT_DEV */
#include <linuxmt/heap.h>
#include <linuxmt/memory.h>
#include <linuxmt/mem.h>
#include <linuxmt/debug.h>

#include <arch/segment.h>
#include <arch/system.h>
#include <arch/io.h>

seg_t membase, memend;  /* start and end segment of available main memory */
unsigned int heapsize;  /* max size of kernel near heap */
byte_t sys_caps;        /* system capabilities bits */
unsigned char arch_cpu; /* CPU type from cputype.S */

#ifdef CONFIG_ARCH_IBMPC
/*
 * Identify the active onboard dispatcher, not merely an installed Tandy
 * system ROM. Only dereference fixed ROM locations after the IVT check.
 */
static int INITPROC tandy_video_active(void)
{
    unsigned short entry, call;

    if (SETUP_VIDEO_DCC != 0xffff || SETUP_VIDEO_EGA != 0xffff ||
        peekw(0, 0xc000) == 0xaa55 || peekw(0x63, 0x40) != 0x3d4 ||
        (peekb(0xb5, 0x40) & 0x10))
        return 0;

    if (peekw(0x40, 0) != 0xf065 || peekw(0x42, 0) != 0xf000)
        return 0;

    entry = peekw(0xf066, 0xf000);
    call = peekw(0xf06e, 0xf000);
    if (!((entry == 0xe856 && call == 0xe868) ||  /* 1000 EX/SX 01.02.00 */
          (entry == 0x0e29 && call == 0x0e41)))   /* 1000 HX 02.00.00 */
        return 0;

    /* Dispatch table and mode gate: BDA 40:B5 bit 4 restricts modes to 0-7. */
    return peekw(0xf072, 0xf000) == 0xff2e &&
        peekw(0xf074, 0xf000) == 0x77a5 &&
        peekw(0xf076, 0xf000) == 0x20f0 &&
        peekw(0xf13c, 0xf000) == 0x06f6 &&
        peekw(0xf13e, 0xf000) == 0x00b5 &&
        peekw(0xf140, 0xf000) == 0x7410 &&
        peekw(0xf149, 0xf000) == 0x0a3c;
}
#endif

unsigned int INITPROC setup_arch(void)
{
    unsigned int endbss, heapsegs;

#ifdef CONFIG_HW_COMPAQFAST
    outb_p(1,0xcf); /* Switch COMPAQ Deskpro to high speed */
#endif

    /*
     * Extend kernel data segment to maximum of 64K to make room for local heap.
     *
     * Set membase to beginning of available main memory, which
     * is directly after end of the kernel data segment.
     *
     * Set memend to end of available main memory.
     * If ramdisk configured, subtract space for it from end of memory.
     *
     * Calculate heapsize for near heap allocator.
     * Return start address for near heap allocator.
     */

    /* Start heap allocations at even addresses */
    endbss = (unsigned int)(_endbss + 1) & ~1;

    /* Calculate size of heap, which extends end of kernel data segment */
#ifdef SETUP_HEAPSIZE
    heapsize = SETUP_HEAPSIZE;          /* may also be set via heap= in /bootopts */
#endif
    if (heapsize) {
        heapsegs = (1 + ~endbss) >> 4;  /* max possible heap in segments*/
        if ((heapsize >> 4) < heapsegs) /* allow if less than max*/
            heapsegs = heapsize >> 4;
        membase = kernel_ds + heapsegs + (((unsigned int) (_endbss+15)) >> 4);
        heapsize = heapsegs << 4;
    } else {
        membase = kernel_ds + 0x1000;
        heapsize = 1 + ~endbss;
    }
    debug("endbss %x heap %x kdata size %x\n", endbss, heapsize, (membase-kernel_ds)<<4);

    memend = SETUP_MEM_KBYTES << 6;
#ifdef CONFIG_ARCH_IBMPC
    /* Original Tandy 1000: mode 09h aliases 98000h-9FFFFh. BIOS 624K
     * reserves only its upper half. Reserve the rest BEFORE mm_init. */
    if (tandy_video_requested) {
        if (SETUP_MEM_KBYTES == 624 && peekb(0x000e,0xffff) == 0xff &&
            peekb(0,0xfc00) == 0x21 && membase < 0x9800 &&
            tandy_video_active()) {
            memend = 0x9800;
            tandy_video_reserved = 0x9800;
            printk("Tandy video: 32K reserved at 98000, usable RAM 608K\n");
        } else printk("Tandy video: active onboard graphics or shared RAM not confirmed, color disabled\n");
    }
#endif

#if defined(CONFIG_RAMDISK_SEGMENT) && (CONFIG_RAMDISK_SEGMENT > 0)
    if (CONFIG_RAMDISK_SEGMENT <= memend) {
        /* reduce top of memory by size of ram disk*/
        memend -= CONFIG_RAMDISK_SECTORS << 5;
    }
#endif

    arch_cpu = SETUP_CPU_TYPE;
#ifdef SYS_CAPS
    sys_caps = SYS_CAPS;                /* custom system capabilities */
#else
    if (arch_cpu >= CPU_80286)          /* 80286+ IBM PC/AT capabilities or Unknown CPU */
        sys_caps = CAP_ALL;
#ifdef CONFIG_ARCH_IBMPC
    else {
        unsigned char __far *machine_type = _MK_FP(0xF000, 0xFFFE);
        if (*machine_type == 0xFC)      /* model AT */
            sys_caps |= CAP_KBD_LEDS;
    }
#endif
    debug("arch %d sys_caps %02x\n", arch_cpu, sys_caps);
#endif

    return endbss;                      /* used as start address in near heap init */
}

void INITPROC kernel_banner_arch(void) {
#ifdef CONFIG_ARCH_IBMPC
    printk("PC/%cT class cpu %d, ", (sys_caps & CAP_PC_AT) ? 'A' : 'X', arch_cpu);
#endif

#ifdef CONFIG_ARCH_PC98
    printk("PC-9801 cpu %d, ", arch_cpu);
#endif

#ifdef CONFIG_ARCH_8018X
    printk("8018X machine, ");
#endif

#ifdef CONFIG_ARCH_NECV25
    printk("NECV25 machine, cpu %d, ", arch_cpu);
#endif

#ifdef CONFIG_ARCH_SOLO86
    printk("Solo/86 machine, ");
#endif
}
