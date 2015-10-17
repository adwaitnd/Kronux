/*
 *  include/linux/timeline_hrtimer.h
 *
 *  timeline_hrtimers - High-resolution kernel timers
 *
 *   Copyright(C) 2005, Thomas Gleixner <tglx@linutronix.de>
 *   Copyright(C) 2005, Red Hat, Inc., Ingo Molnar
 *
 *  data type definitions, declarations, prototypes
 *
 *  Started by: Thomas Gleixner and Ingo Molnar
 *
 *  For licencing details see kernel-base/COPYING
 */
#ifdef CONFIG_TIMELINE
#ifndef _LINUX_TIMELINE_HRTIMER_H
#define _LINUX_TIMELINE_HRTIMER_H

#include <linux/rbtree.h>
#include <linux/ktime.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/percpu.h>
#include <linux/timer.h>
#include <linux/timerqueue.h>
#include <linux/hrtimer.h>

struct timeline_hrtimer_clock_base;
struct timeline_hrtimer_cpu_base;

/*
 * Mode arguments of xxx_timeline_hrtimer functions:
 */
enum timeline_hrtimer_mode {
	TIMELINE_HRTIMER_MODE_ABS = 0x0,		/* Time value is absolute */
	TIMELINE_HRTIMER_MODE_REL = 0x1,		/* Time value is relative to now */
	TIMELINE_HRTIMER_MODE_PINNED = 0x02,	/* Timer is bound to CPU */
	TIMELINE_HRTIMER_MODE_ABS_PINNED = 0x02,
	TIMELINE_HRTIMER_MODE_REL_PINNED = 0x03,
};

/*
 * Return values for the callback function
 */
enum timeline_hrtimer_restart {
	TIMELINE_HRTIMER_NORESTART,	/* Timer is not restarted */
	TIMELINE_HRTIMER_RESTART,	/* Timer must be restarted */
};

/*
 * Values to track state of the timer
 *
 * Possible states:
 *
 * 0x00		inactive
 * 0x01		enqueued into rbtree
 * 0x02		callback function running
 * 0x04		timer is migrated to another cpu
 *
 * Special cases:
 * 0x03		callback function running and enqueued
 *		(was requeued on another CPU)
 * 0x05		timer was migrated on CPU hotunplug
 *
 * The "callback function running and enqueued" status is only possible on
 * SMP. It happens for example when a posix timer expired and the callback
 * queued a signal. Between dropping the lock which protects the posix timer
 * and reacquiring the base lock of the timeline_hrtimer, another CPU can deliver the
 * signal and rearm the timer. We have to preserve the callback running state,
 * as otherwise the timer could be removed before the softirq code finishes the
 * the handling of the timer.
 *
 * The TIMELINE_HRTIMER_STATE_ENQUEUED bit is always or'ed to the current state
 * to preserve the TIMELINE_HRTIMER_STATE_CALLBACK in the above scenario. This
 * also affects TIMELINE_HRTIMER_STATE_MIGRATE where the preservation is not
 * necessary. TIMELINE_HRTIMER_STATE_MIGRATE is cleared after the timer is
 * enqueued on the new cpu.
 *
 * All state transitions are protected by cpu_base->lock.
 */
#define TIMELINE_HRTIMER_STATE_INACTIVE	0x00
#define TIMELINE_HRTIMER_STATE_ENQUEUED	0x01
#define TIMELINE_HRTIMER_STATE_CALLBACK	0x02
#define TIMELINE_HRTIMER_STATE_MIGRATE	0x04

/**
 * struct timeline_hrtimer - the basic timeline_hrtimer structure
 * @node:	timerqueue node, which also manages node.expires,
 *		the absolute expiry time in the timeline_hrtimers internal
 *		representation. The time is related to the clock on
 *		which the timer is based. Is setup by adding
 *		slack to the _softexpires value. For non range timers
 *		identical to _softexpires.
 * @_softexpires: the absolute earliest expiry time of the timeline_hrtimer.
 *		The time which was given as expiry time when the timer
 *		was armed.
 * @function:	timer expiry callback function
 * @base:	pointer to the timer base (per cpu and per clock)
 * @state:	state information (See bit values above)
 * @start_pid: timer statistics field to store the pid of the task which
 *		started the timer
 * @start_site:	timer statistics field to store the site where the timer
 *		was started
 * @start_comm: timer statistics field to store the name of the process which
 *		started the timer
 *
 * The timeline_hrtimer structure must be initialized by timeline_hrtimer_init()
 */
