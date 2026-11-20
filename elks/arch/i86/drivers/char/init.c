#include <linuxmt/config.h>
#include <linuxmt/types.h>
#include <linuxmt/init.h>

#ifdef CONFIG_KMOD_CHAR
void INITPROC kmodchar_init(void);
#endif

void INITPROC chr_dev_init(void)
{
#ifdef CONFIG_KMOD_CHAR
    kmodchar_init();
#endif

#ifdef CONFIG_CHAR_DEV_LP
    lp_init();
#endif

#ifdef CONFIG_CHAR_DEV_MEM
    mem_dev_init();
#endif

#if defined(CONFIG_CHAR_DEV_DSP) || defined(CONFIG_KMOD_DSP)
    dsp_init();
#endif

#ifdef CONFIG_INET
    tcpdev_init();
#endif

#ifdef CONFIG_ETH
    eth_init();
#endif

#ifdef CONFIG_PSEUDO_TTY    
    pty_init();    
#endif
}
