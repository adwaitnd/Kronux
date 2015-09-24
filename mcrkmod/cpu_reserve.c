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
   Pittsburgh PA 15213-3890
 *
 *  or via email to raj@ece.cmu.edu
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */


/*
 * cpu_reserve.c: code to manage cpu reservations
 */

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <asm/percpu.h>
#include <asm/uaccess.h>
#include <linux/kthread.h>
#include <rk/rk_mc.h>
#include <rk/timespec.h>
#include <rk/rk_mutex.h>
#include <rk/rk_virt.h>

#if defined(RK_VCHANNEL_SOCKET)
#include <linux/socket.h>
#include <linux/net.h>
#elif defined(RK_VCHANNEL_PIPE)
#include <linux/fs.h>
#endif

#define RATE_MONOTONIC			0
#define DEADLINE_MONOTONIC		1
#define EDF				2
#define NUM_CPU_RESERVE_POLICIES 	(EDF+1)


#define	CAPACITY_INT(x)		(div_s64(x,100))
#define	CAPACITY_FRAC(x)	((x)-(div_s64(x,100))*100)
#define	INT2CAPACITY(x)		((x)*10000)
#define	CAPACITY2INT(x)		(div_s64(x,10000))
#define	PERCENT2CAPACITY(x)	((x)*100)
#define	CAPACITY_OF(c,t)	(div64_s64(INT2CAPACITY(c),(t)))


static int cpu_reserve_read_proc(rk_reserve_t rsv, char *buf);
struct rk_reserve_ops cpu_reserve_ops = {
	cpu_reserve_read_proc,
};

#ifndef RK_GLOBAL_SCHED // Partitioned scheduling

#ifdef RK_UNIQUE_PRIORITY_ACROSS_PROCESSORS
struct list_head cpu_reserves_head;
#define ptr_cpu_reserves_head(cpunum) (&cpu_reserves_head)
#else
DEFINE_PER_CPU(struct list_head, cpu_reserves_head);
#define ptr_cpu_reserves_head(cpunum) (&per_cpu(cpu_reserves_head, (cpunum)))
#endif // RK_UNIQUE_PRIORITY_ACROSS_PROCESSORS

DEFINE_PER_CPU(cpu_capacity_t, cpu_current_capacity);
#define var_cpu_current_capacity(cpunum) (per_cpu(cpu_current_capacity, (cpunum)))

#else // Global scheduling

struct list_head cpu_reserves_head;
cpu_capacity_t cpu_current_capacity;
#define ptr_cpu_reserves_head(cpunum) (&cpu_reserves_head)
#define var_cpu_current_capacity(cpunum) (cpu_current_capacity)

#endif // RK_GLOBAL_SCHED


DEFINE_PER_CPU(rk_reserve_t, rk_current_cpu_reserve);
DEFINE_PER_CPU(struct task_struct*, rk_worker);
DEFINE_PER_CPU(struct rk_workqueue*, cpu_workqueue);
DEFINE_PER_CPU(struct task_struct*, rk_post_schedule_wakeup); // only used by global scheduling

int cpu_reserves_scheduling_policy = DEADLINE_MONOTONIC;
int cpu_reserves_kernel_scheduling_policy = SCHED_FIFO;
int cpu_reserves_current_min_priority = BASE_LINUXRK_PRIORITY;

void cpursv_profile_update(cpu_tick_t start, cpu_tick_t now, cpu_tick_t used_ticks, struct rk_cpu_profile_set *profile);
void cpursv_profile_utilization(cpu_reserve_t cpu, unsigned long util);


/* 
 * CPU Reserve inline helper functions
 */
static inline int cpunum_cpu_reserve(rk_reserve_t rsv)
{
	return ((struct cpu_reserve*)rsv->reserve)->cpu_res_attr.cpunum;
}


/*
 * Global scheduling: Return the task that can be activated in the current cpursv
 * 
 * Should be called with rset->lock held
 *
 * Called by rk_get_next_task_in_cpursv,
 * - rk_sched.c::rk_post_schedule
 * - resource_set.c::sys_rk_resource_set_detach_process
 */
static inline bool __feasible_next_task(rk_reserve_t rsv, struct task_struct *task, struct task_struct *except) 
{
	if (task == except) return FALSE;
	if (rk_get_task_current_cpursv(task) != rsv) return FALSE;
	if (task->rk_cannot_schedule & RK_TASK_BEING_ATTACHED) return FALSE;
	if (task->rk_cannot_schedule & RK_TASK_TO_BE_DETACHED) return FALSE;
	if (task->rk_cannot_schedule & RK_TASK_RUNNING) return FALSE; // Should not enable task in running state
	
	// Only a task waiting for its turn can be activated
	if (task->rk_cannot_schedule & RK_TASK_WAIT_FOR_TURN) {
		task->rk_cannot_schedule &= ~(RK_TASK_UNSCHEDULABLE | RK_TASK_WAIT_FOR_TURN);
		//printk("get_next_tsk: current %d pid %d rk_state %d\n", current->pid, task->pid, task->rk_cannot_schedule);
		return TRUE;
	}
	return FALSE;
}

struct task_struct* rk_get_next_task_in_cpursv(rk_resource_set_t rset, rk_reserve_t rsv, struct task_struct *start, struct task_struct *except) 
{
	struct task_struct *task;
	struct list_head *p;

	if (rsv->reservation_state & RSV_IS_DEPLETED) return NULL;
	
	for (p = start->rk_resource_set_link.next; p != &start->rk_resource_set_link; p = p->next) {
		if (p == &rset->task_list) continue;
		task = list_entry(p, struct task_struct, rk_resource_set_link);

		if (__feasible_next_task(rsv, task, except)) return task;
	}
	if (__feasible_next_task(rsv, start, except)) return start;

	return NULL;
}


/* 
 * Called by
 * - rk_sched.c::rk_pre_schedule
 * - resource_set.c::sys_rk_resource_set_detach_process
 */
void rk_prepare_task_for_cpursv(int type, struct task_struct *task, int cpunum, int prio)
{
	struct sched_param par;

#ifndef RK_GLOBAL_SCHED
	cpumask_t cpumask;
	cpus_clear(cpumask);
	cpu_set(cpunum, cpumask);

	// Partitioned sched: Migrate the task to the rsv's core
	// - Note: set_cpus_allowed_ptr() cannot be called in ISR 
	if (!cpumask_equal(&task->cpus_allowed, &cpumask)) {
		//printk("rk_prepare_task_for_cpursv: cpu %d migration %s (pid:%d, cur_tsk_cpu:%d, dest_cpu:%d)\n", raw_smp_processor_id(), type & RK_WORK_ENFORCE ? "ENF" : "REP", task->pid, task_cpu(task), cpunum);
		if (set_cpus_allowed_ptr(task, &cpumask) != 0) {
			printk("rk_prepare_task_for_cpursv: cannot migrate task (pid:%d, cpunum:%d)\n", task->pid, cpunum);
		}
	}
#endif

	// Change the task's priority 
	// - Note: sched_setscheduler() cannot be called in ISR
	par.sched_priority = prio;
	if (task->rk_mutex_inherited_prio_list && task_inherited_prio(task) > prio) 
		par.sched_priority = task_inherited_prio(task);
	if (task->rt_priority != par.sched_priority) {
		if (sched_setscheduler_nocheck(task, cpu_reserves_kernel_scheduling_policy, &par) < 0) {
			printk("rk_prepare_task_for_cpursv: cannot change task's priority (pid:%d, cur_prio %d, new_prio %d)\n", task->pid, task->rt_priority, par.sched_priority);
		}
	}

	// Wake up task if called by cpu_reserve_replenish
	if (type & RK_WORK_WAKEUP) {
		wake_up_process(task);
	}
}