struct timeline_hrtimer {
	struct timerqueue_node		node;
	ktime_t				_softexpires;
	enum timeline_hrtimer_restart		(*function)(struct timeline_hrtimer *);
	struct timeline_hrtimer_clock_base	*base;
	unsigned long			state;
#ifdef CONFIG_TIMER_STATS
	int				start_pid;
	void				*start_site;
	char				start_comm[16];
#endif
};

/**
 * struct timeline_hrtimer_sleeper - simple sleeper structure
 * @timer:	embedded timer structure
 * @task:	task to wake up
 *
 * task is set to NULL, when the timer expires.
 */
struct timeline_hrtimer_sleeper {
	struct timeline_hrtimer timer;
	struct task_struct *task;
};

/**
 * struct timeline_hrtimer_clock_base - the timer base for a specific clock
 * @cpu_base:		per cpu clock base
 * @index:		clock type index for per_cpu support when moving a
 *			timer to a base on another cpu.
 * @clockid:		clock id for per_cpu support
 * @active:		red black tree root node for the active timers
 * @resolution:		the resolution of the clock, in nanoseconds
 * @get_time:		function to retrieve the current time of the clock
 * @softirq_time:	the time when running the timeline_hrtimer queue in the softirq
 * @offset:		offset of this clock to the monotonic base
 */
struct timeline_hrtimer_clock_base {
	struct timeline_hrtimer_cpu_base	*cpu_base;
	int			index;
	clockid_t		clockid;
	struct timerqueue_head	active;
	ktime_t			resolution;
	ktime_t			(*get_time)(void);
	ktime_t			softirq_time;
	ktime_t			offset;
};

enum  timeline_hrtimer_base_type {
	TIMELINE_HRTIMER_BASE_MONOTONIC,
	TIMELINE_HRTIMER_BASE_REALTIME,
	TIMELINE_HRTIMER_BASE_BOOTTIME,
	TIMELINE_HRTIMER_BASE_TAI,
	TIMELINE_HRTIMER_MAX_CLOCK_BASES,
};

/*
 * struct timeline_hrtimer_cpu_base - the per cpu clock bases
 * @lock:		lock protecting the base and associated clock bases
 *			and timers
 * @cpu:		cpu number
 * @active_bases:	Bitfield to mark bases with active timers
 * @clock_was_set:	Indicates that clock was set from irq context.
 * @expires_next:	absolute time of the next event which was scheduled
 *			via clock_set_next_event()
 * @in_hrtirq:		timeline_hrtimer_interrupt() is currently executing
 * @hres_active:	State of high resolution mode
 * @hang_detected:	The last timeline_hrtimer interrupt detected a hang
 * @nr_events:		Total number of timeline_hrtimer interrupt events
 * @nr_retries:		Total number of timeline_hrtimer interrupt retries
 * @nr_hangs:		Total number of timeline_hrtimer interrupt hangs
 * @max_hang_time:	Maximum time spent in timeline_hrtimer_interrupt
 * @clock_base:		array of clock bases for this cpu
 */
struct timeline_hrtimer_cpu_base {
	raw_spinlock_t			lock;
	unsigned int			cpu;
	unsigned int			active_bases;
	unsigned int			clock_was_set;
#ifdef CONFIG_HIGH_RES_TIMERS
	ktime_t				expires_next;
	int				in_hrtirq;
	int				hres_active;
	int				hang_detected;
	unsigned long			nr_events;
	unsigned long			nr_retries;
	unsigned long			nr_hangs;
	ktime_t				max_hang_time;
#endif
	struct timeline_hrtimer_clock_base	clock_base[TIMELINE_HRTIMER_MAX_CLOCK_BASES];
};

static inline void timeline_hrtimer_set_expires(struct timeline_hrtimer *timer, ktime_t time)
{
	timer->node.expires = time;
	timer->_softexpires = time;
}

static inline void timeline_hrtimer_set_expires_range(struct timeline_hrtimer *timer, ktime_t time, ktime_t delta)
{
	timer->_softexpires = time;
	timer->node.expires = ktime_add_safe(time, delta);
}

static inline void timeline_hrtimer_set_expires_range_ns(struct timeline_hrtimer *timer, ktime_t time, unsigned long delta)
{
	timer->_softexpires = time;
	timer->node.expires = ktime_add_safe(time, ns_to_ktime(delta));
}

static inline void timeline_hrtimer_set_expires_tv64(struct timeline_hrtimer *timer, s64 tv64)
{
	timer->node.expires.tv64 = tv64;
	timer->_softexpires.tv64 = tv64;
}

