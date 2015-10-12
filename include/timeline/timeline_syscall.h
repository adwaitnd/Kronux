#ifndef TIMELINE_SYSCALL_H
#define TIMELINE_SYSCALL_H
#include <linux/unistd.h>
#include <linux/linkage.h>
#ifdef CONFIG_TIMELINE
asmlinkage int sys_timeline_wait_until(void);
asmlinkage int sys_timeline_sleep(void);
#endif
#endifs