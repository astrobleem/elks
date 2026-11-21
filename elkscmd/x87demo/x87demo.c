/*
 * Optional real-mode 286+ x87 smoke test for ELKS. Integer-only main/stdio.
 * The assembly saves/restores x87 state in a bounded, non-scheduling window.
 * This is not an x87 context switch implementation for the ELKS kernel.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct measurement {
    unsigned int a, b;
    unsigned long root144, diagonal_milli;
};

_Static_assert(offsetof(struct measurement, root144) == 4, "assembly layout");
_Static_assert(offsetof(struct measurement, diagonal_milli) == 8, "assembly layout");
_Static_assert(sizeof(struct measurement) == 12, "assembly layout");

extern int x87_ready(void);
extern void __attribute__((cdecl)) x87_measure(struct measurement *m);

static unsigned int leg(const char *s)
{
    unsigned int n = 0;
    while (*s >= '0' && *s <= '9') {
        unsigned int digit = *s++ - '0';
        if (n > 2000 || (n == 2000 && digit))
            return 0;
        n = n * 10 + digit;
    }
    return *s ? 0 : n;
}

int main(int argc, char **argv)
{
    struct measurement known = {3, 4, 0, 0};
    struct measurement m = {6, 7, 0, 0};
    int status;

    if (argc == 1) {
        puts("No x87 executed. After confirming an installed coprocessor:");
        puts("  x87demo --x87 [width height]");
        return 0;
    }
    if (strcmp(argv[1], "--x87") || (argc != 2 && argc != 4) ||
        (argc == 4 && (!(m.a = leg(argv[2])) || !(m.b = leg(argv[3]))))) {
        fprintf(stderr, "Usage: x87demo --x87 [width height] (1..20000 each)\n");
        return 1;
    }
    status = x87_ready();
    switch (status) {
    case 0:
        break;
    case 1:
        puts("No x87 executed: requires an IBM-compatible 286+ in real mode.");
        return 2;
    case 2:
        puts("No x87 executed: BIOS does not report a math coprocessor.");
        return 2;
    default:
        puts("No x87 executed: CPU has x87 emulation or task-switched state set.");
        return 2;
    }
    x87_measure(&known);
    if (known.root144 != 12 || known.diagonal_milli != 5000) {
        fputs("x87 self-check failed.\n", stderr);
        return 1;
    }
    printf("x87 self-check: sqrt(144) = %lu; 3 by 4 diagonal = 5.000\n",
        known.root144);
    x87_measure(&m);
    printf("Diagonal of a %u by %u rectangle: %lu.%03lu\n",
        m.a, m.b, m.diagonal_milli / 1000UL, m.diagonal_milli % 1000UL);
    return 0;
}