int rk_worker_thread(void *data)
{
	int thread_id = (long)data;
	printk("rk_worker_thread: %s (%d)\n", current->comm, thread_id);

	while (!kthread_should_stop()) {
		struct rk_work_info work = {0,};

		// Performs deferred executions from cpu_reserve_replenish/enforce
		while (rk_pop_from_workqueue(thread_id, &work) == RK_SUCCESS) {
			rk_prepare_task_for_cpursv(work.type, work.args[0], (long)work.args[1], (long)work.args[2]);
		}

		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return 0;
}

void cpu_reserves_init(void)
{
	int cpunum = 0;
	char name[20];
	cpumask_t cpumask;
	struct sched_param par;

	cpus_clear(cpumask);
	par.sched_priority = MAX_LINUXRK_PRIORITY; 

	for_each_online_cpu(cpunum) {
		per_cpu(rk_current_cpu_reserve, cpunum) = NULL;
		var_cpu_current_capacity(cpunum) = 0;
		INIT_LIST_HEAD(ptr_cpu_reserves_head(cpunum));

		per_cpu(rk_post_schedule_wakeup, cpunum) = NULL;

		// worker thread
		per_cpu(cpu_workqueue, cpunum) = kmalloc(sizeof(struct rk_workqueue), GFP_ATOMIC);
		memset(per_cpu(cpu_workqueue, cpunum), 0, sizeof(struct rk_workqueue));
		raw_spin_lock_init(&per_cpu(cpu_workqueue, cpunum)->lock);

		sprintf(name, "rk-worker/%d", cpunum);
		per_cpu(rk_worker, cpunum) = kthread_create(&rk_worker_thread, (void*)(long)cpunum, name);

		if (IS_ERR(per_cpu(rk_worker, cpunum))) {
			printk("%s: ERROR\n", name);
			continue;
		}

		cpus_clear(cpumask);
		cpu_set(cpunum, cpumask);
		set_cpus_allowed_ptr(per_cpu(rk_worker, cpunum), &cpumask);

		sched_setscheduler_nocheck(per_cpu(rk_worker, cpunum), cpu_reserves_kernel_scheduling_policy, &par);
		wake_up_process(per_cpu(rk_worker, cpunum));
	}
}

void cpu_reserves_cleanup(void)
{
	int cpunum = 0;
	for_each_online_cpu(cpunum) {
		kthread_stop(per_cpu(rk_worker, cpunum));

		var_cpu_current_capacity(cpunum) = 0;
		INIT_LIST_HEAD(ptr_cpu_reserves_head(cpunum));
		kfree(per_cpu(cpu_workqueue, cpunum));
	}
}

int ceiling(unsigned long dividend, unsigned long divider)
{
	int quotient;
	quotient = div_s64(dividend,divider); 

	if (divider * quotient == dividend)
		return (quotient);
	else
		return (++quotient);
}

int timespec_ceiling(struct timespec dividend, struct timespec divider)
{
	int quotient = 1;
	struct timespec divider1;

	divider1 = divider;
	while (timespec_gt(dividend, divider1)) {
		timespec_add(divider1, divider);
		quotient++;
	}
	return (quotient);
}

/* this ceiling computation works faster by assuming that the seconds
 * field is less than 1000 seconds, and by truncating the nanoseconds field.
 * If the seconds field is greater than 1000 seconds, a valid but VERY 
 * inefficient response will result!
 */
int efficient_timespec_ceil(struct timespec dividend, struct timespec divider)
{
	unsigned long dividend_microsecs, divider_microsecs;

	if ((divider.tv_sec == 0) && (divider.tv_nsec == 0)) {
		return -1;
	}
	if ((dividend.tv_sec >= 1000) || (divider.tv_sec >= 1000)) {
		/* be ready to pay a BIG penalty now! */
		return (timespec_ceiling(dividend, divider));
	}

	/* truncate nanoseconds */
	dividend_microsecs = (dividend.tv_sec % 1000) * MICROSEC_PER_SEC +
		(div_s64(dividend.tv_nsec,1000));

	divider_microsecs = (divider.tv_sec % 1000) * MICROSEC_PER_SEC +
		(div_s64(divider.tv_nsec,1000));

	return (ceiling(dividend_microsecs, divider_microsecs));
}

/* Should optmize the multiplication for efficient handling of the multiplication operation*/
struct timespec timespec_mult(struct timespec multiplicand, int multiplier)
{
	struct timespec result;

	result.tv_sec = 0;
	result.tv_nsec = 0;

	while (multiplier--) {
		timespec_add(result, multiplicand);
	}

	return (result);
}


/* 
 * Should be called with rk_sem held
 *
 * Called by
 * - sys_rk_cpu_reserve_create
 */
void priority_list_add(cpu_reserve_t new_rsv, struct list_head *head)
{
	cpu_reserve_t cur_rsv, prev_rsv;
	int prio = BASE_LINUXRK_PRIORITY + 1;
	struct list_head *temp;	
	struct timespec *new_time, *cur_time, *prev_time;
	int needs_prio_change = FALSE;

	INIT_LIST_HEAD(&new_rsv->cpu_link);
	new_time = cur_time = prev_time = NULL;
	prev_rsv = NULL;

	// Add to a list sorted in the descending order of priorities 
	// (highest to lowest)
	for (temp = head->next; temp != head; temp = temp->next) {
		cur_rsv = list_entry(temp, struct cpu_reserve, cpu_link);
		prio = cur_rsv->cpu_priority_index;
		
		// new_rsv has been inserted
		if (!list_empty(&new_rsv->cpu_link)) {
			if (needs_prio_change) cur_rsv->cpu_priority_index--;
			// Check minumum POSIX real-time priority
			if (cur_rsv->cpu_priority_index < 1) cur_rsv->cpu_priority_index = 1;
			continue;
		}
		// new_rsv needs to be inserted
		switch (cpu_reserves_scheduling_policy) {
		case RATE_MONOTONIC:
			new_time = &new_rsv->cpu_res_attr.period;
			cur_time = &cur_rsv->cpu_res_attr.period;
			break;
		case DEADLINE_MONOTONIC:
			new_time = &new_rsv->cpu_res_attr.deadline;
			cur_time = &cur_rsv->cpu_res_attr.deadline;
			break;
		case EDF:
			new_time = &new_rsv->absolute_deadline;
			cur_time = &cur_rsv->absolute_deadline;
			break;
		default:
			new_time = cur_time = NULL;
			break;
		}
		if (!new_time || !cur_time || timespec_ge(*new_time, *cur_time)) {
			prev_time = cur_time;
			prev_rsv = cur_rsv;
			continue;
		}
		// Add new_rsv before cur_rsv
		list_add_tail(&new_rsv->cpu_link, temp);
		temp = &new_rsv->cpu_link;

#if defined(RK_UNIQUE_PRIORITY) || defined(RK_UNIQUE_PRIORITY_ACROSS_PROCESSORS)
		new_rsv->cpu_priority_index = prio;
		needs_prio_change = TRUE;
#else
		if (prev_time && prev_rsv && timespec_eq(*new_time, *prev_time)) {
			new_rsv->cpu_priority_index = prev_rsv->cpu_priority_index;
		}
		else {
			new_rsv->cpu_priority_index = prio;
			needs_prio_change = TRUE;
		}
#endif
	}
	if (list_empty(&new_rsv->cpu_link)) {
#if defined(RK_UNIQUE_PRIORITY) || defined(RK_UNIQUE_PRIORITY_ACROSS_PROCESSORS)
		new_rsv->cpu_priority_index = prio - 1;
#else
		if (new_time && cur_time && timespec_eq(*new_time, *cur_time)) 
			new_rsv->cpu_priority_index = prio;
		else 
			new_rsv->cpu_priority_index = prio - 1;
#endif
		if (new_rsv->cpu_priority_index < 1) new_rsv->cpu_priority_index = 1;
		list_add_tail(&new_rsv->cpu_link, head);
	}
	// Update min cpursv priority
	cur_rsv = list_entry(head->prev, struct cpu_reserve, cpu_link);
	cpu_reserves_current_min_priority = cur_rsv->cpu_priority_index;
}

/* 
 * Should be called with rk_sem held
 * Should be called after cpu_rsv is removed from rset 
 *
 * Called by
 * - sys_rk_cpu_reserve_create (when failed to create a new cpursv)
 * - rk_cpu_reserve_delete
 */
int priority_list_remove(cpu_reserve_t cpu, struct list_head *head)
{
	cpu_reserve_t rsv;
	struct list_head *temp;
	int deleted = FALSE;
	int cpu_prio, prev_prio, cur_prio;
	
	if (head->next == head) {
		/* Delete from an empty list? */
		printk("priority_list_remove: delete from empty list (curcpu:%d)\n", raw_smp_processor_id());
		return -1;
	}
	
	// Search through a list to find the element
	cpu_prio = cpu->cpu_priority_index;
	prev_prio = -1;
	for (temp = head->next; temp != head; temp = temp->next) {
		rsv = list_entry(temp, struct cpu_reserve, cpu_link);
		cur_prio = rsv->cpu_priority_index;

		if (deleted == FALSE) {
			if (rsv == cpu) {
				temp = temp->prev;
				list_del(&cpu->cpu_link);
				deleted = TRUE;
				if (cpu_prio == prev_prio) break;
			}
			else prev_prio = cur_prio;
		}
		else {
			if (cpu_prio == cur_prio) break;
			rsv->cpu_priority_index++;
			prev_prio = cur_prio;
		}
	}
	// Update min cpursv priority
	if (!list_empty(head)) {
		rsv = list_entry(head->prev, struct cpu_reserve, cpu_link);
		cpu_reserves_current_min_priority = rsv->cpu_priority_index;
	}
	else cpu_reserves_current_min_priority = BASE_LINUXRK_PRIORITY;

	return 0;
}

/* 
 * Should be called with rk_sem held
 *
 * Called by
 * - sys_rk_cpu_reserve_create
 */
#ifndef RK_GLOBAL_SCHED
int admit_reserve_request(cpu_reserve_t cpu, int cpunum)
{
	struct timespec completion_time, prev_completion_time;
	struct timespec tmpval;
	cpu_reserve_t rsv_i, rsv_j;
	int ceil_value;
	cpu_reserve_attr_t attr;
	cpu_capacity_t  guarcap;
	long long qc, qt;
	int cont, done;	

	guarcap = PERCENT2CAPACITY(0);
	attr = &cpu->cpu_res_attr;

	if (timespec_gt(attr->compute_time, attr->period) || timespec_gt(attr->compute_time, attr->deadline)) {
		printk("admit_reserve_request: RK cannot admit tasks with computation time greater than the period or the deadline.\n");
		return FALSE;
	}

	list_for_each_entry(rsv_i, ptr_cpu_reserves_head(cpunum), cpu_link) {
		cpu_reserve_attr_t rsv_i_attr = &rsv_i->cpu_res_attr;
		if (rsv_i_attr->cpunum != cpunum) continue;

		cont = FALSE;
		switch (cpu_reserves_scheduling_policy) {
		case RATE_MONOTONIC:
			if (timespec_lt(rsv_i_attr->period, attr->period)) {
				cont = TRUE;
			}
			break;

		case DEADLINE_MONOTONIC:
			if (timespec_lt(rsv_i_attr->deadline, attr->deadline)) {
				cont = TRUE;
			}
			break;

		case EDF:
			qc = rsv_i_attr->compute_time.tv_sec * NANOSEC_PER_SEC;
			qc += rsv_i_attr->compute_time.tv_nsec;
			qt = rsv_i_attr->period.tv_sec * NANOSEC_PER_SEC;
			qt += rsv_i_attr->period.tv_nsec;
			guarcap += CAPACITY_OF(qc, qt);
			if (guarcap > PERCENT2CAPACITY(100)) {
				return FALSE;
			}
			cont = TRUE;
			break;
		}
		if (cont) continue;

		timespec_zero(prev_completion_time);
		timespec_set(completion_time, rsv_i_attr->compute_time);
		timespec_add(completion_time, rsv_i_attr->blocking_time);

		while (timespec_le(prev_completion_time, rsv_i_attr->deadline) && timespec_lt(prev_completion_time, completion_time)) {
			timespec_set(prev_completion_time, completion_time);
			timespec_zero(completion_time);
			
			list_for_each_entry(rsv_j, ptr_cpu_reserves_head(cpunum), cpu_link) {
				cpu_reserve_attr_t rsv_j_attr = &rsv_j->cpu_res_attr;;

				if (rsv_j == rsv_i) continue;

				done = FALSE;
				switch (cpu_reserves_scheduling_policy) {
				case RATE_MONOTONIC:
					if (timespec_gt(rsv_j_attr->period, rsv_i_attr->period)) {
						done = TRUE;
					}
					break;

				case DEADLINE_MONOTONIC:
					if (timespec_gt(rsv_j_attr->deadline, rsv_i_attr->deadline)) {
						done = TRUE;
					}
					break;
				}
				if (done) break;				

				//ceil_value = efficient_timespec_ceil(prev_completion_time, rsv_j_attr->period);
				timespec_set(tmpval, prev_completion_time);
				timespec_add(tmpval, rsv_j_attr->release_jitter);

				ceil_value = efficient_timespec_ceil(tmpval, rsv_j_attr->period);

				tmpval = timespec_mult(rsv_j_attr->compute_time, ceil_value);
				timespec_add(completion_time, tmpval);
			}
			timespec_add(completion_time, rsv_i_attr->compute_time);
			timespec_add(completion_time, rsv_i_attr->blocking_time);
		}
		if (timespec_gt(completion_time, rsv_i_attr->period) || timespec_gt(completion_time, rsv_i_attr->deadline)) {
			return FALSE;
		}
	}
	return TRUE;
}
#else
int admit_reserve_request(cpu_reserve_t cpu, int notused)
{
	// TODO: admission control test for global scheduling
	return TRUE;
}
#endif // RK_GLOBAL_SCHED


extern int debug_rd;
/* 
 * Called by 
 * - timer.c::rk_timer_isr 
 */
void cpu_reserve_replenish(rk_reserve_t rsv, cpu_tick_t start_ticks, cpu_tick_t output_period) 
{
	cpu_reserve_t cpu;
	cpu_capacity_t utilization = 0;
	cpu_tick_data_t now;
	cpu_tick_data_t ticks;
	int cpunum = 0;
	rk_resource_set_t rset;
	struct task_struct *task;
	unsigned long flags;
	int nr_task_count = 0;
	int cpu_priority_index;

	//printk("replenish enter: %d\n", cpunum);
	rk_rdtsc(&now);
	
	if (rsv == NULL) {
		printk("cpu_reserve_replenish: Cannot replenish a NULL reserve\n");
		return;
	}
	cpu = rsv->reserve;
	if (cpu == NULL) {
		printk("cpu_reserve_replenish: Cannot replenish a reserve whose cpu reservation is NULL\n");
		return;
	}
	rset = rsv->parent_resource_set;
	if (rset == NULL) {
		printk("cpu_reserve_replenish: parant_resource_set is NULL. Might be destroyed\n");
		return;
	}
	raw_spin_lock_irqsave(&rset->lock, flags);
	
	// Double check whether rsv is still valid
	if (rsv->parent_resource_set != rset) {
		printk("cpu_reserve_replenish: parant_resource_set is NULL. Might be destroyed before locking\n");
		goto error_spin_unlock;
	}
	cpunum = raw_smp_processor_id();
#ifndef RK_GLOBAL_SCHED
	if (cpunum != cpu->cpu_res_attr.cpunum) {
		printk("cpu_reserve_replenish: ERROR - cpunum %d, curcpu %d\n", cpu->cpu_res_attr.cpunum, cpunum);
		return;
	}
#endif

	/* Later on We can create a seperate code path for starting a reserve, now let it be in common path (performance)*/
	if (rsv->reservation_state == RSV_IS_NULL) {
		// No tasks whose default cpursv is 'rsv' are running at this point.
		rsv->reservation_state |= RSV_IS_STARTED;
	}
	else {
		cpu_tick_data_t used_ticks;
		if (rsv->reservation_state & RSV_IS_RUNNING) {
			// CPU profile: update completion time
			used_ticks = now - cpu->start_time_of_cur_exec;
			cpursv_profile_update(NULL, &now, &used_ticks, &cpu->cpu_profile);
			if (cpu->current_task) cpursv_profile_update(NULL, &now, &used_ticks, cpu->current_task->rk_profile);

			/* Account the current invocation also */
			used_ticks = (now - cpu->start_time_of_cur_exec) + cpu->used_ticks_in_cur_period;
			cpu->start_time_of_cur_exec = now;
		}
		else {
			/* Reserve is not running, so get the previous information */
			used_ticks = (cpu->used_ticks_in_cur_period);
		}

		cpu->used_ticks_in_cur_period = used_ticks;
		cpu->used_ticks_in_prev_period = used_ticks;
			
		utilization = (unsigned long)div64_s64(cpu->used_ticks_in_cur_period * 10000, cpu->cpu_period_ticks);
		if (utilization > cpu->cpu_max_utilization) cpu->cpu_max_utilization = utilization;
		if (utilization < cpu->cpu_min_utilization) cpu->cpu_min_utilization = utilization;

		// CPU profile: record utilization
		cpursv_profile_utilization(cpu, utilization);
	}

	// CPU profile: update release time (cpu reserve)
	// - Put completion time (2nd param) as well for the case where no task is released in this period
	cpursv_profile_update(&now, &now, NULL, &cpu->cpu_profile); 
	// CPU profile: update release time (task)
	if ((rsv->reservation_state & RSV_IS_RUNNING) && cpu->current_task) {
		cpursv_profile_update(&cpu->start_time_of_cur_exec, NULL, NULL, cpu->current_task->rk_profile);
	}
	//printk("replenish\n");

	*output_period = cpu->cpu_period_ticks;
	cpu->release_time_of_cur_period = now;
	cpu->cpu_eligible_deadline_ticks = *start_ticks + cpu->cpu_deadline_ticks;
		
	/* Refill the capacity of the task */
	switch (rsv->reservation_parameters.rep_mode) {
	case RSV_SOFT:
		/* Give him all he wanted */
		cpu->avail_ticks_in_cur_period = cpu->cpu_time_ticks;
		break;

	case RSV_FIRM:
	case RSV_HARD:
		/* Measure how much he has overflown  */
		ticks = cpu->used_ticks_in_cur_period - cpu->avail_ticks_in_cur_period;
		//printk("used_tick:%llu, avail:%llu, cpu_time_tick:%llu\n", 
		//	cpu->used_ticks_in_cur_period, cpu->avail_ticks_in_cur_period, cpu->cpu_time_ticks);
		if (ticks > 0) {
			/* He has overflown last time */
			cpu_tick_data_t next;
			next = cpu->cpu_time_ticks - ticks;
			if (cpu->cpu_time_ticks >= ticks) {
				/* Adjust for the overflow in his current replenishment */
				cpu->avail_ticks_in_cur_period = next;
			}
			else {
				/* He has overflown more than 1 whole cpu_time dont give him anything this period */
				cpu->avail_ticks_in_cur_period = 0;
			}
		}
		else {
			/* He has not overflown so give him what he wanted */
			cpu->avail_ticks_in_cur_period = cpu->cpu_time_ticks;
		}
		break;

	default:
		break;
	}

	cpu_priority_index = cpu->cpu_priority_index;
	if (is_pseudo_vcpu(cpu)) pseudo_vcpu_replenish(cpu, &cpu_priority_index);
	
	// We have replenished the reserve 
	rsv->reservation_state &= ~RSV_IS_DEPLETED;
	cpu->used_ticks_in_cur_period = 0;

	// rsv is running, so we need to restart the enforcement timer
	if ((rsv->reservation_state & RSV_IS_RUNNING)) {
		cpu_tick_data_t next;
		next = cpu->avail_ticks_in_cur_period;

		if (cpu->do_enforcement) {
			rk_enforce_timer_stop(rsv, cpunum);
			rk_enforce_timer_start(rsv, &next, start_ticks, cpunum);
		}
	}

	if (debug_rd == rset->rd_entry) {
		printk("replenish: curcpu %d, rsvidx %d, prev_util %ld\n", raw_smp_processor_id(), rsv->reserve_index, utilization);
	}

	// Wake up the tasks whose default cpursv is equal to 'rsv'
	list_for_each_entry(task, &rset->task_list, rk_resource_set_link) {
		int type = RK_WORK_REPLENISH_WAKEUP;
		rk_reserve_t task_cur_rsv;

		if (!task->rk_cpursv_list || !task->rk_cpursv_list->n) continue;
		if (__rk_get_task_default_cpursv(task) != rsv) continue;
		if (task->rk_cannot_schedule & RK_TASK_BEING_ATTACHED) continue;
		if (task->rk_cannot_schedule & RK_TASK_TO_BE_DETACHED) continue;
		
		// Task might have been using another reserve
		task_cur_rsv = rk_get_task_current_cpursv(task);
		if (task_cur_rsv && task_cur_rsv != rsv && (task_cur_rsv->reservation_state & RSV_IS_RUNNING)) {
			// Stop accounting task's reserve
			cpu_reserve_stop_account(task_cur_rsv, &now);
		}
		__rk_set_task_default_cpursv(task);

#ifndef RK_GLOBAL_SCHED
		// Partitioned sched: Change priority and migrate to a dest cpu
		task->rk_cannot_schedule &= ~RK_TASK_SCHED_MASK;
		//printk("replenish: pid %d tsk_state %d rk_state %d (act)\n", task->pid, task->state, task->rk_cannot_schedule);
#else
		// Global scheduling: activate one task at a time
		if (nr_task_count == 0 && !(rsv->reservation_state & RSV_IS_RUNNING)) {
			if (debug_rd == rset->rd_entry) {
				printk("replenish: curcpu %d, pid %d tsk_state %ld rk_state %d tsk_cpu %d (act)\n", raw_smp_processor_id(), task->pid, task->state, task->rk_cannot_schedule, task_cpu(task));
			}
			task->rk_cannot_schedule &= ~RK_TASK_SCHED_MASK;
			cpunum = task_cpu(task); 
			cpu->waking_task = task;
		}
		else if (cpu->current_task != task) {
			if (debug_rd == rset->rd_entry) {
				printk("replenish: curcpu %d, pid %d tsk_state %ld rk_state %d tsk_cpu %d\n", raw_smp_processor_id(), task->pid, task->state, task->rk_cannot_schedule, task_cpu(task));
			}
			type = RK_WORK_REPLENISH;
			task->rk_cannot_schedule |= RK_TASK_WAIT_FOR_TURN;
			rk_suspend_task_now(task);
		}
#endif

		nr_task_count++;
		//printk("replenish: migrate pid %d to %d\n", task->pid, cpunum);

		// Push to workqueue
		// - All tasks should be pushed to workqueue, because it's not safe to call wake_up_process() here
		rk_push_to_workqueue(cpunum, type, task, (void*)(long)cpunum, (void*)(long)cpu_priority_index);
	}
	
error_spin_unlock:
	raw_spin_unlock_irqrestore(&rset->lock, flags);

	// wake up rk_worker if necessary
	// - Note: since wake_up_process() holds rq->lock, it should be called after releasing rset->lock.
	if (nr_task_count) wake_up_process(per_cpu(rk_worker, cpunum));
	//printk("replenish exit: %d\n", cpunum);
}


/*
 * Called by
 * - timer.c::rk_timer_isr
 */
void cpu_reserve_enforce(rk_reserve_t rsv)
{
	cpu_reserve_t cpu;
	rk_resource_set_t rset;
	struct siginfo sigrk;
	struct task_struct *task;
	unsigned long flags;
	int cpunum;
#ifndef RK_GLOBAL_SCHED
	bool need_rk_worker = FALSE;
#else
	bool need_rk_worker[RK_MAX_CPUS] = {FALSE,};
#endif
	//printk("enforcement enter\n");

	// Initialize SIG_RK_ENFORCED
	memset(&sigrk, 0, sizeof(sigrk));
	sigrk.si_signo = SIG_RK_ENFORCED;
	sigrk.si_code  = SI_KERNEL;

	if (rsv == NULL) {
		printk("cpu_reserve_enforce: Called with NULL reserve\n");
		return ;
	}
	cpu = rsv->reserve;
	if (cpu == NULL) {
		printk("cpu_reserve_enforce: CPU reserve is NULL\n");
		return ;
	}
	rset = rsv->parent_resource_set;
	if (rset == NULL) {
		printk("cpu_reserve_enforce: parant_resource_set is NULL. Might be destroyed\n");
		return;
	}
	raw_spin_lock_irqsave(&rset->lock, flags);
	
	// Double check if rsv is still valid
	if (rsv->parent_resource_set != rset) {
		goto error;
	}
	cpunum = raw_smp_processor_id();
#ifndef RK_GLOBAL_SCHED
	if (cpunum != cpu->cpu_res_attr.cpunum) {
		printk("cpu_reserve_enforce: ERROR - cpunum %d, curcpu %d\n", cpu->cpu_res_attr.cpunum, cpunum);
		goto error;
	}
#endif

	rsv->reservation_state |= RSV_IS_DEPLETED; 

	if (is_pseudo_vcpu(cpu)) pseudo_vcpu_enforce(cpu);

	if (debug_rd == rset->rd_entry) {
		printk("enforce: curcpu %d, rsvidx %d\n", raw_smp_processor_id(), rsv->reserve_index);
	}
	list_for_each_entry(task, &rset->task_list, rk_resource_set_link) {
		rk_reserve_t newrsv;
		if (rk_get_task_current_cpursv(task) != rsv) continue;
		// If a task has other rsvs, find a valid rsv
		for (;;) {
			newrsv = __rk_set_task_next_cpursv(task);
			if (newrsv == rsv) break;
			if ((newrsv->reservation_state & RSV_IS_STARTED) 
			    && !(newrsv->reservation_state & RSV_IS_DEPLETED)
			    && !is_pseudo_vcpu(newrsv->reserve)) break;
		}
		// Change priority and migrate to the dest cpu
		if (newrsv != rsv) {
			cpu_reserve_t newcpu = newrsv->reserve;
			int type = RK_WORK_ENFORCE;

#ifndef RK_GLOBAL_SCHED
			need_rk_worker = TRUE;
#else
			// Global scheduling: activate one task at a time
			if (!(newrsv->reservation_state & RSV_IS_RUNNING) && !newcpu->waking_task) {
				if (debug_rd == rset->rd_entry) {
					printk("enf: curcpu %d, pid %d tsk_state %ld rk_state %d tsk_cpu %d newrsv %d (act)\n", raw_smp_processor_id(), task->pid, task->state, task->rk_cannot_schedule, task_cpu(task), newrsv->reserve_index);
				}
				type |= RK_WORK_WAKEUP;
				task->rk_cannot_schedule &= ~RK_TASK_SCHED_MASK;
				cpunum = task_cpu(task); 
				cpu->waking_task = task;
			}
			else {
				if (debug_rd == rset->rd_entry) {
					printk("enf: curcpu %d, pid %d tsk_state %ld rk_state %d tsk_cpu %d newrsv %d\n", raw_smp_processor_id(), task->pid, task->state, task->rk_cannot_schedule, task_cpu(task), newrsv->reserve_index);
				}
				task->rk_cannot_schedule |= RK_TASK_WAIT_FOR_TURN;
				rk_suspend_task_now(task);
			}
			if (cpunum < 0 || cpunum >= RK_MAX_CPUS) {
				printk("cpu_reserve_enforce: WARNING - ERROR CPUNUM %d\n", cpunum);
				cpunum = 0;
			}
			need_rk_worker[cpunum] = TRUE;
#endif
			//printk("enforce: mig pid %d (curcpu %d) to rsv %d (dest_cpu %d) \n", task->pid, task_cpu(task), task->rk_cpursv_list->elem[(int)task->rk_current_cpursv], newcpu->cpu_res_attr.cpunum);

			// Push to workqueue
			rk_push_to_workqueue(cpunum, type, task, (void*)(long)newcpu->cpu_res_attr.cpunum, (void*)(long)newcpu->cpu_priority_index);
			continue;
		}
		rk_suspend_task_now(task);

		if (cpu->cpu_res_attr.notify_when_enforced) {
			do_send_sig_info(SIG_RK_ENFORCED, &sigrk, task, false);
		}
		//printk("cpu_reserve_enforce: pid %d, state %ld, cannot_sched %d\n", task->pid, task->state, task->rk_cannot_schedule);
	}

error:
	raw_spin_unlock_irqrestore(&rset->lock, flags);

	// wake up rk_worker if necessary
	// - Note: since wake_up_process() holds rq->lock, it should be called after releasing rset->lock.
#ifndef RK_GLOBAL_SCHED
	if (need_rk_worker) wake_up_process(per_cpu(rk_worker, cpunum));
#else
	for (cpunum = 0; cpunum < RK_MAX_CPUS; cpunum++) {
		if (need_rk_worker[cpunum]) wake_up_process(per_cpu(rk_worker, cpunum));
	}
#endif
	//printk("enforcement exit\n");
}


/* 
 * Should be called with rset->lock held
 *
 * Called by
 * - rk_sched.c::rk_schedule
 * - resource_set.c::rk_resource_set_detach_process
 */
void cpu_reserve_stop_account(rk_reserve_t rsv, cpu_tick_t now)
{
	cpu_tick_data_t used_ticks;
	cpu_reserve_t cpu;
	int cpunum;

	if (rsv == NULL) {
		printk("cpu_reserve_stop_account: Cannot stop a NULL reserve\n");
		return;
	}
	cpu = rsv->reserve;
	if (cpu == NULL) {
		printk("cpu_reserve_stop_account: Cannot stop a reserve with NULL cpu reservation\n");
		return;
	}
	cpunum = cpu->cpu_res_attr.cpunum;
	if (cpu->do_enforcement) rk_enforce_timer_stop(rsv, cpunum);

	used_ticks = (*now - cpu->start_time_of_cur_exec);

	// CPU profile: update completion time
	cpursv_profile_update(NULL, now, &used_ticks, &cpu->cpu_profile);
	if (cpu->current_task) cpursv_profile_update(NULL, now, &used_ticks, cpu->current_task->rk_profile);

	cpu->used_ticks_in_cur_period = cpu->used_ticks_in_cur_period + used_ticks;
	cpu->current_task = NULL;

	rsv->reservation_state &= ~RSV_IS_RUNNING;
}

/* 
 * Should be called with rset->lock held 
 *
 * Called by
 * - rk_sched.c::rk_schedule 
 */ 
void cpu_reserve_start_account(rk_reserve_t rsv, struct task_struct *task, cpu_tick_t now)
{
	cpu_reserve_t cpu;
	cpu_tick_data_t next;
	int cpunum;

	if (rsv == NULL || rsv->reserve == NULL) return;

	cpu = rsv->reserve;
	cpunum = cpu->cpu_res_attr.cpunum;
	//printk("start: pid %d prio %d cpunum %d rsvid %d\n", task->pid, task->rt_priority, raw_smp_processor_id(), rsv->reserve_index);
	
#ifndef RK_GLOBAL_SCHED
	if (cpunum != raw_smp_processor_id()) {
		printk("cpu_reserve_start_account: ERROR - cpunum:%d, cur cpu:%d\n", cpunum, raw_smp_processor_id());
		//dump_stack();
		return;
	}
#endif
	// Check if 'rsv' is still being used by another task. 
	if (rsv->reservation_state & RSV_IS_RUNNING) {
		// Reschedule task
		set_tsk_need_resched(task);
		return;
	}
	rsv->reservation_state |= RSV_IS_RUNNING;
	cpu->current_task = task;

	// remaining ticks = available ticks - used ticks
	next = cpu->avail_ticks_in_cur_period - cpu->used_ticks_in_cur_period;

	//rk_rdtsc(&(cpu->start_time_of_cur_exec));
	cpu->start_time_of_cur_exec = *now;

	// CPU profile: update release time
	cpursv_profile_update(&cpu->start_time_of_cur_exec, NULL, NULL, &cpu->cpu_profile);
	cpursv_profile_update(&cpu->start_time_of_cur_exec, NULL, NULL, task->rk_profile);

	if (cpu->do_enforcement) 
		rk_enforce_timer_start(rsv, &next, &cpu->start_time_of_cur_exec, cpunum);
}


asmlinkage int sys_rk_cpu_reserve_create(int rd, cpu_reserve_attr_t cpu_attr)
{
	rk_reserve_t  		rsv;
	rk_resource_set_t	rset;
	cpu_reserve_t 		cpu;
	cpu_capacity_t		capacity;
	cpu_tick_data_t		now, start_time_ns;
	struct timespec		cur_time;
	unsigned long 		flags;
	cpumask_t cpumask;

	int cpursv_index;
	int cpunum;
	int i;

	if (rd < 0 || rd >= MAX_RESOURCE_SETS) {
		printk("sys_rk_cpu_reserve_create: Invalid resource set id\n");
		return RK_ERROR;
	}

	rk_sem_down();
	rset = resource_set_descriptor[rd];

	// Input checks
	if (rset==NULL) {
		printk("sys_rk_cpu_reserve_create: Cpu reserves cannot be created for a Null resource set.\n");
		goto error_sem_unlock;
	}
	if (cpu_attr == NULL) {
		printk("sys_rk_cpu_reserve_create: Cpu attributes must be specified for creating a cpu reserve.\n");
		goto error_sem_unlock;
	}
	if (cpu_attr->period.tv_nsec == 0 && cpu_attr->period.tv_sec == 0) {
		printk("sys_rk_cpu_reserve_create: Task has a period of 0. Such tasks cannot be handled.\n");
		goto error_sem_unlock;
	}
	if (timespec_gt(cpu_attr->deadline, cpu_attr->period)) {
		printk("sys_rk_cpu_reserve_create: The deadline of a task cannot be greater than the period. %ld . %ld , %ld. %ld\n", 
				cpu_attr->deadline.tv_sec, cpu_attr->deadline.tv_nsec, cpu_attr->period.tv_sec, cpu_attr->period.tv_nsec);	
		goto error_sem_unlock;
	}

	cpus_clear(cpumask);

#ifndef RK_GLOBAL_SCHED
	// Input check: CPU number
	cpunum = cpu_attr->cpunum;
	if (cpunum < 0 || cpunum >= num_cpus) {
		printk("sys_rk_cpu_reserve_create: Invalid cpunum\n");
		goto error_sem_unlock;
	}
	// Need to check this, because hrtimer should be configured within its local CPU context
	if (rk_check_task_cpursv(current) == RK_SUCCESS) {
		printk("sys_rk_cpu_reserve_create: Task with CPU reserve cannot create another CPU reserve (pid %d)\n", current->pid);
		goto error_sem_unlock;
	}
	cpu_set(cpunum, cpumask);
	if (set_cpus_allowed_ptr(current, &cpumask) != 0) {
		printk("sys_rk_cpu_reserve_create: set_cpus_allowed_ptr error\n");
		goto error_set_cpus;
	}
	if (raw_smp_processor_id() != cpunum) {
		printk("sys_rk_cpu_reserve_create: Cannot migrate to CPU %d\n", cpunum);
		goto error_set_cpus;
	}	
#else
	cpunum = cpu_attr->cpunum = -1; // global scheduling does not use cpunum
#endif

    	// create cpu reserve object 
	cpu = kmalloc(sizeof(struct cpu_reserve), GFP_ATOMIC);
        memset(cpu, 0, sizeof(struct cpu_reserve));
    	memcpy(&(cpu->cpu_res_attr), cpu_attr, sizeof(cpu_reserve_attr_data_t));

    	rsv = rk_reserve_create(rset, RSV_CPU);

	// lock rset 
	raw_spin_lock_irqsave(&rset->lock, flags);

	if (rset->nr_cpu_reserves >= RK_MAX_ORDERED_LIST) {
		printk("sys_rk_cpu_reserve_create: cannot create more than %d CPU reserves in a resource set\n", RK_MAX_ORDERED_LIST);
		goto error_spin_unlock;
	}

	// Find cpursv_index
	for (i = 0, cpursv_index = -1; i < RK_MAX_ORDERED_LIST; i++) {
		if (rset->cpu_reserves[i] == NULL) {
			cpursv_index = i;
			break;
		}
	}
	if (cpursv_index < 0) {
		printk("sys_rk_cpu_reserve_create: no more cpursv can be created in rset %d (# of cpursv: %d)\n", rd, rset->nr_cpu_reserves);
		goto error_spin_unlock;
	}

	// Input check: start_time
	rk_rdtsc(&now);
	nano2timespec(cur_time, now);
	start_time_ns = timespec2nano(cpu_attr->start_time);
	if (start_time_ns < now) {
		timespec_set(cpu_attr->start_time, cur_time);
		timespec_add(cpu_attr->start_time, cpu_attr->period);
		start_time_ns = timespec2nano(cpu_attr->start_time);
		cpu->cpu_res_attr.start_time = cpu_attr->start_time;
		printk("rk_cpu_reserve_create: start_time adjusted to %ld.%03lld (now: %ld.%03lld)\n", 
			cpu_attr->start_time.tv_sec, cpu_attr->start_time.tv_nsec / MICROSEC_PER_SEC, 
			cur_time.tv_sec, cur_time.tv_nsec / MICROSEC_PER_SEC);
	}

    	// assigned scheduling policy 
    	cpu->scheduling_policy = cpu_reserves_kernel_scheduling_policy;

	// get vcpu priority (only in a guest vm)
	if (is_virtualized) {
		int vcpu_prio = rk_get_vcpu_priority();
		if (vcpu_prio != RK_ERROR) cpu->vcpu_priority_index = vcpu_prio;
	}

    	// calculate a new capacity 
	cpu->cpu_time_ticks     = timespec2nano(cpu_attr->compute_time);
	cpu->cpu_period_ticks   = timespec2nano(cpu_attr->period);
	cpu->cpu_deadline_ticks = timespec2nano(cpu_attr->deadline);

	// enforcement control
	if (cpu->cpu_time_ticks == cpu->cpu_period_ticks) cpu->do_enforcement = FALSE;
	else cpu->do_enforcement = TRUE;

	cpu->avail_ticks_in_cur_period = cpu->cpu_time_ticks;
	cpu->used_ticks_in_cur_period = 0;

	printk("rk_cpu_reserve_create: rsvidx %d, core %d(cur %d), rd %d, inherit %d\n", cpursv_index, cpu_attr->cpunum, raw_smp_processor_id(), rd, rset->rk_inherit);
	printk("  - C: %lu.%03lld, T: %lu.%03lld, D: %lu.%03lld\n",
		cpu_attr->compute_time.tv_sec, cpu_attr->compute_time.tv_nsec / MICROSEC_PER_SEC,
		cpu_attr->period.tv_sec, cpu_attr->period.tv_nsec / MICROSEC_PER_SEC,
		cpu_attr->deadline.tv_sec, cpu_attr->deadline.tv_nsec / MICROSEC_PER_SEC);
	
	capacity = CAPACITY_OF(cpu->cpu_time_ticks, cpu->cpu_period_ticks);
	cpu->cpu_capacity = capacity;

	timespec_set(cpu->absolute_deadline, cpu_attr->start_time);
	timespec_add(cpu->absolute_deadline, cpu_attr->deadline);

	priority_list_add(cpu, ptr_cpu_reserves_head(cpunum));
	
	// Admission control test 
#ifdef RK_ADMISSION_TEST
	if (!admit_reserve_request(cpu, cpunum)) {
		printk("  - admission control failed: rd %d, core %d\n", rd, cpu_attr->cpunum);
		// Admission test failed 
		priority_list_remove(cpu, ptr_cpu_reserves_head(cpunum));
		goto error_spin_unlock;
	}
#endif
	printk("  - admission control passed: rd %d -> rt_prio %d\n", rd, cpu->cpu_priority_index);

	var_cpu_current_capacity(cpunum) += capacity;

#ifndef RK_GLOBAL_SCHED
	printk("  - cpu current capacity (cpunum %d): %ld\n", cpunum, var_cpu_current_capacity(cpunum));
#else
	printk("  - cpu current capacity (global): %ld\n", var_cpu_current_capacity(cpunum));
#endif

	rsv->reservation_state = RSV_IS_NULL;
    	rsv->reserve = cpu;
    	rsv->operations = &cpu_reserve_ops;
    	rsv->reservation_parameters = cpu_attr->reserve_mode;

    	cpu->rsv = rsv;
	cpu->used_ticks_in_prev_period = 0;
	cpu->cpu_max_utilization = 0;
	cpu->cpu_min_utilization = 10000;	/* The maximum value of cpu_capacity_t */
	cpu->release_time_of_cur_period = 0;

	rset->cpu_reserves[cpursv_index] = rsv;
	rset->nr_cpu_reserves++;
	rsv->reserve_index = cpursv_index;

	raw_spin_unlock_irqrestore(&rset->lock, flags); // OK to unlock here

	rk_procfs_reserve_create(rsv, cpursv_index);
	
   	rk_replenish_timer_create(rsv, start_time_ns - now); // rset->lock not required

	rk_sem_up();

	return cpursv_index;

error_spin_unlock:
	raw_spin_unlock_irqrestore(&rset->lock, flags);
	kfree(cpu);
	kfree(rsv);

#ifndef RK_GLOBAL_SCHED
error_set_cpus:
	cpus_setall(cpumask);
	set_cpus_allowed_ptr(current, &cpumask);
#endif

error_sem_unlock:
	rk_sem_up();
	return RK_ERROR;
}


/* 
 * Should be called with rk_sem held
 *
 * Called by resource_set.c::rk_delete_reserve
 */
void rk_cpu_reserve_delete(cpu_reserve_t cpu)
{
	rk_resource_set_t rset;
	int cpunum;
	unsigned long flags;

	if (cpu == NULL) {
		printk("rk_cpu_reserve_delete: Deleting a NULL reserve\n");
		return;
	}
	rset = cpu->rsv->parent_resource_set;
	if (rset == NULL) {
		printk("rk_cpu_reserve_delete: parent_resource_set is NULL\n");
		return;
	}
	cpunum = cpu->cpu_res_attr.cpunum;

#ifndef RK_GLOBAL_SCHED
	{
		cpumask_t cpumask;
		cpus_clear(cpumask);
		cpu_set(cpunum, cpumask);
		set_cpus_allowed_ptr(current, &cpumask);
	}
#endif

	raw_spin_lock_irqsave(&rset->lock, flags);

	rk_enforce_timer_stop(cpu->rsv, cpunum);
        rk_timer_remove(cpu->rsv->reserve_replenish_timer, cpunum);		

	rk_timer_destroy(cpu->rsv->reserve_enforce_timer, cpunum);
	rk_timer_destroy(cpu->rsv->reserve_replenish_timer, cpunum);

	cpu->rsv->reserve = NULL;	/* After this step, no way to reach the reserve */

	remove_from_pseudo_vcpu_list(NULL, cpu);

	raw_spin_unlock_irqrestore(&rset->lock, flags);
	
	if (priority_list_remove(cpu, ptr_cpu_reserves_head(cpunum)) != 0) {
		printk("rk_cpu_reserve_delete: Could not delete the cpu reserve from the list of cpu reserves\n");
		return;
	}
	var_cpu_current_capacity(cpunum) -= cpu->cpu_capacity;
	if (var_cpu_current_capacity(cpunum) < 0) {
		var_cpu_current_capacity(cpunum) = 0;
	}

	INIT_LIST_HEAD(&(cpu->cpu_link));	/* Just to be sure*/

	if (cpu->vchannel_host) 
#if defined(RK_VCHANNEL_SOCKET)
		sock_release((struct socket*)cpu->vchannel_host);
#elif defined(RK_VCHANNEL_PIPE)
		filp_close((struct file*)cpu->vchannel_host, NULL);
#endif

	memset(cpu, 0, sizeof(struct cpu_reserve));
	kfree(cpu);
}


asmlinkage void sys_rk_setschedulingpolicy(int policy)
{
	/* Minimum sanity check on the input policy */
	if (policy != SCHED_FIFO && policy != SCHED_RR) return;

	/* set the scheduling policy */
	cpu_reserves_kernel_scheduling_policy = policy;
	return;
}

asmlinkage void sys_rk_getcpursv_prev_used_ticks(int rd, unsigned long long *ret)
{
	cpu_reserve_t cpu;
	rk_resource_set_t rset;
	unsigned long flags;
	unsigned long long used_ticks;
	int i;

	if (rd < 0 || rd >= MAX_RESOURCE_SETS) return;

	rk_sem_down();
	rset = resource_set_descriptor[rd];
	if (rset == NULL) goto error_sem_unlock;

	raw_spin_lock_irqsave(&rset->lock, flags);
	if (rset->nr_cpu_reserves <= 0) goto error_spin_unlock;

	used_ticks = 0;
	for (i = 0; i < RK_MAX_ORDERED_LIST; i++) {
		if (rset->cpu_reserves[i] == NULL) continue;
		cpu = rset->cpu_reserves[i]->reserve;
		used_ticks += cpu->used_ticks_in_prev_period;	
	}
	*ret = used_ticks;

error_spin_unlock:
	raw_spin_unlock_irqrestore(&rset->lock, flags);
error_sem_unlock:
	rk_sem_up();
}

asmlinkage void sys_rk_getcpursv_min_utilization(int rd, unsigned long *ret)
{
	cpu_reserve_t cpu;
	rk_resource_set_t rset;
	unsigned long flags;
	unsigned long util;
	int i;

	if (rd < 0 || rd >= MAX_RESOURCE_SETS) return;

	rk_sem_down();
	rset = resource_set_descriptor[rd];
	if (rset == NULL) goto error_sem_unlock;

	raw_spin_lock_irqsave(&rset->lock, flags);
	if (rset->nr_cpu_reserves <= 0) goto error_spin_unlock;

	util = 0;
	for (i = 0; i < RK_MAX_ORDERED_LIST; i++) {
		if (rset->cpu_reserves[i] == NULL) continue;
		cpu = rset->cpu_reserves[i]->reserve;
		util += cpu->cpu_min_utilization;	
	}
	*ret = util;

error_spin_unlock:
	raw_spin_unlock_irqrestore(&rset->lock, flags);
error_sem_unlock:
	rk_sem_up();
}

asmlinkage void sys_rk_getcpursv_max_utilization(int rd, unsigned long *ret)
{
	cpu_reserve_t cpu;
	rk_resource_set_t rset;
	unsigned long flags;
	unsigned long util;
	int i;

	if (rd < 0 || rd >= MAX_RESOURCE_SETS) return;

	rk_sem_down();
	rset = resource_set_descriptor[rd];
	if (rset == NULL) goto error_sem_unlock;

	raw_spin_lock_irqsave(&rset->lock, flags);
	if (rset->nr_cpu_reserves <= 0) goto error_spin_unlock;

	util = 0;
	for (i = 0; i < RK_MAX_ORDERED_LIST; i++) {
		if (rset->cpu_reserves[i] == NULL) continue;
		cpu = rset->cpu_reserves[i]->reserve;
		util += cpu->cpu_max_utilization;	
	}
	*ret = util;

error_spin_unlock:
	raw_spin_unlock_irqrestore(&rset->lock, flags);
error_sem_unlock:
	rk_sem_up();
}

asmlinkage void sys_rk_get_start_of_current_period(unsigned long long *tm)
{
	cpu_reserve_t cpu;
	rk_resource_set_t rset;
	unsigned long flags;

	rset = current->rk_resource_set;
	if (rset == NULL) return;

	raw_spin_lock_irqsave(&rset->lock, flags);
	if (rk_check_task_cpursv(current) == RK_SUCCESS) {
		cpu = __rk_get_task_default_cpursv(current)->reserve;
		*tm = cpu->release_time_of_cur_period;
	}
	raw_spin_unlock_irqrestore(&rset->lock, flags);
}



asmlinkage void sys_rk_get_current_time(unsigned long long *tm)
{
	cpu_reserve_t cpu;
	rk_resource_set_t rset;
	unsigned long flags;
	cpu_tick_data_t now;

	rset = current->rk_resource_set;
	if (rset == NULL) return;
	
	rk_rdtsc(&now);
	raw_spin_lock_irqsave(&rset->lock, flags);
	if (rk_check_task_cpursv(current) == RK_SUCCESS) {
		cpu = rk_get_task_current_cpursv(current)->reserve;
		// previously used ticks + elapsed ticks in current invocation
		*tm = cpu->used_ticks_in_cur_period + (now - cpu->start_time_of_cur_exec);
	}
	raw_spin_unlock_irqrestore(&rset->lock, flags);
}


void cpursv_profile_update(cpu_tick_t start, cpu_tick_t now, cpu_tick_t used_ticks, struct rk_cpu_profile_set *profile)
{
	struct timespec *time;

	if (profile == NULL || profile->max_size == 0) return;

	// release time
	if (start) {
		time = &profile->cur_buf[profile->cur_pos].release;
		if (time->tv_sec == 0 && time->tv_nsec == 0) {
			nano2timespec(*time, *start);	
		}
#ifdef RK_PROFILE_PMC
		get_pmc_info(&profile->start_pmc);
#endif
	}
	// completion time
	if (now) {
		time = &profile->cur_buf[profile->cur_pos].completion;
		nano2timespec(*time, *now);
#ifdef RK_PROFILE_PMC
		if (start != now) {
			struct pmc_counter cur_pmc;
			struct pmc_counter *pmc;
			get_pmc_info(&cur_pmc);
			pmc = &profile->cur_buf[profile->cur_pos].pmc;

                        pmc->inst_retired_any += cur_pmc.inst_retired_any - profile->start_pmc.inst_retired_any;
                        pmc->cpu_clk_unhalted += cur_pmc.cpu_clk_unhalted - profile->start_pmc.cpu_clk_unhalted;
                        pmc->l1_hit           += cur_pmc.l1_hit - profile->start_pmc.l1_hit;
                        pmc->l2_hit           += cur_pmc.l2_hit - profile->start_pmc.l2_hit;
                        pmc->l3_hit           += cur_pmc.l3_hit - profile->start_pmc.l3_hit;
                        pmc->l3_miss          += cur_pmc.l3_miss - profile->start_pmc.l3_miss;
                        pmc->invariant_tsc    += cur_pmc.invariant_tsc - profile->start_pmc.invariant_tsc;
		}
#endif
	}
	// used ticks
	if (used_ticks) {
		profile->used_ticks += *used_ticks;
	}
}

static inline void cpursv_profile_utilization_helper(struct rk_cpu_profile_set *profile, cpu_reserve_t cpu)
{
	unsigned long flags;
	if (profile == NULL || profile->max_size == 0) return;

	// since now we change cur_pos, locking is required here.
	raw_spin_lock_irqsave(&profile->lock, flags);

	if (profile->used_ticks > 0) {
		profile->cur_buf[profile->cur_pos].utilization 
			= div64_s64(profile->used_ticks * 10000, cpu->cpu_period_ticks);
	}
	else {
		profile->cur_buf[profile->cur_pos].utilization = 0;
	}
	profile->cur_pos++;

	if (profile->cur_pos >= profile->max_size) 
		profile->cur_pos = 0;
	if (profile->cur_size < profile->max_size) 
		profile->cur_size++;

	profile->used_ticks = 0; 
	profile->cur_buf[profile->cur_pos].release.tv_sec = 0;
	profile->cur_buf[profile->cur_pos].release.tv_nsec = 0;
#ifdef RK_PROFILE_PMC
	profile->cur_buf[profile->cur_pos].pmc.inst_retired_any = 0;
	profile->cur_buf[profile->cur_pos].pmc.cpu_clk_unhalted = 0;
	profile->cur_buf[profile->cur_pos].pmc.l1_hit = 0;
	profile->cur_buf[profile->cur_pos].pmc.l2_hit = 0;
	profile->cur_buf[profile->cur_pos].pmc.l3_hit = 0;
	profile->cur_buf[profile->cur_pos].pmc.l3_miss = 0;
	profile->cur_buf[profile->cur_pos].pmc.invariant_tsc = 0;
#endif
	raw_spin_unlock_irqrestore(&profile->lock, flags);	
}

void cpursv_profile_utilization(cpu_reserve_t cpu, unsigned long util)
{
	struct task_struct *task;
	// reserve
	cpursv_profile_utilization_helper(&cpu->cpu_profile, cpu);

	// task level
	list_for_each_entry(task, &cpu->rsv->parent_resource_set->task_list, rk_resource_set_link) {
		if (task->rk_profile == NULL) continue;	
		if (rk_check_task_cpursv(task) == RK_ERROR) continue;

		// Update task's utilization only if cpu is the task's default cpursv
		if (cpu != __rk_get_task_default_cpursv(task)->reserve) continue;
	     	cpursv_profile_utilization_helper(task->rk_profile, cpu);
	}
}

static inline int rk_getcpursv_start_profile_helper(struct rk_cpu_profile_set *profile, int size)
{
	struct rk_cpu_profile *buf1, *buf2;
	if (size <= 1 || size > CPU_PROFILE_DATA_MAX) {
		printk("rk_getcpursv_start_profile_helper: invalid size (1 < size < %d)\n", CPU_PROFILE_DATA_MAX);
		return RK_ERROR;
	}
	if (profile == NULL) {
		printk("rk_getcpursv_start_profile_helper: profile is NULL\n");
		return RK_ERROR;
	}
	if (profile->max_size) {
		printk("rk_getcpursv_start_profile_helper: profiling already started (size:%d)\n", profile->max_size);
		return RK_SUCCESS;
	}

	buf1 = kmalloc(size * sizeof(struct rk_cpu_profile), GFP_ATOMIC);
	if (buf1 == NULL) {
		return RK_ERROR;
	}
	buf2 = kmalloc(size * sizeof(struct rk_cpu_profile), GFP_ATOMIC);
	if (buf2 == NULL) {
		kfree(profile->buf[0]);
		return RK_ERROR;
	}
	memset(buf1, 0, size * sizeof(struct rk_cpu_profile));
	memset(buf2, 0, size * sizeof(struct rk_cpu_profile));

	raw_spin_lock_init(&profile->lock);
	profile->buf[0] = buf1;
	profile->buf[1] = buf2;
	profile->cur_buf = buf1;
	profile->cur_pos = 0;
	profile->cur_size = 0;
	profile->used_ticks = 0;
	smp_mb();

	profile->max_size = size; 
	return RK_SUCCESS;
}

static inline int rk_getcpursv_get_profile_helper(struct rk_cpu_profile_set *profile, void *data)
{
	int start, end, cur_size, max_size;
	unsigned long flags;
	struct rk_cpu_profile *buf;

	if (data == NULL) {
		printk("rk_getcpursv_get_profile_helper: invalid user address\n");
		return RK_ERROR;
	}
	if (profile == NULL) {
		printk("rk_getcpursv_get_profile_helper: profile is NULL\n");
		return RK_ERROR;
	}
	if (profile->max_size == 0) {
		printk("rk_getcpursv_get_profile_helper: profile data not exist\n");
		return RK_ERROR;
	}

	// protect profile index values which can be modified by another core
	raw_spin_lock_irqsave(&profile->lock, flags);

	cur_size = profile->cur_size;
	max_size = profile->max_size;
	if (cur_size == 0) {
		// nothing to copy
		raw_spin_unlock_irqrestore(&profile->lock, flags);	
		return 0;
	}
	buf = profile->cur_buf;
	if (profile->cur_buf == profile->buf[0]) profile->cur_buf = profile->buf[1];
	else profile->cur_buf = profile->buf[0];

	// Exclude data which may be currently being written 
	profile->cur_buf[0] = buf[profile->cur_pos];
	if (cur_size == max_size) {
		cur_size--; 
	}

	start = profile->cur_pos - cur_size;
	if (start < 0) start += max_size;
	end = profile->cur_pos;
	if (end == 0) end = max_size;

	profile->cur_size = 0;
	profile->cur_pos = 0;
	raw_spin_unlock_irqrestore(&profile->lock, flags);	

	if (start < end) {
		if (copy_to_user(data, &buf[start], cur_size * sizeof(struct rk_cpu_profile))) {
			printk("rk_getcpursv_get_profile_helper: copy_to_user error\n");
		}
	}
	else {
		if (copy_to_user(data, &buf[start], (max_size - start) * sizeof(struct rk_cpu_profile))) {
			printk("rk_getcpursv_get_profile_helper: copy_to_user error\n");
		}
		data = &((struct rk_cpu_profile*)data)[max_size - start];
		if (copy_to_user(data, &buf[0], end * sizeof(struct rk_cpu_profile))) {
			printk("rk_getcpursv_get_profile_helper: copy_to_user error\n");
		}
	}

	return cur_size;
}

static inline int rk_getcpursv_profile(int mode, int rd, void *data1, void *data2)
{
	rk_resource_set_t	rset;
	cpu_reserve_t 		cpu;
	int ret = RK_ERROR;
	int cpursv_index = (long)data1;
	
	if (rd < 0 || rd >= MAX_RESOURCE_SETS) {
		printk("rk_getcpursv_profile: invalid rset id %d\n", rd);
		return RK_ERROR;
	}

	rk_sem_down();
	rset = resource_set_descriptor[rd];
	if (rset == NULL) {
		printk("rk_getcpursv_profile: rset %d is NULL\n", rd);
		goto sem_unlock;
	}
	if (rset->nr_cpu_reserves <= 0) {
		printk("rk_getcpursv_profile: rset %d does not have cpu reserve\n", rd);
		goto sem_unlock;
	}
	if (cpursv_index < 0 || cpursv_index >= RK_MAX_ORDERED_LIST) {
		printk("rk_getcpursv_profile: invalid cpursv_index %d\n", cpursv_index);
		goto sem_unlock;
	}
	if (rset->cpu_reserves[cpursv_index] == NULL) {
		printk("rk_getcpursv_profile: cpursv_index %d does not exist\n", cpursv_index);
		goto sem_unlock;
	}
	cpu = rset->cpu_reserves[cpursv_index]->reserve;

	// Start profiling
	if (mode == 0) {
		int size = (long)data2;
		ret = rk_getcpursv_start_profile_helper(&cpu->cpu_profile, size);
	}
	// Get profile data
	else if (mode == 1) {
		ret = rk_getcpursv_get_profile_helper(&cpu->cpu_profile, data2);
	}
sem_unlock:
	rk_sem_up();
	return ret;
}

static inline int rk_getcpursv_task_profile(int mode, int pid, void *data)
{
	struct task_struct	*task;
	rk_resource_set_t	rset;
	int ret = RK_ERROR;
	
	rk_sem_down();
	task = find_task_by_pid_ns(pid, &init_pid_ns);

	if (task == NULL) {
		printk("rk_getcpursv_task_profile: Could not find task with pid %d\n", pid);
		goto sem_unlock;
	}
	if (task->rk_resource_set == NULL) {
		printk("rk_getcpursv_task_profile: task %d is not attached to a resource set\n", pid);
		goto sem_unlock;
	}
	rset = task->rk_resource_set;
	if (rset->nr_cpu_reserves <= 0 || task->rk_cpursv_list->n <= 0) {
		printk("rk_getcpursv_task_profile: task %d does not have cpu reserve\n", pid);
		goto sem_unlock;
	}

	// Start task profiling
	if (mode == 0) {
		int size = (long)data;
		struct rk_cpu_profile_set *profile;
		if (task->rk_profile) {
			printk("rk_getcpursv_task_profile: profiling already started\n");
			ret = RK_SUCCESS;
			goto sem_unlock;
		}
		profile = kmalloc(sizeof(struct rk_cpu_profile_set), GFP_ATOMIC);
		if (profile == NULL) {
			printk("rk_getcpursv_task_profile: kmalloc error (rk_cpu_profile_set)\n");
			goto sem_unlock;
		}
		memset(profile, 0, sizeof(struct rk_cpu_profile_set));

		ret = rk_getcpursv_start_profile_helper(profile, size);
		if (ret == RK_ERROR) {
			kfree(profile);
		}
		else {
			task->rk_profile = profile;
		}
	}
	// Get profile data
	else if (mode == 1) {
		ret = rk_getcpursv_get_profile_helper(task->rk_profile, data);
	}
sem_unlock:
	rk_sem_up();
	return ret;
}

asmlinkage int sys_rk_getcpursv_profile(int mode, int id, void *data1, void *data2)
{
	// CPU Reserve profiling
	if (mode == 0 || mode == 1) {
		return rk_getcpursv_profile(mode, id, data1, data2);
	}
	// Task profiling
	else if (mode == 2 || mode == 3) {
		return rk_getcpursv_task_profile(mode - 2, id, data1);
	}
	printk("sys_rk_getcpursv_profile: invalid mode %d\n", mode);
	return RK_ERROR;
}

// for procfs
int cpu_reserve_read_proc(rk_reserve_t rsv, char *buf)
{
	char *p = buf;
	cpu_reserve_t cpu;
	cpu_reserve_attr_data_t attr;

	rk_sem_down();
	
	if (rsv == NULL || rsv->reserve == NULL) {
		rk_sem_up();
		return 0;
	}
	cpu = rsv->reserve;
	attr = cpu->cpu_res_attr;

	rk_sem_up();

	p += sprintf(p, "compute_time  : %lu.%09lu\n", attr.compute_time.tv_sec,   attr.compute_time.tv_nsec);
	p += sprintf(p, "period        : %lu.%09lu\n", attr.period.tv_sec,         attr.period.tv_nsec);
	p += sprintf(p, "deadline      : %lu.%09lu\n", attr.deadline.tv_sec,       attr.deadline.tv_nsec);
	p += sprintf(p, "blocking_time : %lu.%09lu\n", attr.blocking_time.tv_sec,  attr.blocking_time.tv_nsec);
	p += sprintf(p, "release_jitter: %lu.%09lu\n", attr.release_jitter.tv_sec, attr.release_jitter.tv_nsec);
	p += sprintf(p, "start_time    : %lu.%09lu\n", attr.start_time.tv_sec,     attr.start_time.tv_nsec);

	p += sprintf(p, "rsv_mode(sch) : %d\n", attr.reserve_mode.sch_mode);
	p += sprintf(p, "rsv_mode(enf) : %d\n", attr.reserve_mode.enf_mode);
	p += sprintf(p, "rsv_mode(rep) : %d\n", attr.reserve_mode.rep_mode);

	p += sprintf(p, "cpunum        : %d\n", attr.cpunum);
	p += sprintf(p, "rt_priority   : %d\n", cpu->cpu_priority_index);
	p += sprintf(p, "SIG_RK_ENF    : %d\n", (int)attr.notify_when_enforced);
	p += sprintf(p, "rsv_state     : %x\n", rsv->reservation_state);
	p += sprintf(p, "is_pseudo_vcpu: %d\n", is_pseudo_vcpu(cpu));

	return (p - buf);
}