static inline void timeline_hrtimer_add_expires(struct timeline_hrtimer *timer, ktime_t time)
{
	timer->node.expires = ktime_add_safe(timer->node.expires, time);
	timer->_softexpires = ktime_add_safe(timer->_softexpires, time);
}

static inline void timeline_hrtimer_add_expires_ns(struct timeline_hrtimer *timer, u64 ns)
{
	timer->node.expires = ktime_add_ns(timer->node.expires, ns);
	timer->_softexpires = ktime_add_ns(timer->_softexpires, ns);
}

static inline ktime_t timeline_hrtimer_get_expires(const struct timeline_hrtimer *timer)
{
	return timer->node.expires;
}

static inline ktime_t timeline_hrtimer_get_softexpires(const struct timeline_hrtimer *timer)
{
	return timer->_softexpires;
}

static inline s64 timeline_hrtimer_get_expires_tv64(const struct timeline_hrtimer *timer)
{
	return timer->node.expires.tv64;
}
static inline s64 timeline_hrtimer_get_softexpires_tv64(const struct timeline_hrtimer *timer)
{
	return timer->_softexpires.tv64;
}

static inline s64 timeline_hrtimer_get_expires_ns(const struct timeline_hrtimer *timer)
{
	return ktime_to_ns(timer->node.expires);
}

static inline ktime_t timeline_hrtimer_expires_remaining(const struct timeline_hrtimer *timer)
{
	return ktime_sub(timer->node.expires, timer->base->get_time());
}

#ifdef CONFIG_HIGH_RES_TIMERS
struct clock_event_device;

extern void timeline_hrtimer_interrupt(struct clock_event_device *dev);

/*
 * In high resolution mode the time reference must be read accurate
 */
static inline ktime_t timeline_hrtimer_cb_get_time(struct timeline_hrtimer *timer)
{
	return timer->base->get_time();
}

static inline int timeline_hrtimer_is_hres_active(struct timeline_hrtimer *timer)
{
	return timer->base->cpu_base->hres_active;
}

extern void timeline_hrtimer_peek_ahead_timers(void);

/*
 * The resolution of the clocks. The resolution value is returned in
 * the clock_getres() system call to give application programmers an
 * idea of the (in)accuracy of timers. Timer values are rounded up to
 * this resolution values.
 */
//# define HIGH_RES_NSEC		1
//# define KTIME_HIGH_RES		(ktime_t) { .tv64 = HIGH_RES_NSEC }
//# define MONOTONIC_RES_NSEC	HIGH_RES_NSEC
//# define KTIME_MONOTONIC_RES	KTIME_HIGH_RES

//extern void clock_was_set_delayed(void);

#else

//# define MONOTONIC_RES_NSEC	LOW_RES_NSEC
//# define KTIME_MONOTONIC_RES	KTIME_LOW_RES

static inline void timeline_hrtimer_peek_ahead_timers(void) { }

/*
 * In non high resolution mode the time reference is taken from
 * the base softirq time variable.
 */
static inline ktime_t timeline_hrtimer_cb_get_time(struct timeline_hrtimer *timer)
{
	return timer->base->softirq_time;
}

static inline int timeline_hrtimer_is_hres_active(struct timeline_hrtimer *timer)
{
	return 0;
}

//static inline void clock_was_set_delayed(void) { }

#endif

//extern void clock_was_set(void);
#ifdef CONFIG_TIMERFD
//extern void timerfd_clock_was_set(void);
#else
//static inline void timerfd_clock_was_set(void) { }
#endif
extern void timeline_hrtimers_resume(void);

DECLARE_PER_CPU(struct tick_device, tick_cpu_timeline_device);


/* Exported timer functions: */

/* Initialize timers: */
extern void timeline_hrtimer_init(struct timeline_hrtimer *timer, clockid_t which_clock,
			 enum timeline_hrtimer_mode mode);

#ifdef CONFIG_DEBUG_OBJECTS_TIMERS
extern void timeline_hrtimer_init_on_stack(struct timeline_hrtimer *timer, clockid_t which_clock,
				  enum timeline_hrtimer_mode mode);

extern void destroy_timeline_hrtimer_on_stack(struct timeline_hrtimer *timer);
#else
static inline void timeline_hrtimer_init_on_stack(struct timeline_hrtimer *timer,
					 clockid_t which_clock,
					 enum timeline_hrtimer_mode mode)
{
	timeline_hrtimer_init(timer, which_clock, mode);
}
static inline void destroy_timeline_hrtimer_on_stack(struct timeline_hrtimer *timer) { }
#endif

