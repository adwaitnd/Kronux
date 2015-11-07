#ifndef _TIMELNE_INTERFACE_H
#define _TIMELINE_INTERFACE_H
#include <linux/hrtimer.h>
extern int interface_sleep(struct timespec *sleep_time);
extern int interface_update(struct timespec (*get_new_time)(struct timespec *), struct rb_root* timeline_root);
#endif