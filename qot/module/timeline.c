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
#include <linux/timekeeping.h>  // for getnstimeofday
#include <asm/unistd.h>         // syscall values
#include <linux/freezer.h>      // for freezable_schedule
#include <linux/ptp_clock_kernel.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/timecounter.h>

//
// useful #\defines
//
#define QOT_MAX_NAMELEN 32
#define SIGTASKEXPIRED 42       // our signal (...)


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
    char uuid[QOT_MAX_NAMELEN];         // UUID
    struct rb_node node_uuid;           // Red-black tree is used to store timelines on UUID
    struct rb_node node_ptpi;           // Red-black tree is used to store timelines on PTP index
    struct rb_root event_head;          // RB tree head for events on this timeline
    spinlock_t rb_lock;                 // RB tree spinlock

    struct ptp_clock_info info;         // PTP clock infomration
    struct ptp_clock *clock;         
       // PTP clock itself
    int index;                          // Index of the clock
    spinlock_t lock;                    // Protects driver time registers
    struct list_head head_acc;          // Head pointing to maximum accuracy structure
    struct list_head head_res;          // Head pointing to maximum resolution structure
    // struct qot_metric actual;           // The actual accuracy/resolution
    int dialed_frequency;           // Discipline: dialed frequency
    unsigned int cc_mult;                   // Discipline: mult carry
    u64 last;                      // Discipline: last cycle count of discipline
    s64 mult;                       // Discipline: ppb multiplier for errors
    s64 nsec;                       // Discipline: global time offset

     
};

struct timeline_sleeper {
    struct rb_node tl_node;             // RB tree node for timeline event
    ktime_t expires;                    // expiry time for this node
    struct qot_timeline *timeline;      // timeline this belongs to
    struct hrtimer timer;               // hrtimer
    struct task_struct *task;           // task this belongs to
};

struct qot_clock_params {
    u64 last;
    s64 mult;
    s64 nsec;
};

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
s64 nsec;
struct timespec epsilon = {0,1000000};      // the allowable timing error between event request & execution
struct qot_timeline global_timeline;

//
// function definitions 
//

asmlinkage long sys_timeline_nanosleep(char __user *timeline_id, struct timespec __user *exp_time);
asmlinkage long sys_set_offset(char __user *timeline_id, s64 __user *off);
asmlinkage long sys_print_timeline(char __user *uuid);
static int timeline_event_add(struct rb_root *head, struct timeline_sleeper *sleeper);
// static void signal_missed_deadline(struct task_struct *task);
static void interface_cancel(struct timeline_sleeper *sleeper);
static enum hrtimer_restart interface_wakeup(struct hrtimer *timer);
static void interface_init_sleeper(struct timeline_sleeper *sl, struct task_struct *task, struct qot_timeline *timeline);
int interface_update(struct rb_root *timeline_root, struct qot_clock_params *old_params, struct qot_clock_params *new_params);
struct qot_timeline *get_timeline(char *uuid);
ktime_t update_time(ktime_t old, struct qot_clock_params *old_params, struct qot_clock_params *new_params);

//
// function declarations
//