/* Basic timer operations: */
extern int timeline_hrtimer_start(struct timeline_hrtimer *timer, ktime_t tim,
			 const enum timeline_hrtimer_mode mode);
extern int timeline_hrtimer_start_range_ns(struct timeline_hrtimer *timer, ktime_t tim,
			unsigned long range_ns, const enum timeline_hrtimer_mode mode);
extern int
__timeline_hrtimer_start_range_ns(struct timeline_hrtimer *timer, ktime_t tim,
			 unsigned long delta_ns,
			 const enum timeline_hrtimer_mode mode, int wakeup);

extern int timeline_hrtimer_cancel(struct timeline_hrtimer *timer);
extern int timeline_hrtimer_try_to_cancel(struct timeline_hrtimer *timer);

static inline int timeline_hrtimer_start_expires(struct timeline_hrtimer *timer,
						enum timeline_hrtimer_mode mode)
{
	unsigned long delta;
	ktime_t soft, hard;
	soft = timeline_hrtimer_get_softexpires(timer);
	hard = timeline_hrtimer_get_expires(timer);
	delta = ktime_to_ns(ktime_sub(hard, soft));
	return timeline_hrtimer_start_range_ns(timer, soft, delta, mode);
}

static inline int timeline_hrtimer_restart(struct timeline_hrtimer *timer)
{
	return timeline_hrtimer_start_expires(timer, TIMELINE_HRTIMER_MODE_ABS);
}

/* Query timers: */
extern ktime_t timeline_hrtimer_get_remaining(const struct timeline_hrtimer *timer);
extern int timeline_hrtimer_get_res(const clockid_t which_clock, struct timespec *tp);

extern ktime_t timeline_hrtimer_get_next_event(void);

/*
 * A timer is active, when it is enqueued into the rbtree or the
 * callback function is running or it's in the state of being migrated
 * to another cpu.
 */
static inline int timeline_hrtimer_active(const struct timeline_hrtimer *timer)
{
	return timer->state != TIMELINE_HRTIMER_STATE_INACTIVE;
}

/*
 * Helper function to check, whether the timer is on one of the queues
 */
static inline int timeline_hrtimer_is_queued(struct timeline_hrtimer *timer)
{
	return timer->state & TIMELINE_HRTIMER_STATE_ENQUEUED;
}

/*
 * Helper function to check, whether the timer is running the callback
 * function
 */
static inline int timeline_hrtimer_callback_running(struct timeline_hrtimer *timer)
{
	return timer->state & TIMELINE_HRTIMER_STATE_CALLBACK;
}

/* Forward a timeline_hrtimer so it expires after now: */
extern u64
timeline_hrtimer_forward(struct timeline_hrtimer *timer, ktime_t now, ktime_t interval);

/* Forward a timeline_hrtimer so it expires after the timeline_hrtimer's current now */
static inline u64 timeline_hrtimer_forward_now(struct timeline_hrtimer *timer,
				      ktime_t interval)
{
	return timeline_hrtimer_forward(timer, timer->base->get_time(), interval);
}

/* Precise sleep: */
extern long timeline_hrtimer_nanosleep(struct timespec *rqtp,
			      struct timespec __user *rmtp,
			      const enum timeline_hrtimer_mode mode,
			      const clockid_t clockid);
extern long timeline_hrtimer_nanosleep_restart(struct restart_block *restart_block);

extern void timeline_hrtimer_init_sleeper(struct timeline_hrtimer_sleeper *sl,
				 struct task_struct *tsk);

extern int schedule_timeline_hrtimeout_range(ktime_t *expires, unsigned long delta,
						const enum timeline_hrtimer_mode mode);
extern int schedule_timeline_hrtimeout_range_clock(ktime_t *expires,
		unsigned long delta, const enum timeline_hrtimer_mode mode, int clock);
extern int schedule_timeline_hrtimeout(ktime_t *expires, const enum timeline_hrtimer_mode mode);

/* Soft interrupt function to run the timeline_hrtimer queues: */
extern void timeline_hrtimer_run_queues(void);
extern void timeline_hrtimer_run_pending(void);

/* Bootup initialization: */
extern void __init timeline_hrtimers_init(void);

/* Show pending timers: */
extern void sysrq_timer_list_show(void);

#endif
#ifdef CONFIG_TIMELINE
//Code added by Sandeep D'souza - Dummy Timeline Interrupt
//extern void timeline_interrupt(struct clock_event_device *dev);
#endif
#endif

