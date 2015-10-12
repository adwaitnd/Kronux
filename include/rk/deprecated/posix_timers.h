#ifndef __RK_POSIX_TIMERS_H__
#define __RK_POSIX_TIMERS_H__

#ifdef __KERNEL__
#include <asm/siginfo.h>
#else
#include <signal.h>

/*   # i f n def __USE_POSIX199309  */
#ifndef __clockid_t_defined
struct  itimerspec {
	struct  timespec it_interval;    /* timer period */
	struct  timespec it_value;       /* timer expiration */
};
#endif
#endif

/* # i f n def __USE_POSIX199309 */


#define CLOCK_REALTIME		0
#define DELAYTIMER_MAX		0x7fffffff
#define MAXPOSIXTIMERS		32

int clock_settime (clockid_t clock_id, __const struct timespec *tp);
int clock_gettime (clockid_t clock_id, struct timespec *tp);
int clock_getres  (clockid_t clock_id, struct timespec *res);
int timer_create  (clockid_t clock_id, struct sigevent *evp, 
							  timer_t *timerid);
int timer_delete  (timer_t timerid);
int timer_settime (timer_t timerid, int flags, 
							  __const struct itimerspec *value,
							  struct itimerspec *ovalue);
int timer_gettime (timer_t timerid, struct itimerspec *value);
int timer_getoverrun(timer_t timerid);

typedef struct posix_timer {
	struct rk_timer *rkt;	/* RK timer pointer */
	struct sigevent ev;		/* Signal event description */
	struct itimerspec its;	/* Interval specification */
	struct task_struct *p;	/* Process to signal */
	int pending;				/* Number of signals pending for this timer */
} posix_timer;

#endif
