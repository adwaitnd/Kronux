// timeline.c
// Adwait Dongare

// entry function for timeline module

//
// include files
//
#include <linux/module.h>
#include <linux/kernel.h>   // printk
#include <linux/rbtree.h>   // rbtree functionality
#include <linux/hrtimer.h>  // hrtimer functions and data structures
#include <linux/time.h>     // timespec & operations
#include <linux/slab.h>     // kmalloc, kfree
#include <linux/init.h>     // __init & __exit macros
#include <asm/unistd.h>         // syscall values
#include <asm-generic/uaccess.h>        // copy_from_userfcopy_from

//
// useful #\defines
//
#define MAX_NAMESIZE 32
#define SIGTASKEXPIRED 44


//
// module parameters
//
MODULE_LICENSE("GPL");          // the license type
MODULE_AUTHOR("CMU");           // the author (for modinfo)
MODULE_DESCRIPTION("Implements timeline functionality for quality-of-time");
MODULE_VERSION("0.1");

//
// data structures
//
struct qot_timeline {
    struct rb_node node_uuid;   // timeline node
    char uuid[MAX_NAMESIZE];    // unique id of timeline
}

struct timeline_sleeper {

}

//
// global objects
//

extern void *sys_call_table[];      // import syscall table
static long (*old_custom0)(void);   // store old syscall
static long (*old_custom1)(void);   // store old syscall
static long (*old_custom2)(void);   // store old syscall
static long (*old_custom3)(void);   // store old syscall
static long (*old_custom4)(void);   // store old syscall
static long (*old_custom5)(void);   // store old syscall
static long (*old_custom6)(void);   // store old syscall
static long (*old_custom7)(void);   // store old syscall
struct rb_root timeline_root = RB_ROOT;     // also initialize it

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
    old_custom0 = sys_call_table[__NR_qot_custom0];
    old_custom1 = sys_call_table[__NR_qot_custom1];
    old_custom2 = sys_call_table[__NR_qot_custom2];
    old_custom3 = sys_call_table[__NR_qot_custom3];
    old_custom4 = sys_call_table[__NR_qot_custom4];
    old_custom5 = sys_call_table[__NR_qot_custom5];
    old_custom6 = sys_call_table[__NR_qot_custom6];
    old_custom7 = sys_call_table[__NR_qot_custom7];
    sys_call_table[__NR_qot_custom0] = &sys_timeline_nanosleep;
    

    printk(KERN_INFO"[timeline] module loaded\n");
    return 0;
}

