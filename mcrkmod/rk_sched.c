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
 * rk_sched.c: contains the scheduler hook that starts and stops rk accounts for each process
 */
#include <linux/module.h>
#include <rk/timespec.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <rk/rk_mc.h>
#include <rk/rk_mutex.h>
#include <rk/rk_virt.h>


#define RATE_MONOTONIC			0
#define DEADLINE_MONOTONIC		1
#define EDF				2
#define NUM_CPU_RESERVE_POLICIES 	(EDF+1)

extern int debug_rd;
void rk_schedule(struct task_struct *prev, struct task_struct *next)
{	
  	cpu_tick_data_t now;
  	rk_resource_set_t prev_rs;
  	rk_resource_set_t next_rs;
  	unsigned long flags = 0;

  	prev_rs = prev->rk_resource_set;
  	next_rs = next->rk_resource_set;

  	if (prev_rs || next_rs) rk_rdtsc(&now);

	// In-order double rset locking (avoiding deadlock)
	if (prev_rs && next_rs == NULL) {
		raw_spin_lock_irqsave(&prev_rs->lock, flags);
	}
	else if (prev_rs == NULL && next_rs) {
		raw_spin_lock_irqsave(&next_rs->lock, flags);
	}
	else if (prev_rs && next_rs) {
		if (prev_rs == next_rs) {
			raw_spin_lock_irqsave(&prev_rs->lock, flags);
		}
		else if (prev_rs < next_rs) {
			raw_spin_lock_irqsave(&prev_rs->lock, flags);
			raw_spin_lock(&next_rs->lock);
		}
		else {
			raw_spin_lock_irqsave(&next_rs->lock, flags);
			raw_spin_lock(&prev_rs->lock);
		}
	}
	smp_mb(); 

	// Stop and start resource accounting
	if (((volatile rk_resource_set_t*)prev->rk_resource_set)) {	
		rk_reserve_t cpursv = __get_cpu_var(rk_current_cpu_reserve); 
		if (cpursv && (cpursv->reservation_state & RSV_IS_RUNNING)) {
			cpu_reserve_stop_account(cpursv, &now);
		}
		// Clear task running state
		prev->rk_cannot_schedule &= ~RK_TASK_RUNNING;
#ifndef RK_GLOBAL_SCHED
		// Partitioned sched: do nothing
#else
		// Global scheduling
		// Note: rk_current_cpu_reserve may be NULL, if prev was suspended by rk_schedule().
		cpursv = rk_get_task_current_cpursv(prev);
		if (cpursv && !(cpursv->reservation_state & RSV_IS_RUNNING) && !((cpu_reserve_t)cpursv->reserve)->waking_task) {
			__get_cpu_var(rk_post_schedule_wakeup) = rk_get_next_task_in_cpursv(prev_rs, cpursv, prev, NULL);
			((cpu_reserve_t)cpursv->reserve)->waking_task = __get_cpu_var(rk_post_schedule_wakeup);
		}
#endif
		if (prev->rk_resource_set->rd_entry == debug_rd) {
			printk("prev: pid %d state %ld rk_state %d currsv %d taskrsv %d tskrsvstt %d cpuid %d\n", 
				prev->pid, prev->state, prev->rk_cannot_schedule, 
				__get_cpu_var(rk_current_cpu_reserve) ? __get_cpu_var(rk_current_cpu_reserve)->reserve_index : -1, 
				cpursv ? cpursv->reserve_index : -1, cpursv ? cpursv->reservation_state : -1, raw_smp_processor_id());
		}
	}
	__get_cpu_var(rk_current_cpu_reserve) = NULL;
	if (((volatile rk_resource_set_t*)next->rk_resource_set)) {	
		rk_reserve_t cpursv = rk_get_task_current_cpursv(next); 
		// Set task running state
		next->rk_cannot_schedule |= RK_TASK_RUNNING;
		if (cpursv) {
			// Check if the rsv is depleted.
			//if (cpursv->reservation_state & RSV_IS_DEPLETED) {
			if (((cpu_reserve_t)cpursv->reserve)->do_enforcement && (cpursv->reservation_state & RSV_IS_DEPLETED)) {
				// We do not change the task state, because it cannot be deactivated here.
				// Leave the task state as it is. It will be deactivated after context switching.
				// __set_task_state(next, TASK_UNINTERRUPTIBLE); // Should not change here
				set_tsk_need_resched(next);
				next->rk_cannot_schedule |= RK_TASK_UNSCHEDULABLE;
			}
#ifndef RK_GLOBAL_SCHED
			// Partitioned scheduling
			// Check if rsv is still being used by another task. 
			else if ((cpursv->reservation_state & RSV_IS_RUNNING)
			         || ((cpu_reserve_t)cpursv->reserve)->cpu_res_attr.cpunum != raw_smp_processor_id()) {
				// Reschedule task
				set_tsk_need_resched(next);
			}
#else
			// Global scheduling
			// Check if rsv is still being used by another task. 
			else if ((cpursv->reservation_state & RSV_IS_RUNNING) 
				 || (((cpu_reserve_t)cpursv->reserve)->waking_task && ((cpu_reserve_t)cpursv->reserve)->waking_task != next)) {
				set_tsk_need_resched(next);
				next->rk_cannot_schedule |= (RK_TASK_UNSCHEDULABLE | RK_TASK_WAIT_FOR_TURN);
				if (next->rk_resource_set->rd_entry == debug_rd) {
					printk("next: pid %d state %ld rk_state %d rsvstt %d cpuid %d (suspend/cur %d wait %d)\n", 
						next->pid, next->state, next->rk_cannot_schedule, cpursv->reservation_state, raw_smp_processor_id(), 
						((cpu_reserve_t)cpursv->reserve)->current_task ? ((cpu_reserve_t)cpursv->reserve)->current_task->pid : 0, 
						((cpu_reserve_t)cpursv->reserve)->waking_task ? ((cpu_reserve_t)cpursv->reserve)->waking_task->pid : 0);
				}
			}
#endif
			else {
				if (next->rk_resource_set->rd_entry == debug_rd) {
					printk("next: pid %d state %ld rk_state %d rsvstt %d cpuid %d\n", 
						next->pid, next->state, next->rk_cannot_schedule, cpursv->reservation_state, raw_smp_processor_id());
				}
				__get_cpu_var(rk_current_cpu_reserve) = cpursv;
				((cpu_reserve_t)cpursv->reserve)->waking_task = NULL;
				cpu_reserve_start_account(cpursv, next, &now);
			}
		}
	}

	// Double rset unlocking
	if (prev_rs && next_rs == NULL) {
		raw_spin_unlock_irqrestore(&prev_rs->lock, flags);
	}
	else if (prev_rs == NULL && next_rs) {
		raw_spin_unlock_irqrestore(&next_rs->lock, flags);
	}
	else if (prev_rs && next_rs) {
		if (prev_rs == next_rs) {
			raw_spin_unlock_irqrestore(&prev_rs->lock, flags);
		}
		else {
			raw_spin_unlock(&prev_rs->lock);
			raw_spin_unlock_irqrestore(&next_rs->lock, flags);
		}
	}
}	


