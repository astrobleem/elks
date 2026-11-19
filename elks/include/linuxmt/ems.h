#ifndef __LINUXMT_EMS_H
#define __LINUXMT_EMS_H

/*
 * /dev/ems: bank-switched expanded memory as bounded storage.
 *
 * The page frame is never exposed to a process. A single client opens the
 * device, allocates runs of 16K pages and copies bytes in and out by handle
 * and offset; the kernel maps one page at a time for the length of one copy.
 * Closing the device, including by process exit, frees every handle.
 */

#define EMS_PAGE_SIZE   16384U
#define EMS_MAX_PAGES   256     /* one 8-bit Lo-tech style page register */
#define EMS_MAX_HANDLES 24

#define EMS_GETINFO     1       /* arg: struct ems_info *                   */
#define EMS_ALLOC       2       /* arg: unsigned short *: pages in, handle out */
#define EMS_FREE        3       /* arg: handle value                        */
#define EMS_READ        4       /* arg: struct ems_xfer *: EMS -> buf       */
#define EMS_WRITE       5       /* arg: struct ems_xfer *: buf -> EMS       */

struct ems_info {
    unsigned short pages;       /* usable pages found at boot */
    unsigned short free;        /* pages not owned by a handle */
    unsigned short frame;       /* page frame segment (informational) */
    unsigned short port;        /* page register base port (informational) */
};

struct ems_xfer {
    unsigned short handle;
    unsigned short count;       /* bytes; may cross page boundaries */
    unsigned long  offset;      /* from the start of the handle's first page */
    char          *buf;         /* in the caller's data segment */
};

#endif