// hijacked system call
// absolute sleep on given timeline
asmlinkage long sys_timeline_nanosleep(char __user *timeline_id, struct timespec __user *exp_time) {
    char tlid[QOT_MAX_NAMELEN];
    struct timespec t_wake, t_now, delta;
    struct timeline_sleeper sleep_timer;
    struct qot_timeline *tl;


    // copy user data
    if(copy_from_user(tlid, timeline_id, QOT_MAX_NAMELEN) || copy_from_user(&t_wake, exp_time, sizeof(struct timespec))) {
        printk(KERN_ALERT "[sys_timeline_nanosleep] could not copy_from_user\n");
        return -EFAULT;
    }

    if(!(timespec_valid(&t_wake))) {
        printk(KERN_INFO "[sys_timeline_nanosleep] invalid timespec\n");
        return -EINVAL;
    }

    //printk(KERN_INFO "[sys_timeline_nanosleep] timeline id: %s, expiry: %ld.%lu", tlid, t_wake.tv_sec, t_wake.tv_nsec);

    // try to find the given timeline from tree
    // assume we found it for now
    tl = get_timeline(tlid);

    //printk("[sys_timeline_nanosleep] found timeline %s\n", tl->uuid);

    // hrtimer has been initialized
    
    hrtimer_init_on_stack(&sleep_timer.timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    hrtimer_set_expires(&sleep_timer.timer, timespec_to_ktime(t_wake));
    sleep_timer.expires = sleep_timer.timer._softexpires;
    interface_init_sleeper(&sleep_timer, current, tl);

    do {
        set_current_state(TASK_INTERRUPTIBLE);
        hrtimer_start_expires(&sleep_timer.timer, HRTIMER_MODE_ABS);
        if (!hrtimer_active(&sleep_timer.timer))
            sleep_timer.task = NULL;

        getnstimeofday(&t_now);
        if (likely(sleep_timer.task)) {
            //printk(KERN_INFO"[sys_timeline_nanosleep] task %d sleeping at %ld.%09lu\n", current->pid, t_now.tv_sec, t_now.tv_nsec);
            freezable_schedule();
        }

        hrtimer_cancel(&sleep_timer.timer);
    } while (sleep_timer.task && !signal_pending(sleep_timer.task));

    interface_cancel(&sleep_timer);
     __set_current_state(TASK_RUNNING);

    // get system time for comparision
    getnstimeofday(&t_now);
    printk(KERN_INFO"[sys_timeline_nanosleep] task %d woke up at %ld.%09lu\n", current->pid, t_now.tv_sec, t_now.tv_nsec);
    // check if t_now - t_wake > epsilon
    delta = timespec_sub(t_now, t_wake);
    if(timespec_compare(&delta, &epsilon) <= 0) {
        return sleep_timer.task == NULL;
    } else {
        //printk(KERN_INFO "[sys_timeline_nanosleep] task %d waited too long\n", current->pid);
        return -EBADE;
    }
}

// right now just change the global_timeline directly without a bother for checking timelines
asmlinkage long sys_set_offset(char __user *timeline_id, s64 __user *off) {
    struct qot_clock_params old_params, new_params;
    struct qot_timeline *timeline;
    char tlid[QOT_MAX_NAMELEN];
    if(copy_from_user(tlid, timeline_id, QOT_MAX_NAMELEN) || copy_from_user(&new_params.nsec, off, sizeof(s64))) {
        printk(KERN_ALERT"[sys_set_offset] cannot copy_from_user\n");
        return -EINVAL;
    }
    timeline = get_timeline(tlid);
    printk(KERN_INFO "[sys_set_offset] timeline \"%s\" offset requested %lld\n", timeline->uuid, new_params.nsec);
    old_params.nsec = timeline->nsec;
    old_params.mult = timeline->mult;
    old_params.last = timeline->last;
    
    // new_params.nsec = offset;
    new_params.mult = old_params.mult;
    new_params.last = old_params.last;
    
    printk(KERN_INFO "[sys_set_offset] change offset %lld -> %lld\n", old_params.nsec, new_params.nsec);
    timeline->nsec = new_params.nsec;
    interface_update(&timeline->event_head, &old_params, &new_params);
    return 0;
}

// does nothing for now
// only updates offset for now
ktime_t update_time(ktime_t old, struct qot_clock_params *old_params, struct qot_clock_params *new_params) {
    ktime_t new;
    s64 delta;
    delta = new_params->nsec - old_params->nsec;
    new = ns_to_ktime((u64) (ktime_to_ns(old) + delta));
    printk(KERN_INFO "[update time]: %lld -> %lld\n", old.tv64, new.tv64);
    return new;
}


// /*Sends a signal to a process*/
// static void signal_missed_deadline(struct task_struct *task) {
//     struct siginfo info;

//     memset(&info, 0, sizeof(struct siginfo));
//     info.si_signo = SIGTASKEXPIRED;
//     info.si_code = SI_KERNEL;
//     info.si_int = 0;

//     send_sig_info(SIGTASKEXPIRED, &info, task);
// }

/*Destroys a timeline node*/
static void interface_cancel(struct timeline_sleeper *sleeper) {
    printk(KERN_INFO "[interface_cancel] removing task %d timeline_sleeper from timeline %s\n", sleeper->task->pid, sleeper->timeline->uuid);
    rb_erase(&sleeper->tl_node, &sleeper->timeline->event_head);
    return;
}

/* hrtimer callback wakes up a task*/
static enum hrtimer_restart interface_wakeup(struct hrtimer *timer) {
    struct timeline_sleeper *t;
    struct task_struct *task;
    t = container_of(timer, struct timeline_sleeper, timer);
    task = t->task;
    interface_cancel(t);
    t->task = NULL;
    if(task)
        wake_up_process(task);

    return HRTIMER_NORESTART;
}

/*initializes the hrtimer sleeper structure*/
static void interface_init_sleeper(struct timeline_sleeper *sl, struct task_struct *task, struct qot_timeline *timeline)
{
    sl->timer.function = interface_wakeup;
    sl->task = task;
    sl->timeline = timeline;
    timeline_event_add(&timeline->event_head, sl);
}




// add timeline_sleeper node to specified rb tree. tree nodes are ordered on expiry time
static int timeline_event_add(struct rb_root *head, struct timeline_sleeper *sleeper) {
    struct rb_node **new = &(head->rb_node), *parent = NULL;
    int result;
    while(*new) {
        struct timeline_sleeper *this = container_of(*new, struct timeline_sleeper, tl_node);
        // order wrt expiry time
        result = ktime_compare(sleeper->expires, this->expires);
        parent = *new;
        if (result < 0)
            new = &((*new)->rb_left);
        else
            new = &((*new)->rb_right);
    }
    /* Add new node and rebalance tree. */
    rb_link_node(&sleeper->tl_node, parent, new);
    rb_insert_color(&sleeper->tl_node, head);
    return 0;
}



/* Updates hrtimer softexpires when a time change happens, called by the set_time adj_time functions*/
int interface_update(struct rb_root *timeline_root, struct qot_clock_params *old_params, struct qot_clock_params *new_params) {
    // struct timespec new_expires_time;
    // struct timespec old_expires_time;
    ktime_t new_softexpires;
    ktime_t current_sys_time;
    
    struct hrtimer *timer;
    struct timeline_sleeper *sleeping_task;
    struct task_struct *task;
    int missed_events = 0; /* Returns a non zero number incase a task has to be woken up midway the mod of the non zero number is the number of tasks which had to be woken up */
    int ret;

    struct rb_node *timeline_node = NULL;
    /*Get the current system time*/
    current_sys_time = ktime_get_real();

    timeline_node = rb_first(timeline_root);

    for (timeline_node = rb_first(timeline_root); timeline_node != NULL;) {
        sleeping_task = container_of(timeline_node, struct timeline_sleeper, tl_node);
        timer = &sleeping_task->timer;
        task = sleeping_task->task;
        // old_expires_time = ktime_to_timespec(timer->_softexpires);
        new_softexpires = update_time(timer->_softexpires, old_params, new_params);
        // new_softexpires = timespec_to_ktime(new_expires_time);
        printk(KERN_INFO "[interface_update] task %d updated: t_exp %lld -> %lld\n", task->pid, timer->_softexpires.tv64, new_softexpires.tv64);
        
        timeline_node = rb_next(timeline_node);     // change node pointer to next node for next iteration

        // check if our time has expired
        if(ktime_compare(new_softexpires, current_sys_time) <= 0) {
            // we missed our event. wake up process & let it know
            printk(KERN_INFO "[interface_update] task %d missed due to changed notion of time\n", task->pid);
            interface_wakeup(timer);            // this is a callback function. NEEDED
            missed_events++;                    // increment number of missed events
        } else {
            // reprogram timer with new value
            printk(KERN_INFO "[interface_update] task %d timer reprogrammed\n", task->pid);
            if(hrtimer_active(timer)) {     // cancel old if active
                hrtimer_cancel(timer);      
            }      
            hrtimer_init_on_stack(timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
            timer->function = interface_wakeup;
            hrtimer_set_expires(timer, new_softexpires);       // set new expiry time
            sleeping_task->expires = new_softexpires;
            ret = hrtimer_start_expires(timer, HRTIMER_MODE_ABS);   // start new values
        }
    }
    return -missed_events;
}


asmlinkage long sys_print_timeline(char __user *uuid) {
    struct rb_node *timeline_node = NULL;
    struct timeline_sleeper *sleeping_task;
    struct qot_timeline *timeline;
    char tlid[QOT_MAX_NAMELEN]; 
    /*Get the current system time*/
    if(copy_from_user(tlid, uuid, QOT_MAX_NAMELEN)) {
        printk(KERN_ALERT"[sys_print_timeline] unable to copy_from_user\n");
        return -EFAULT;
    }

    timeline = get_timeline(uuid);
    printk(KERN_INFO "Displaying the global timeline\n");
    printk(KERN_INFO "PID\tExpiry Time\n");
    timeline_node = rb_first(&timeline->event_head);
    while(timeline_node != NULL)
    {
        sleeping_task = container_of(timeline_node, struct timeline_sleeper, tl_node);
        printk(KERN_INFO "%d\t%lld\n", sleeping_task->task->pid, ktime_to_ns(sleeping_task->timer._softexpires)); 
        timeline_node = rb_next(timeline_node);
    }
    return 0;
}

// this is a dummy for now. We interface with Andrew later
struct qot_timeline *get_timeline(char *uuid) {
    return &global_timeline;
}

//
// module registration functions
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
    sys_call_table[__NR_qot_custom1] = &sys_set_offset;
    sys_call_table[__NR_qot_custom2] = &sys_print_timeline;


    // test arena

    strncpy(global_timeline.uuid, "local_time", QOT_MAX_NAMELEN);
    global_timeline.nsec = 0;
    global_timeline.mult = 1;
    global_timeline.event_head = RB_ROOT;
    spin_lock_init(&global_timeline.rb_lock);


    printk(KERN_INFO"[timeline] module loaded\n");
    return 0;
}

// module exit function
static void __exit timeline_exit(void) {
    sys_call_table[__NR_qot_custom0] =  old_custom0;
    sys_call_table[__NR_qot_custom1] = old_custom1;
    sys_call_table[__NR_qot_custom2] = old_custom2;
    sys_call_table[__NR_qot_custom3] = old_custom3;
    sys_call_table[__NR_qot_custom4] = old_custom4;
    sys_call_table[__NR_qot_custom5] = old_custom5;
    sys_call_table[__NR_qot_custom6] = old_custom6;
    sys_call_table[__NR_qot_custom7] = old_custom7;

    printk(KERN_INFO"[timeline] module unloaded\n");

}

module_init(timeline_init);
module_exit(timeline_exit);
