// timeline.c
// Adwait Dongare

// entry function for timeline module

//
// include files
//
#include <linux/module.h>
#include <linux/kernel.h>   // printk
#include <linux/rbtree.h>   // rbtree functionality
#include <linux/time.h>     // timespec & operations
#include <linux/slab.h>     // kmalloc, kfree
#include <linux/init.h>     // __init & __exit macros
#include <asm/unistd.h>         // syscall values
#include <asm-generic/uaccess.h>        // copy_from_userfcopy_from

//
// useful #\defines
//
#define MAX_NAMESIZE 32

//
// module parameters
//
MODULE_LICENSE("GPL");            // the license type
MODULE_AUTHOR("Adwait Dongare, CMU"); // the author (for modinfo)
MODULE_DESCRIPTION("Implements timeline functionality for quality-of-time");
MODULE_VERSION("0.1");

//
// external objects
//

extern void *sys_call_table[];
long (*old_custom0)(void);

//
// function definitions 
//

asmlinkage long sys_timeline_nanosleep(char __user *timeline_id, struct timespec __user *exp_time);

//
// function declarations
//

// module initializatin function
static int __init timeline_init(void) {
    
    // Hijack system calls
    printk(KERN_INFO"[timeline] old custom0 call @table: %p\n", sys_call_table[__NR_qot_custom0]);
    printk(KERN_INFO"[timeline] old custom1 call @table: %p\n", sys_call_table[__NR_qot_custom1]);
    printk(KERN_INFO"[timeline] our function @local: %p\n", &sys_timeline_nanosleep);
    old_custom0 = sys_call_table[__NR_qot_custom0];
    sys_call_table[__NR_qot_custom0] = &sys_timeline_nanosleep;
    printk(KERN_INFO"[timeline] new value @table: %p\n", sys_call_table[__NR_qot_custom0]);

    printk(KERN_INFO"[timeline] module loaded\n");

    return 0;
}

// module exit function
static void __exit timeline_exit(void) {
    printk(KERN_INFO"[timeline] current call %p\n", sys_call_table[__NR_qot_custom0]);
    sys_call_table[__NR_qot_custom0] = old_custom0;
    printk(KERN_INFO"[timeline] restored call: %p\n", sys_call_table[__NR_qot_custom0]);
    printk(KERN_INFO"[timeline] module unloaded\n");
}



// hijacked system call
// absolute sleep on given timeline
asmlinkage long sys_timeline_nanosleep(char __user *timeline_id, struct timespec __user *exp_time) {
    char tlid[MAX_NAMESIZE];
    struct timespec t;

    // copy user data
    if(copy_from_user(tlid, timeline_id, MAX_NAMESIZE) || copy_from_user(&t, exp_time, sizeof(struct timespec))) {
        printk(KERN_ALERT "[sys_timeline_nanosleep] could not copy_from_user\n");
        return -EFAULT;
    }

    if(!(timespec_valid(&t))) {
        printk(KERN_INFO "[sys_timeline_nanosleep] invalid timespec\n");
        return -EINVAL;
    }

    printk(KERN_INFO "[sys_timeline_nanosleep] timeline id: %s, expiry: %ld.%lu", tlid, t.tv_sec, t.tv_nsec);

    // try to find the given timeline

    return 0;
}

// SYSCALL_DEFINE2(nanosleep, struct timespec __user *, rqtp,
//         struct timespec __user *, rmtp)
// {
//     struct timespec tu;

//     if (copy_from_user(&tu, rqtp, sizeof(tu)))
//         return -EFAULT;

//     if (!timespec_valid(&tu))
//         return -EINVAL;

//     return hrtimer_nanosleep(&tu, rmtp, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
// }


//
// module registration functions
//
module_init(timeline_init);
module_exit(timeline_exit);