// module exit function
static void __exit timeline_exit(void) {
    sys_call_table[__NR_qot_custom0] = old_custom0;
    sys_call_table[__NR_qot_custom1] = old_custom1;
    sys_call_table[__NR_qot_custom2] = old_custom2;
    sys_call_table[__NR_qot_custom3] = old_custom3;
    sys_call_table[__NR_qot_custom4] = old_custom4;
    sys_call_table[__NR_qot_custom5] = old_custom5;
    sys_call_table[__NR_qot_custom6] = old_custom6;
    sys_call_table[__NR_qot_custom7] = old_custom7;

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

    // try to find the given timeline from tree
    // assume we found it for now

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

// // Sandeep's interface starts here



// /*Sends a signal to a process*/
// static void interface_signal(struct task_struct *task)
// {
//     struct siginfo info;

//     memset(&info, 0, sizeof(struct siginfo));
//     info.si_signo = SIGTASKEXPIRED;
//     info.si_code = SI_KERNEL;
//     info.si_int = 0;

//     send_sig_info(SIGTASKEXPIRED, &info, task);
// }

// /*Destroys a timeline node*/
// static void interface_cancel(struct rb_node *timeline_node)
// {
//     return;
// }

// /* hrtimer callback wakes up a task*/
// static enum hrtimer_restart interface_wakeup(struct hrtimer *timer)
// {
//     struct hrtimer_sleeper *t = container_of(timer, struct hrtimer_sleeper, timer);
//     struct task_struct *task = t->task;
//     interface_cancel(&timer->tl_node);
//     t->task = NULL;
//     if (task)
//         wake_up_process(task);

//     return HRTIMER_NORESTART;
// }

// /*initializes the hrtimer sleeper structure*/
// static void interface_init_sleeper(struct hrtimer_sleeper *sl, struct task_struct *task)
// {
//     sl->timer.function = interface_wakeup;
//     sl->task = task;
// }

// /* Calls HR timer to put task to sleep */
// int __sched interface_sleep(struct timespec *sleep_time)
// {
//     struct hrtimer_sleeper sleep_timer;
//     hrtimer_init_on_stack(&sleep_timer.timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
//     hrtimer_set_expires(&sleep_timer.timer, timespec_to_ktime(*sleep_time));
//     interface_init_sleeper(&sleep_timer, current);
//     do {
//         set_current_state(TASK_INTERRUPTIBLE);
//         hrtimer_start_expires(&sleep_timer.timer, HRTIMER_MODE_ABS);
//         if (!hrtimer_active(&sleep_timer.timer))
//             sleep_timer.task = NULL;

//         if (likely(sleep_timer.task))
//             freezable_schedule();

//         hrtimer_cancel(&sleep_timer.timer);
//     } while (sleep_timer.task && !signal_pending(sleep_timer.task));
//     __set_current_state(TASK_RUNNING);
//     return sleep_timer.task == NULL;
// }
// /*Changes the timeline tasks on the hrtimer rb tree*/
// static void interface_reconfigure(struct hrtimer *timer)
// {
//     struct hrtimer_sleeper *sleeper;
//     struct rb_node timeline_node;
//     struct task_struct *task;
//     sleeper = container_of(timer, struct hrtimer_sleeper, timer);
//     task = sleeper->task;
//     timeline_node = timer->tl_node;
//     hrtimer_cancel(timer);
//     hrtimer_init_on_stack(timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
//     interface_init_sleeper(sleeper, task);
//     timer->tl_node = timeline_node;
//     hrtimer_start_expires(timer, HRTIMER_MODE_ABS);
//     return;
// }

// /* Updates hrtimer softexpires when a time change happens, called by the set_time adj_time functions*/
// int interface_update(struct timespec (*get_new_time)(struct timespec *), struct rb_root *timeline_root)
// {
//     struct timespec new_expires_time;
//     struct timespec old_expires_time;
//     ktime_t new_softexpires;
//     ktime_t current_sys_time;
    
//     struct hrtimer *timer;
//     struct hrtimer_sleeper *sleeping_task;
//     int retval = 0;
//     int ret_flag = 0; /* Returns a non zero number incase a task has to be woken up midway the mod of the non zero number is the number of tasks which had to be woken up */

//     struct task_struct *task;

//     struct rb_node *timeline_node = NULL;
//     struct rb_node *next_node = NULL;
//     /*Get the current system time*/
//     current_sys_time = ktime_get_real();

//     timeline_node = rb_first(timeline_root);
//     while(timeline_node != NULL)
//     {
//         timer = container_of(timeline_node, struct hrtimer, tl_node );
//         sleeping_task = container_of(timer, struct hrtimer_sleeper, timer);
//         task = sleeping_task->task;
//         old_expires_time = ktime_to_timespec(timer->_softexpires);
//         new_expires_time = get_new_time(&old_expires_time);

//         new_softexpires = timespec_to_ktime(new_expires_time);
        
//         /* Send a signal to the user */
//         retval = ktime_compare(new_softexpires, current_sys_time);
//         if(retval <= 0)
//         {
//             interface_signal(task);
//             interface_cancel(&timer->tl_node);
//             hrtimer_cancel(timer);
//             wake_up_process(task);
//             ret_flag--;
//         }
//         else
//         {
//             hrtimer_set_expires(timer, new_softexpires);
//             interface_reconfigure(timer);
//         }
//         next_node = rb_next(timeline_node);
//         timeline_node = next_node;
//     }
    
//     return ret_flag;
// }