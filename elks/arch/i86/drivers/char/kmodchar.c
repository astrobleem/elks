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
        header[2] != 'C' || header[3] != '1')
        return -EINVAL;
    size = read_word(header + 4);
    entry = read_word(header + 6);
    output = read_word(header + 8);
    if (size != bytes - KMODCHAR_HEADER_LEN || entry >= size ||
        output == 0 || output > KMODCHAR_MAX_OUTPUT)
        return -EINVAL;

    /* No process owns this segment: it survives the loader process exit. */
    text = seg_alloc((size + 15) >> 4, SEG_FLAG_KMOD);
    if (!text)
        return -ENOMEM;
    fmemcpyb(0, text->base,
        (char *)user_arg + sizeof(unsigned int) + KMODCHAR_HEADER_LEN,
        current->t_regs.ds, size);
    module_read_byte = (module_entry_t)_MK_FP(text->base, entry);
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

static struct file_operations kmodchar_fops = {
    .read = kmodchar_read,
    .ioctl = kmodchar_ioctl,
    .open = kmodchar_open,
    .release = kmodchar_release
};

void INITPROC kmodchar_init(void)
{
    if (register_chrdev(KMOD_CHAR_MAJOR, "kmodchar", &kmodchar_fops))
        printk("kmodchar: cannot register device\n");
}
