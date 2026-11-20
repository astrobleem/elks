#ifndef ELKS_KMODCHAR_H
#define ELKS_KMODCHAR_H

#define KMODCHAR_LOAD       0x6d01
#define KMODCHAR_UNLOAD     0x6d02
#define KMODCHAR_CTL_MINOR  0
#define KMODCHAR_DEV_MINOR  1
#define KMODCHAR_MAX_CODE   512
#define KMODCHAR_MAX_OUTPUT 64
#define KMODCHAR_HEADER_LEN 10

#define KMOD_LPT_PORT       0x0378
#define KMOD_LPT_SENT       0
#define KMOD_LPT_BUSY       1
#define KMOD_LPT_PAPER      2
#define KMOD_LPT_IOERR      3

/*
 * KMC1: magic[4], text size, entry offset, output length (little-endian words),
 * followed by position-independent 8086 text. The entry is called far using
 * cdecl, with a word offset parameter. Return that byte (zero-extended in AX).
 * KMC2: same layout, last word is the LPT1 base (0x378 only). The
 * cdecl far entry takes one word (the byte to write) and returns a
 * KMOD_LPT_* result in AX. The kernel uses a one-writer file policy.
 * Preserve DS/ES and cdecl callee-saved registers. No callbacks into the kernel,
 * blocking, interrupt or DMA work. This is trusted native code, not a sandbox.
 */
#ifndef __ASSEMBLER__
struct kmodchar_upload {
    unsigned int bytes;
    unsigned char image[KMODCHAR_HEADER_LEN + KMODCHAR_MAX_CODE];
};

#endif

#endif
