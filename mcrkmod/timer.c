/* 
 * Copyright (C) 2000 TimeSys Corporation
 *
 * This is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 * This file is derived from software distributed under the following terms:
 *
 * Real-Time and Multimedia Systems Laboratory
 * Copyright (c) 2000-2013 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Real-Time and Multimedia Systems Laboratory
 *  Attn: Prof. Raj Rajkumar
 *  Electrical and Computer Engineering, and Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 *  or via email to raj@ece.cmu.edu
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/* 
 * timer.c	: Contains the functions that manage all the timers
 * 		  used by RK including enforcement timers and replenishment timers
 * 		  and the way that these timers are maintained and multiplexed 
 * 		  on the single per-cpu hrtimer
 */

#include <rk/rk_mc.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <linux/cpumask.h>
#include <linux/rmap.h>
#include <asm/percpu.h>

#define RK_MIN_TIMER		1000LL			/* Corresponds to 1 usecs	*/
#define TIMER_DELTA		10000LL			/* Corresponds to 10 usecs	*/
#define MAX_TIMERS_PER_ISR	1000			/* Max timers in 1 ISR		*/


static enum hrtimer_restart rk_timer_isr(struct hrtimer *timer);

cpu_tick_data_t		global_time;
int			time_in_seconds;
int			time_in_minutes;

#ifndef RK_GLOBAL_SCHED

DEFINE_PER_CPU(struct rk_vtimer, rk_virtual_timer);
DEFINE_PER_CPU(struct list_head, rk_online_timer_root);
DEFINE_PER_CPU(raw_spinlock_t, rk_timer_lock);
DEFINE_PER_CPU(cpu_tick_data_t, rk_overflow);
#define ptr_rk_virtual_timer(cpunum) (&per_cpu(rk_virtual_timer, (cpunum)))
#define ptr_rk_online_timer_root(cpunum) (&per_cpu(rk_online_timer_root, (cpunum)))
#define ptr_rk_timer_lock(cpunum) (&per_cpu(rk_timer_lock, (cpunum)))
#define var_rk_overflow(cpunum) (per_cpu(rk_overflow, (cpunum)))
#define RK_HRTIMER_MODE HRTIMER_MODE_ABS_PINNED

#else

struct rk_vtimer rk_virtual_timer;
struct list_head rk_online_timer_root;
raw_spinlock_t rk_timer_lock;
cpu_tick_data_t rk_overflow;
#define ptr_rk_virtual_timer(cpunum) (&rk_virtual_timer)
#define ptr_rk_online_timer_root(cpunum) (&rk_online_timer_root)
#define ptr_rk_timer_lock(cpunum) (&rk_timer_lock)
#define var_rk_overflow(cpunum) (rk_overflow)
#define RK_HRTIMER_MODE HRTIMER_MODE_ABS

#endif


// Initialize the timer management subsystem
void rk_timer_init(void)
{
 	int cpunum = 0;

#ifndef RK_GLOBAL_SCHED
	for_each_online_cpu(cpunum) {
		cpumask_t cpumask;
		cpus_clear(cpumask);
		cpu_set(cpunum,cpumask);
		if (set_cpus_allowed_ptr(current, &cpumask) != 0) {
			printk("rk_timer_init: set_cpus_allowed_ptr error\n");
			return;
		}

		printk("Timer: CPU %d\n", raw_smp_processor_id());
		hrtimer_init(&(ptr_rk_virtual_timer(cpunum)->t), CLOCK_MONOTONIC, RK_HRTIMER_MODE);

		ptr_rk_virtual_timer(cpunum)->t.function = rk_timer_isr;
		ptr_rk_virtual_timer(cpunum)->cpunum = cpunum; 

		INIT_LIST_HEAD(ptr_rk_online_timer_root(cpunum));

		raw_spin_lock_init(ptr_rk_timer_lock(cpunum));
	}
#else
	hrtimer_init(&(ptr_rk_virtual_timer(cpunum)->t), CLOCK_MONOTONIC, RK_HRTIMER_MODE);

	ptr_rk_virtual_timer(cpunum)->t.function = rk_timer_isr;
	ptr_rk_virtual_timer(cpunum)->cpunum = cpunum; 

	INIT_LIST_HEAD(ptr_rk_online_timer_root(cpunum));

	raw_spin_lock_init(ptr_rk_timer_lock(cpunum));
#endif
} 


