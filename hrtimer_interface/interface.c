#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/unistd.h>
#include <linux/cpumask.h>
#include <linux/freezer.h>
#include "interface.h"


/*Sends a signal to a process*/
static void interface_signal(struct task_struct *task)
{
	return;
}

/*Destroys a timeline node*/
static void interface_cancel(struct rb_node *timeline_node)
{
	return;
}

/* hrtimer callback wakes up a task*/
static enum hrtimer_restart interface_wakeup(struct hrtimer *timer)
{
	struct hrtimer_sleeper *t = container_of(timer, struct hrtimer_sleeper, timer);
	struct task_struct *task = t->task;
	interface_cancel(&timer->tl_node);
	t->task = NULL;
	if (task)
		wake_up_process(task);

	return HRTIMER_NORESTART;
}

/*initializes the hrtimer sleeper structure*/
static void interface_init_sleeper(struct hrtimer_sleeper *sl, struct task_struct *task)
{
	sl->timer.function = interface_wakeup;
	sl->task = task;
}

/* Calls HR timer to put task to sleep */
int __sched interface_sleep(struct timespec *sleep_time)
{
	struct hrtimer_sleeper sleep_timer;
	hrtimer_init_on_stack(&sleep_timer.timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	hrtimer_set_expires(&sleep_timer.timer, timespec_to_ktime(*sleep_time));
	interface_init_sleeper(&sleep_timer, current);
	do {
		set_current_state(TASK_INTERRUPTIBLE);
		hrtimer_start_expires(&sleep_timer.timer, HRTIMER_MODE_ABS);
		if (!hrtimer_active(&sleep_timer.timer))
			sleep_timer.task = NULL;

		if (likely(sleep_timer.task))
			freezable_schedule();

		hrtimer_cancel(&sleep_timer.timer);
	} while (sleep_timer.task && !signal_pending(sleep_timer.task));
	__set_current_state(TASK_RUNNING);
	return sleep_timer.task == NULL;
}
/*Changes the timeline tasks on the hrtimer rb tree*/
static void interface_reconfigure(struct hrtimer *timer)
{
	struct hrtimer_sleeper *sleeper;
	struct rb_node timeline_node;
	struct task_struct *task;
	sleeper = container_of(timer, struct hrtimer_sleeper, timer);
	task = sleeper->task;
	timeline_node = timer->tl_node;
	hrtimer_cancel(timer);
	hrtimer_init_on_stack(timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	interface_init_sleeper(sleeper, task);
	timer->tl_node = timeline_node;
	hrtimer_start_expires(timer, HRTIMER_MODE_ABS);
	return;
}

/* Updates hrtimer softexpires when a time change happens, called by the set_time adj_time functions*/
int interface_update(struct timespec (*get_new_time)(struct timespec *), struct rb_root *timeline_root)
{
	struct timespec new_expires_time;
	struct timespec old_expires_time;
	ktime_t new_softexpires;
	ktime_t current_sys_time;
	
	struct hrtimer *timer;
	struct hrtimer_sleeper *sleeping_task;
	int retval = 0;
	int ret_flag = 0; /* Returns a non zero number incase a task has to be woken up midway the mod of the non zero number is the number of tasks which had to be woken up */

	struct task_struct *task;

	struct rb_node *timeline_node = NULL;
	struct rb_node *next_node = NULL;
	/*Get the current system time*/
	current_sys_time = ktime_get_real();

	timeline_node = rb_first(timeline_root);
	while(timeline_node != NULL)
	{
		timer = container_of(timeline_node, struct hrtimer, tl_node );
		sleeping_task = container_of(timer, struct hrtimer_sleeper, timer);
		task = sleeping_task->task;
		old_expires_time = ktime_to_timespec(timer->_softexpires);
		new_expires_time = get_new_time(&old_expires_time);

		new_softexpires = timespec_to_ktime(new_expires_time);
		
		/* Send a signal to the user */
		retval = ktime_compare(new_softexpires, current_sys_time);
		if(retval <= 0)
		{
			interface_signal(task);
			interface_cancel(&timer->tl_node);
			hrtimer_cancel(timer);
			wake_up_process(task);
			ret_flag--;
		}
		else
		{
			hrtimer_set_expires(timer, new_softexpires);
			interface_reconfigure(timer);
		}
		next_node = rb_next(timeline_node);
		timeline_node = next_node;
	}
	
	return ret_flag;
}

static int init_interface(void)
{

    return 0;
}

static void exit_interface(void)
{
    return;

}

module_init(init_interface);
module_exit(exit_interface);
MODULE_LICENSE("GPL");