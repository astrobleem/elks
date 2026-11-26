#ifndef ELKS_VGA_PM_H
#define ELKS_VGA_PM_H

/* Direct mode 12h from standard 80x25 colour VGA text (ELKS/x86 PM).
 * Returns 0 on success. No BIOS call, font ROM or interrupt vector is used.
 * The caller must pair a successful enter with leave, before normal exit.
 */
extern unsigned vga_pm_graphics_seg;
extern void (*vga_pm_checkpoint)(unsigned); /* valid after successful enter */
int vga_pm_enter(const char *tty);
int vga_pm_leave(void);
#endif