// Clean up the timer management subsystem
void rk_timer_cleanup(void)
{
#ifndef RK_GLOBAL_SCHED
 	int cpunum;
	for_each_online_cpu(cpunum) {
		hrtimer_cancel(&(ptr_rk_virtual_timer(cpunum)->t));
  	}
#else
	hrtimer_cancel(&(ptr_rk_virtual_timer(0)->t));
#endif
}


// Creates the timer structure for a hardware timer 
rk_timer_t rk_timer_create(void) 
{
  	rk_timer_t tmr;
  
	// Allocate a new timer, since no free timers exist
	tmr = kmalloc(sizeof(struct rk_timer), GFP_ATOMIC);
	if (!tmr) {
		printk(KERN_CRIT "RK: Could not allocate memory for timers\n");
		return NULL;
	}
	memset(tmr, 0, sizeof(struct rk_timer));
	tmr->reserve_link = NULL;

  	return tmr;
}

/*
 * Adds a timer to the list of online timers on a specific cpu
 *
 * Should be called with rk_timer_lock held
 */
void __rk_timer_add_cpu(rk_timer_t tmr, int cpunum)
{
  	struct list_head *head;
  	rk_timer_t temp;

  	if (!tmr) {
		printk("RK: rk_timer_add: NULL Timer.\n");
		return;
  	}

  	if (tmr->tmr_tmr.next || tmr->tmr_tmr.prev) {
		printk("RK: rk_timer_add: Adding an uninitialized link, probably duplicate timer the old type seems to be %d.\n", tmr->tmr_type);
		//dump_stack();
		return;
  	}
  
  	head = ptr_rk_online_timer_root(cpunum)->next;
  	while (head != ptr_rk_online_timer_root(cpunum)) {
		temp = list_entry(head, struct rk_timer, tmr_tmr);
		if (!temp) {
			printk(KERN_ERR"RK: rk_timer_add : NULL timer is present in the list of online timers.\n");
		}
		else {
			if ((tmr->overflow <= temp->overflow) && (tmr->tmr_expire < temp->tmr_expire)) {
				// Found the place in the list where we must insert the new timer
				break;
			}
			head = head -> next;
		}
	}
  	rk_list_add(&tmr->tmr_tmr, head->prev);
}
	
// Adds a timer to the list of online timers
void rk_timer_add_cpu(rk_timer_t tmr, int cpunum)
{
	unsigned long flags;
	raw_spin_lock_irqsave(ptr_rk_timer_lock(cpunum), flags);

	__rk_timer_add_cpu(tmr, cpunum);

	raw_spin_unlock_irqrestore(ptr_rk_timer_lock(cpunum), flags);
}

void adjust_timer(rk_timer_t tmr, cpu_tick_data_t time_to_be_added, int cpunum)
{
	cpu_tick_data_t	check;

  	check = tmr->tmr_expire + time_to_be_added;
  	if ((check < time_to_be_added) && (check < tmr->tmr_expire)) {
		tmr->overflow = var_rk_overflow(cpunum) + 1;
	}
	else {
		tmr->overflow = var_rk_overflow(cpunum);
	}

 	tmr->tmr_expire = check;
}

// Sets the hardware timer to go off at a specified time
void __rk_update_hw_timer_cpu(struct rk_timer *tmr, int cpunum)
{
 	cpu_tick_data_t now, val;
  	ktime_t delta, nowhr;
  
  	rk_rdtsc(&now);
 
#ifndef RK_GLOBAL_SCHED
	if (cpunum != raw_smp_processor_id()) {
		printk("rk_update_hw_timer_cpu: ERROR - cpunum:%d, curcpu:%d, type:%d\n", cpunum, raw_smp_processor_id(), tmr->tmr_type);
		return;
	}
#endif
 	if (tmr == NULL) {
		printk("rk_update_hw_timer: Called with a NULL timer\n");
		return;
  	}
  	if (tmr != NULL && tmr->tmr_expire < now) {
		// We are setting a timer in the past, do the best that we can
		val = RK_MIN_TIMER;
	}
	else {
		val = (tmr->tmr_expire - now);
		if (val < RK_MIN_TIMER) val = RK_MIN_TIMER;
	}

	delta = ns_to_ktime((u64)(val));
	nowhr = ptr_rk_virtual_timer(cpunum)->t.base->get_time();
	ptr_rk_virtual_timer(cpunum)->t.node.expires = nowhr;
	ptr_rk_virtual_timer(cpunum)->t._softexpires = nowhr;
	
	hrtimer_forward(&(ptr_rk_virtual_timer(cpunum)->t), nowhr, delta);

	// Note: Do not check hrtimer_callback_running, and use HRTIMER_NORESTART in rk_timer_isr()
	// - When we use HRTIMER_RESTART, __run_hrtimer() checks whether enqueuing happened 
	//   while serving timer handler (rk_timer_isr). 
	//   This is why we checked if the timer handler is running, using hrtimer_callback_running().
	// - However, it turned out that hrtimer_callback_running() is not reliable 
	//   when it is called by other CPUs. This may cause breaking the hrtimer constraint.
	// - When we use HRTIMER_NORESTART, we can avoid this issue. 
	//   There's no extra overhead for calling hrtimer_start() directly, 
	//   because reprogramming will eventually happen only once when the timer handler is running. 
	//if (!hrtimer_callback_running(&(ptr_rk_virtual_timer(cpunum)->t))) {
	hrtimer_start(&(ptr_rk_virtual_timer(cpunum)->t), ptr_rk_virtual_timer(cpunum)->t.node.expires, RK_HRTIMER_MODE);
  	//}

	//printk("U: %llu\n", ptr_rk_virtual_timer(cpunum)->_expires.tv64);
}

