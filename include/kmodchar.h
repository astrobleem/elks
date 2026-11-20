#ifndef ELKS_KMODCHAR_H
#define ELKS_KMODCHAR_H

#define KMODCHAR_LOAD       0x6d01
#define KMODCHAR_UNLOAD     0x6d02
#define KMODCHAR_CTL_MINOR  0
#define KMODCHAR_DEV_MINOR  1
#define KMODCHAR_MAX_CODE   512
#define KMODCHAR_MAX_OUTPUT 64
#define KMODCHAR_HEADER_LEN 10

/*
 * KMC1: magic[4], text size, entry offset, output length (little-endian words),
 * followed by position-independent 8086 text. The entry is called far using
 * cdecl, with a word offset parameter. Return that byte (zero-extended in AX).
 * Preserve DS/ES and cdecl callee-saved registers. No callbacks into the kernel,
 * blocking, interrupt or DMA work. This is trusted native code, not a sandbox.
 */
struct kmodchar_upload {
    unsigned int bytes;
    unsigned char image[KMODCHAR_HEADER_LEN + KMODCHAR_MAX_CODE];
};

#endif
