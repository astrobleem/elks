#include <linuxmt/config.h>
#include <linuxmt/errno.h>
#include <linuxmt/fs.h>
#include <linuxmt/init.h>
#include <linuxmt/memory.h>
#include <linuxmt/mm.h>
#include <linuxmt/sched.h>
#include <kmodchar.h>

#if !defined(CONFIG_ARCH_IBMPC) || defined(CONFIG_286_PMODE) || defined(CONFIG_ROMCODE)
#error Experimental character modules require an IBM-PC real-mode disk kernel
#endif
#if defined(CONFIG_KMOD_LPT) && defined(CONFIG_CHAR_DEV_LP)
#error Built-in LP and the LPT module must not claim the same ports
#endif

typedef __attribute__((__cdecl__)) __far int (*module_entry_t)(unsigned int);

/*
 * Only process-context file operations access this state. The single-CPU
 * real-mode kernel does not preempt these paths, and they do not sleep.
 * One reference per open file description covers duplicates and fork.
 */
static segment_s *module_text;
static module_entry_t module_read_byte;
static unsigned int module_output;
static unsigned int open_files;
#ifdef CONFIG_KMOD_LPT
static unsigned char module_lpt;
#endif

static unsigned int read_word(const unsigned char *p)
{
    return p[0] | ((unsigned int)p[1] << 8);
}

static int load_module(void *user_arg)
{
    unsigned char header[KMODCHAR_HEADER_LEN];
    unsigned int size, entry, output, bytes;
    segment_s *text;

    if (module_text)
        return -EBUSY;
    if ((unsigned int)user_arg > 0xffffU - sizeof(unsigned int) ||
        verify_area(VERIFY_READ, user_arg, sizeof(unsigned int)))
        return -EFAULT;
    bytes = get_user(user_arg);
    if (bytes <= KMODCHAR_HEADER_LEN ||
        bytes > KMODCHAR_HEADER_LEN + KMODCHAR_MAX_CODE)
        return -EINVAL;
    if ((unsigned int)user_arg > 0xffffU - bytes - sizeof(unsigned int) ||
        verify_area(VERIFY_READ, user_arg, bytes + sizeof(unsigned int)))
        return -EFAULT;
    memcpy_fromfs(header, (char *)user_arg + sizeof(unsigned int), sizeof(header));
    if (header[0] != 'K' || header[1] != 'M' ||
        header[2] != 'C')
        return -EINVAL;
    size = read_word(header + 4);
    entry = read_word(header + 6);
    output = read_word(header + 8);
    if (size != bytes - KMODCHAR_HEADER_LEN || entry >= size)
        return -EINVAL;
    switch (header[3]) {
    case '1':
        if (output == 0 || output > KMODCHAR_MAX_OUTPUT)
            return -EINVAL;
        break;
#ifdef CONFIG_KMOD_LPT
    case '2':
        /* The BIOS must report a real LPT1 at our one supported base. */
        if (output != KMOD_LPT_PORT)
            return -EINVAL;
        if (peekw(0x0008, 0x0040) != KMOD_LPT_PORT)
            return -ENODEV;
        break;
#endif
    default:
        return -EINVAL;
    }

    /* No process owns this segment: it survives the loader process exit. */
    text = seg_alloc((size + 15) >> 4, SEG_FLAG_KMOD);
    if (!text)
        return -ENOMEM;
    fmemcpyb(0, text->base,
        (char *)user_arg + sizeof(unsigned int) + KMODCHAR_HEADER_LEN,
        current->t_regs.ds, size);
    module_read_byte = (module_entry_t)_MK_FP(text->base, entry);
#ifdef CONFIG_KMOD_LPT
    module_lpt = header[3] == '2';
#endif
    module_output = output;
    module_text = text;
    return 0;
}