void rk_update_hw_timer_cpu(struct rk_timer *tmr, int cpunum)
{
	unsigned long flags;
	raw_spin_lock_irqsave(ptr_rk_timer_lock(cpunum), flags);

	__rk_update_hw_timer_cpu(tmr, cpunum);

	raw_spin_unlock_irqrestore(ptr_rk_timer_lock(cpunum), flags);
}


/* The interrupt service routine for the hrtimer					*/
static enum hrtimer_restart rk_timer_isr(struct hrtimer *timer)
{	
  	unsigned long		flags;
  	struct list_head	*head;
  	struct list_head	*next_timer;

  	cpu_tick_data_t 	now;
  	cpu_tick_data_t	 	period;
  	rk_timer_t		temp;

  	int			number_of_timers_expired = 0;
	int 			cpunum;

  	rk_rdtsc(&now);	

#ifndef RK_GLOBAL_SCHED
	cpunum = container_of(timer, struct rk_vtimer, t)->cpunum;
	if (cpunum < 0 || cpunum > num_cpus) {
		printk("rk_timer_isr: cpunum error %d\n", cpunum);
		return HRTIMER_NORESTART;
	}

	if (cpunum != raw_smp_processor_id()) {
		printk("rk_timer_isr: timer cpu:%d, cur cpu:%d\n", cpunum, raw_smp_processor_id());
		return HRTIMER_NORESTART;
	}
#else
	cpunum = 0;
#endif

	//printk("rk_timer_isr: enter\n");
	raw_spin_lock_irqsave(ptr_rk_timer_lock(cpunum), flags);
	head = ptr_rk_online_timer_root(cpunum)->next;

	while (head != ptr_rk_online_timer_root(cpunum)) {
		if (number_of_timers_expired > MAX_TIMERS_PER_ISR) {
			printk(KERN_CRIT "BUG: We are stuck on %d\n", raw_smp_processor_id());
			goto error_spin_unlock;
		}	

		temp = list_entry(head, struct rk_timer, tmr_tmr);
		if (temp == NULL) {
			printk("Null Timer Expired\n");
			continue;
		}
		if (head == NULL) {
			printk("Head Timer is NULL\n");
			continue;
		}
		next_timer = head->next;	

 		/* 
		 * Check if timer will expire within TIMER_DELTA
		 */
		if ((now + TIMER_DELTA) >= temp->tmr_expire) {  
			number_of_timers_expired ++;

			// This timer has expired
			if (temp->overflow > var_rk_overflow(cpunum)) {
				var_rk_overflow(cpunum) = temp->overflow;
			}

			// Check if the next entry is valid 
			if (head->next == NULL || head->prev == NULL ||
	 			head->next == LIST_POISON1 || head->prev == LIST_POISON2) {

				switch (temp->tmr_type) {
				case TMR_JIFFY:
					printk("Jiffy Timer is corrupt\n");
					break;
				case TMR_ENFORCE:
					printk("Enforce Timer is corrupt\n");
					break;
				case TMR_REPLENISH_RSV:
					printk("Replenish Timer is corrupt %p %p\n", head->next, head->prev);
					break;
				default:
					printk("Unknown Timer is corrupt\n");
					break;
				}
				goto error_spin_unlock;
			}

			// Delete the timer from active list
			rk_list_del(head);
			head->next = head->prev = NULL;

			// Unlock rk_timer_lock 
			// - should be unlocked before calling cpu_reserve_enforce/replenish
			// - since the timer is already removed from the timer list, it is safe to unlock.
			raw_spin_unlock_irqrestore(ptr_rk_timer_lock(cpunum), flags);

			// Process the expired timer
			switch (temp->tmr_type) {
			case TMR_JIFFY:
				printk("Error: Jiffy Timer in a non-jiffy RK module?\n");
				break;

			case TMR_ENFORCE:
				if (temp->reserve_link != NULL) {
					cpu_reserve_enforce(temp->reserve_link);
				}
				break;

			case TMR_REPLENISH_RSV:
				if (temp->reserve_link != NULL) {
					cpu_reserve_replenish(temp->reserve_link, &temp->tmr_expire, &period);

					// Make sure that we are greater than now
					while (temp->tmr_expire <= (now + TIMER_DELTA)) {
						adjust_timer(temp, period, cpunum);
					}

					rk_timer_add_cpu(temp, cpunum);
				}
				break;

			default:
				break;
			}
		}
		else {
			/* 
			 * The list of online timers is sorted 
			 * in the order of expiry times, so we can break safely here 
			 */
			break;
		}

		/*
		 * The timer list is not stable: 
 		 * Enforcement timers may have been changed during the isr processing
		 * Therefore, we need to make sure that we start from the head of the list
		 * The assumption here is that we delete the elements as we process them
		 * Otherwise, the next_timer may have been deleted and we cannot use it to re-enter the list
		 * Previous code was using head = next_timer, which leads to bugs when the list is not stable	
		 */
		raw_spin_lock_irqsave(ptr_rk_timer_lock(cpunum), flags);
		head = ptr_rk_online_timer_root(cpunum)->next;
	}
  