void rk_post_schedule(void)
{
	unsigned long flags;
	struct task_struct *task;

	local_irq_save(flags);
	if (__get_cpu_var(rk_post_schedule_wakeup) == NULL) goto end;

	task = __get_cpu_var(rk_post_schedule_wakeup);
	__get_cpu_var(rk_post_schedule_wakeup) = NULL;

	if (task) wake_up_process(task);
end:
	local_irq_restore(flags);
}


void rk_exit(struct task_struct *tsk)
{	
  	rk_resource_set_t rs;

  	if (tsk->rk_resource_set) {
		if (tsk->rk_cpursv_list && tsk->rk_cpursv_list->n) {
			printk("rk_exit: task %d\n", tsk->pid);
		}
		rs = tsk->rk_resource_set;
		sys_rk_resource_set_detach_process(rs->rd_entry, tsk->pid);
   	}
	else {
		rk_task_cleanup(tsk);
	}
}

void rk_task_cleanup(struct task_struct *tsk)
{
	rk_sem_down();
	if (tsk->rk_profile) {
		struct rk_cpu_profile_set *ptr = tsk->rk_profile;
		tsk->rk_profile = NULL;
		raw_spin_unlock_wait(&ptr->lock);
		kfree(ptr->buf[0]);
		kfree(ptr->buf[1]);
		kfree(ptr);
	}
	if (tsk->rk_trace) {
#ifdef RK_TRACE_SUM
		void *ptr = tsk->rk_trace;
		save_rk_trace_sum(tsk);
		tsk->rk_trace = NULL;
		vfree(ptr);
#else
		struct rk_trace_data_set *ptr = tsk->rk_trace;
		tsk->rk_trace = NULL;
		raw_spin_unlock_wait(&ptr->lock);
		vfree(ptr->buf[0]);
		vfree(ptr->buf[1]);
		vfree(ptr);
#endif 
	}
	rk_sem_up();
	rk_mutex_task_cleanup(tsk);
	remove_from_pseudo_vcpu_list(tsk, NULL);
}


void rk_fork(struct task_struct *tsk)
{
	// Check resource set inherit_flag 
	struct task_struct *parent;
	int rd = -1;
	int inherit_flag = FALSE;

	if (tsk->group_leader != tsk) {
		parent = tsk->group_leader;
	}
	else {
		parent = tsk->real_parent;
	}
	if (parent->rk_resource_set) {
		rd = parent->rk_resource_set->rd_entry;
		inherit_flag = parent->rk_resource_set->rk_inherit;
		if (inherit_flag && parent->rk_resource_set->cpursv_policy != NO_DEFAULT_CPURSV) {
			printk("rk_fork : pid %d - parent: %d, inherit: %d\n", tsk->pid, parent->pid, inherit_flag);
		}
	}
	if (rd < 0) return; 

	if (inherit_flag) {
		sys_rk_resource_set_attach_process(rd, tsk->pid, NULL);
	}
	else {
		// Give parent's original priority
		struct sched_param par;
		par.sched_priority = parent->orig_sched_prio;
		sched_setscheduler_nocheck(tsk, parent->orig_sched_policy, &par);
		//printk("rk_fork : pid %d - parent: %d, inherit: %d\n", tsk->pid, parent->pid, inherit_flag);
	}
	tsk->orig_sched_policy = parent->orig_sched_policy;
	tsk->orig_sched_prio = parent->orig_sched_prio;
}