static int kmodchar_ioctl(struct inode *inode, struct file *file,
    unsigned int cmd, unsigned int arg)
{
    segment_s *text;

    if (MINOR(inode->i_rdev) != KMODCHAR_CTL_MINOR)
        return -EINVAL;
    if (!suser())
        return -EPERM;
    switch (cmd) {
    case KMODCHAR_LOAD:
        return load_module((void *)arg);
    case KMODCHAR_UNLOAD:
        if (!module_text)
            return -ENODEV;
        if (open_files)
            return -EBUSY;
        text = module_text;
        module_text = NULL;
        module_read_byte = NULL;
        module_output = 0;
#ifdef CONFIG_KMOD_LPT
        module_lpt = 0;
#endif
        seg_put(text);
        return 0;
    default:
        return -EINVAL;
    }
}

static int kmodchar_open(struct inode *inode, struct file *file)
{
    switch (MINOR(inode->i_rdev)) {
    case KMODCHAR_CTL_MINOR:
        return suser() ? 0 : -EPERM;
    case KMODCHAR_DEV_MINOR:
        if (!module_text)
            return -ENODEV;
#ifdef CONFIG_KMOD_LPT
        if (module_lpt) {
            if (file->f_mode != FMODE_WRITE)
                return -EACCES;
            if (open_files)
                return -EBUSY;
        }
#endif
        open_files++;
        return 0;
    default:
        return -ENODEV;
    }
}

static void kmodchar_release(struct inode *inode, struct file *file)
{
    if (MINOR(inode->i_rdev) == KMODCHAR_DEV_MINOR)
        open_files--;
}

static size_t kmodchar_read(struct inode *inode, struct file *file, char *buffer,
    size_t count)
{
    unsigned int i;

    if (MINOR(inode->i_rdev) != KMODCHAR_DEV_MINOR)
        return -EINVAL;
#ifdef CONFIG_KMOD_LPT
    if (module_lpt)
        return -EINVAL;
#endif
    if (file->f_pos < 0)
        return -EINVAL;
    if (file->f_pos >= module_output)
        return 0;
    if (count > module_output - (unsigned int)file->f_pos)
        count = module_output - (unsigned int)file->f_pos;
    if ((unsigned int)buffer > 0xffffU - count ||
        verify_area(VERIFY_WRITE, buffer, count))
        return -EFAULT;
    for (i = 0; i < count; ++i)
        put_user_char(module_read_byte((unsigned int)file->f_pos + i), buffer + i);
    file->f_pos += count;
    return count;
}

#ifdef CONFIG_KMOD_LPT
static size_t kmodchar_write(struct inode *inode, struct file *file, char *buffer,
    size_t count)
{
    unsigned int i;
    int error;

    if (MINOR(inode->i_rdev) != KMODCHAR_DEV_MINOR || !module_lpt)
        return -EINVAL;
    /* Limit time spent in one non-sleeping file operation. */
    if (count > 64)
        count = 64;
    if ((unsigned int)buffer > 0xffffU - count ||
        verify_area(VERIFY_READ, buffer, count))
        return -EFAULT;
    for (i = 0; i < count; ++i) {
        switch (module_read_byte(get_user_char(buffer + i))) {
        case KMOD_LPT_SENT:
            continue;
        case KMOD_LPT_BUSY:
            error = -EAGAIN;
            break;
        case KMOD_LPT_PAPER:
            error = -ENOSPC;
            break;
        default:
            error = -EIO;
            break;
        }
        return i ? i : error;
    }
    return count;
}
#endif

static struct file_operations kmodchar_fops = {
    .read = kmodchar_read,
#ifdef CONFIG_KMOD_LPT
    .write = kmodchar_write,
#endif
    .ioctl = kmodchar_ioctl,
    .open = kmodchar_open,
    .release = kmodchar_release
};

void INITPROC kmodchar_init(void)
{
    if (register_chrdev(KMOD_CHAR_MAJOR, "kmodchar", &kmodchar_fops))
        printk("kmodchar: cannot register device\n");
}