  	if (!list_empty(ptr_rk_online_timer_root(cpunum))) {
		head = ptr_rk_online_timer_root(cpunum)->next;
		while (head != ptr_rk_online_timer_root(cpunum)) {
			temp = list_entry(head, struct rk_timer, tmr_tmr);
			if (temp->tmr_expire > now) {
				// printk("rk_timer_isr : %llu %llu\n", temp->tmr_expire, now);
				__rk_update_hw_timer_cpu(temp, cpunum);
				break;
			}
			head = head->next;
		}
		if (head == ptr_rk_online_timer_root(cpunum)){
			printk("BUG: No Timers Left\n");
			goto error_spin_unlock;
		}	
  	}
  	else {
		goto error_spin_unlock;
 	}
	raw_spin_unlock_irqrestore(ptr_rk_timer_lock(cpunum), flags);
	//printk("rk_timer_isr: exit\n");
	return HRTIMER_NORESTART;

	// Do not use HRTIMER_RESTART. See note in __rk_update_hw_timer_cpu() 
  	//return HRTIMER_RESTART; 

error_spin_unlock:
	raw_spin_unlock_irqrestore(ptr_rk_timer_lock(cpunum), flags);
	//printk("rk_timer_isr: exit with error\n");
	return HRTIMER_NORESTART;
}


/*
 * Create a timer to replenish a reserve, add it to the queue.
 * Set a linux timer if necessary.
 */
void rk_replenish_timer_create(rk_reserve_t rsv, cpu_tick_data_t ticks)
{
	rk_timer_t tmr;
	cpu_tick_data_t now;
	unsigned long flags;
  	int cpunum;

	cpunum = ((struct cpu_reserve*)rsv->reserve)->cpu_res_attr.cpunum;
  
	// create a timer for enforcement (data structure only)
	tmr = rk_timer_create();
	rsv->reserve_enforce_timer = tmr;
	tmr->reserve_link = rsv;
	tmr->tmr_type = TMR_ENFORCE;
	
  	// create a timer for replenishment and add it to queue
	tmr = rk_timer_create();
  	rsv->reserve_replenish_timer = tmr;
  	tmr->reserve_link = rsv;
  	tmr->tmr_type = TMR_REPLENISH_RSV;
  
  	rk_rdtsc(&now);
  	tmr->tmr_expire = now + ticks;
  
	raw_spin_lock_irqsave(ptr_rk_timer_lock(cpunum), flags);
	__rk_timer_add_cpu(tmr, cpunum);
  	__rk_update_hw_timer_cpu(list_entry(ptr_rk_online_timer_root(cpunum)->next, struct rk_timer, tmr_tmr), cpunum);	
	raw_spin_unlock_irqrestore(ptr_rk_timer_lock(cpunum), flags);
}


void rk_timer_destroy(rk_timer_t tmr, int cpunum)
{
 	//printk("Destroying Timer of type %d (cpunum:%d)\n", tmr->tmr_type, cpunum); 
  	if (tmr != NULL) {
		memset(tmr, 0, sizeof(struct rk_timer));
		kfree(tmr);
  	}
}

/* 
 * Disarms a potentially armed timer; call this before destroying
 * unless you know that the timer is not armed. 
 *
 * Called by
 * - cpu_reserve.c::rk_cpu_reserve_delete 
 * - cpu_reserve.c::cpu_reserve_stop_account
 * - cpu_reserve.c::cpu_reserve_replenish
 */
inline void rk_timer_remove(rk_timer_t tmr, int cpunum)
{
	unsigned long flags;
	int need_to_reprogram = 0;
	struct rk_timer *next_tmr = NULL;

	//printk("Removing Timer of type %d\n", tmr->tmr_type); 
	raw_spin_lock_irqsave(ptr_rk_timer_lock(cpunum), flags);
	if (tmr == NULL) {
		printk("rk_timer_remove: Trying to remove a NULL timer\n");
		goto error;
	}

	if (!tmr->tmr_tmr.next || !tmr->tmr_tmr.prev) {
		goto error;
	}

	if (tmr == list_entry(ptr_rk_online_timer_root(cpunum)->next, struct rk_timer, tmr_tmr)) {
		need_to_reprogram = TRUE;
		next_tmr = list_entry(tmr->tmr_tmr.next, struct rk_timer, tmr_tmr);
		if (next_tmr == list_entry(ptr_rk_online_timer_root(cpunum), struct rk_timer, tmr_tmr)) {
			next_tmr = NULL;
		}
	}

	rk_list_del(&tmr->tmr_tmr);
	tmr->tmr_tmr.next = tmr->tmr_tmr.prev = NULL;

	if (need_to_reprogram) {
		// Reprogram the hwtimer for the next timer
		if (next_tmr) {
			//printk("rk_timer_remove : cur %d / next %d\n", tmr->tmr_type, list_entry(tmr->tmr_tmr.next, struct rk_timer, tmr_tmr)->tmr_type);
			__rk_update_hw_timer_cpu(next_tmr, cpunum);
		}
		else {
			// Note: Do not cancel hrtimer here. As rk_timer_remove() may be called
			//       within the hrtimer callback function, cancelling here can lead to
			//       a deadlock. Even if it is not cancelled, it will be reprogrammed 
			//       with other timer requests anyway.
			//hrtimer_cancel(&(ptr_rk_virtual_timer(cpunum)->t)); 
			//dump_stack();
		}
	}
error:
	raw_spin_unlock_irqrestore(ptr_rk_timer_lock(cpunum), flags);
}


/* 
 * Should be called with rset->lock held
 *
 * Called by
 * - cpu_reserve.c::cpu_reserve_replenish
 * - cpu_reserve.c::cpu_reserve_stop_account
 */
void rk_enforce_timer_stop(rk_reserve_t rsv, int cpunum)
{
	//printk("rk_enforce_timer_stop: %d\n", cpunum);
	rk_timer_remove(rsv->reserve_enforce_timer, cpunum);
}


/* 
 * Should be called with rset->lock held
 *
 * Called by
 * - cpu_reserve.c::cpu_reserve_start_account
 * - cpu_reserve.c::cpu_reserve_replenish
 */
void rk_enforce_timer_start(rk_reserve_t rsv, cpu_tick_t next_available_ticks, cpu_tick_t start, int cpunum)
{
	cpu_tick_data_t next_ticks;
	unsigned long flags;

	//printk("rk_enforce_timer_start: %d\n", cpunum);
	next_ticks = *next_available_ticks;

	if (next_ticks < RK_MIN_TIMER) {
		next_ticks = RK_MIN_TIMER;
	}

	raw_spin_lock_irqsave(ptr_rk_timer_lock(cpunum), flags);

	rsv->reserve_enforce_timer->tmr_expire = next_ticks;
  	adjust_timer(rsv->reserve_enforce_timer, *start, cpunum);
  	__rk_timer_add_cpu(rsv->reserve_enforce_timer, cpunum);
  
  	if (list_first_entry(ptr_rk_online_timer_root(cpunum), struct rk_timer, tmr_tmr) == rsv->reserve_enforce_timer) {
		// printk("rk_enforce_timer_start\n");
		__rk_update_hw_timer_cpu(list_first_entry(ptr_rk_online_timer_root(cpunum), struct rk_timer, tmr_tmr), cpunum);	
	}

	raw_spin_unlock_irqrestore(ptr_rk_timer_lock(cpunum), flags);
}

