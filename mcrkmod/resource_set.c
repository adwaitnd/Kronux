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
 * resource_set.c: Maintains the resource sets and translates calls to the reserves
 */
 
#include <rk/rk_mc.h>
#include <rk/rk_mutex.h>
#include <linux/pid.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nsproxy.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <asm/percpu.h>

void resched_cpu(int cpu);

/* rk_resource_set_root: A list to maintain the list of online resource sets	*/
LIST_HEAD(rk_resource_set_root);

rk_resource_set_t *resource_set_descriptor = NULL;

int num_cpus;
struct semaphore rk_sem;

/* Initializes the resource set management sub system				*/
void rk_resource_set_init(void)
{
	/* Initialize the head of the list of online resource sets		*/
	INIT_LIST_HEAD(&rk_resource_set_root);

        resource_set_descriptor = kmalloc(sizeof(rk_resource_set_t)*(MAX_RESOURCE_SETS), GFP_ATOMIC);
	if(resource_set_descriptor == NULL) {
		printk("Error: Could not allocate memory for resource set descriptor table\n");
	} 
	else {
		memset(resource_set_descriptor, 0, sizeof(rk_resource_set_t)*(MAX_RESOURCE_SETS));
	}
}

/* rk_resource_set_attach_process: Attaches a process to a resource set		*/
asmlinkage  int sys_rk_resource_set_attach_process(int rd, pid_t pid, struct rk_ordered_list *req_cpursv_list)
{
	rk_resource_set_t rset;
	struct task_struct *tmptask;
	struct task_struct *task;
    	unsigned long flags;
	struct rk_ordered_list *cpursv_list;
	int i;

	if (rd < 0 || rd >= MAX_RESOURCE_SETS) {
		printk("rk_resource_set_attach_process: Invalid resource set id\n");
		return RK_ERROR;
	}
	task = find_task_by_pid_ns(pid, &init_pid_ns);

	rk_sem_down();
	rset = resource_set_descriptor[rd];
	if (rset == NULL) {
		goto error_sem_unlock;
	}
	if (task == NULL) {
		printk("rk_resource_set_attach_process: Could not find pid %d\n", pid);
		goto error_sem_unlock;
	}
	if (task->rk_resource_set) {
		printk("rk_resource_set_attach_process: task %d already has a resource set (rd %d, name %s)\n",
			pid, task->rk_resource_set->rd_entry, task->rk_resource_set->name);
		goto error_sem_unlock;
	}
	if (task->flags & PF_EXITING) {
		printk("rk_resource_set_attach_process: task %d is in PF_EXITING. cannot be attached\n", pid);
		goto error_sem_unlock;
	}
	// error check for cpursv_list
	cpursv_list = kmalloc(sizeof(struct rk_ordered_list), GFP_ATOMIC);
	if (cpursv_list == NULL) {
		printk("rk_resource_set_attach_process: cannot allocate memory\n");
		goto error_sem_unlock;
	}
	memset(cpursv_list, 0, sizeof(struct rk_ordered_list));
	if (req_cpursv_list) {
		if (copy_from_user(cpursv_list, req_cpursv_list, sizeof(struct rk_ordered_list)) != 0) {
			printk("sys_rk_resource_set_attach_process: Could not copy the given cpursv_list into kernel space\n");
			goto error_mem_free;
		}
		if (cpursv_list->n < 0 || cpursv_list->n > RK_MAX_ORDERED_LIST) {
			printk("sys_rk_resource_set_attach_process: cpursv_list error (n: %d)\n", cpursv_list->n);
			goto error_mem_free;
		}
		if (rset->cpursv_policy == CPURSV_NO_MIGRATION && cpursv_list->n != 1) {
			printk("sys_rk_resource_set_attach_process: rset %d uses no migration policy (cpursv_list->n should be 1)\n", rd);
			goto error_mem_free;
		}
		for (i = 0; i < cpursv_list->n; i++) {
			if (cpursv_list->elem[i] < 0 || cpursv_list->elem[i] > RK_MAX_ORDERED_LIST) {
				printk("sys_rk_resource_set_attach_process: cpursv_list.elem[%d] error (%d)\n", i, cpursv_list->elem[i]);
				goto error_mem_free;
			}
		}
	}
	task->rk_cannot_schedule |= RK_TASK_BEING_ATTACHED;

#ifdef CONFIG_RK_MEM
	if (rset->mem_reserve) {
		struct siginfo sig;
		memset(&sig, 0, sizeof(sig));
		sig.si_signo = SIGSTOP;
		sig.si_code  = SI_KERNEL;

		// Stop the target task
		if (task != current) {
			do_send_sig_info(SIGSTOP, &sig, task, false);

			for (i = 0; i < HZ; i++) {
				schedule_timeout(1);
				if (task->state) break;
			}
		}
		mem_reserve_attach_process(rset->mem_reserve->reserve, task);
		// Resume the task
		if (task != current) {
			sig.si_signo = SIGCONT;
			do_send_sig_info(SIGCONT, &sig, task, false);
		}
	}
#endif

	raw_spin_lock_irqsave(&rset->lock, flags);

	// If cpursv_list is empty, set the task's cpursv_list based on the rset's policy
	if (cpursv_list->n == 0) {
		// No default CPU reserve
		if (rset->cpursv_policy == NO_DEFAULT_CPURSV) {
			; // do nothing
		}
		// No migration
		else if (rset->cpursv_policy == CPURSV_NO_MIGRATION) {
			// Pick an eligible cpursv 
			cpursv_list->n = 1;
			if (rset->nr_cpu_reserves <= 0) {
				// hyos: better way to handle it?
				cpursv_list->elem[0] = 0; 
			}
			else {
				int count[RK_MAX_ORDERED_LIST] = {0,};
				int min_count_id = -1;
				list_for_each_entry(tmptask, &rset->task_list, rk_resource_set_link) {
					count[(int)tmptask->rk_cpursv_list->elem[0]]++;
				}
				for (i = 0; i < RK_MAX_ORDERED_LIST; i++) {
					if (rset->cpu_reserves[i] == NULL) continue;
					if (min_count_id < 0 || count[i] < count[min_count_id]) min_count_id = i;
				}
				cpursv_list->elem[0] = min_count_id;
			}
		}
		// Default migration: tasks can use all cpursvs in rset
		else if (rset->cpursv_policy == CPURSV_MIGRATION_DEFAULT) {
			cpursv_list->n = RK_MAX_ORDERED_LIST;
			for (i = 0; i < cpursv_list->n; i++) {
				cpursv_list->elem[i] = i;
			}
			if (rset->nr_cpu_reserves > 0) {
				for (i = 0; i < RK_MAX_ORDERED_LIST; i++) {
					if (rset->cpu_reserves[i] == NULL) continue;
					break;
				}
			}
		}
		// Migration for fork-join model (CPURSV_MIGRATION_FORKJOIN)
		else if (rset->cpursv_policy == CPURSV_MIGRATION_FORKJOIN) {
			// check if the task is the master string (or we have no choice for selecting cpursvs)
			if (rset->nr_cpu_reserves <= 0) {
				// hyos: better way to handle it?
				cpursv_list->elem[0] = 0; 
			}
			else {
				int count[RK_MAX_ORDERED_LIST] = {0,};
				int min_count_id = -1;
				int master_string_id = -1;
				// find the master cpursv, and a cpursv for the next parallel segment
				list_for_each_entry(tmptask, &rset->task_list, rk_resource_set_link) {
					count[(int)tmptask->rk_cpursv_list->elem[0]]++;
				}
				for (i = 0; i < RK_MAX_ORDERED_LIST; i++) {
					if (rset->cpu_reserves[i] == NULL) continue;
					if (master_string_id < 0) master_string_id = i;
					else if (min_count_id < 0 || count[i] < count[min_count_id]) min_count_id = i;
				}
				// master task
				if (list_empty(&rset->task_list)) {
					cpursv_list->n = 1;
					cpursv_list->elem[0] = master_string_id;
				}
				// parallel task
				else {
					if (min_count_id >= 0) {
						cpursv_list->n = 2;
						cpursv_list->elem[0] = min_count_id;
						cpursv_list->elem[1] = master_string_id;
					}
					else {
						cpursv_list->n = 1;
						cpursv_list->elem[0] = master_string_id;
					}
				}
			}
		}
	}
	// Save task's original scheduling policy and priority
	task->orig_sched_policy = task->policy;
	task->orig_sched_prio = task->rt_priority;

	// Save cpursv_list in the task's tcb
	task->rk_cpursv_list = cpursv_list;

	// Invalidate the task's current cpursv index (it will get a valid index when replenished)
	task->rk_cpursv_list->cur_idx = -1;

	// Set rset in the task's tcb
	task->rk_resource_set = rset;

	// Add the task into rset's task list
	rk_list_add(&(task->rk_resource_set_link), &(rset->task_list));

	if (task != current) {
		task->rk_cannot_schedule &= ~RK_TASK_BEING_ATTACHED;
	}
	raw_spin_unlock_irqrestore(&rset->lock, flags);

	if (task->rk_cpursv_list->n) {
		printk("rk_resource_set_attach_process: pid %d, rd %d, n_cpursv %d ( ", task->pid, task->rk_resource_set->rd_entry, cpursv_list->n);
		for (i = 0; i < cpursv_list->n; i++) {
			printk("%d ", cpursv_list->elem[i]);
		}
		printk(")\n");
	}
	rk_procfs_rset_attach_process(rset, task->pid);
	rk_sem_up(); 

	// Note: Set CPU core ID for task is not needed, because it will be set when the CPU reserve starts
	//printk("[] attached: pid %d, curpid %d, curcpu %d\n", task->pid, current->pid, raw_smp_processor_id());
	
	// Managing thread group
	if (thread_group_leader(task) && task->rk_resource_set->rk_inherit) {
for_child_threads:
		rcu_read_lock();
		tmptask= task;
		while_each_thread(task, tmptask) {
			int child_pid = tmptask->pid;
			if (tmptask->rk_resource_set) continue;
			if (tmptask->flags & PF_EXITING) continue;

			rcu_read_unlock();
			sys_rk_resource_set_attach_process(rd, child_pid, req_cpursv_list);
			goto for_child_threads;
		}
		rcu_read_unlock();
	}

	if (task == current) {
		task->rk_cannot_schedule &= ~RK_TASK_BEING_ATTACHED;
	}
	return RK_SUCCESS;

error_mem_free:
	kfree(cpursv_list);

error_sem_unlock:
	rk_sem_up();
	return RK_ERROR;
}


/* rk_resource_set_detach_process: Detaches a process from a resource set	*/
asmlinkage  int sys_rk_resource_set_detach_process(int rd, pid_t pid)
{
	rk_resource_set_t rset;
	struct task_struct *task;
	struct task_struct *tmptask;
	int cpunum = 0;
    	unsigned long int flags;
	struct rk_ordered_list *cpursv_list;
	cpu_tick_data_t now;
	rk_reserve_t tmprsv;

	task = find_task_by_pid_ns(pid, &init_pid_ns);
	if (task == NULL) return RK_ERROR;

	rk_sem_down();
	rset = task->rk_resource_set;
	if (rset == NULL) {
		goto error_sem_unlock;
	}
	if (current->rk_resource_set && current->rk_resource_set != rset) {
		printk("rk_resource_set_detach_process: A task with a resource set cannot detach the tasks from other resource sets\n");
		goto error_sem_unlock;
	}
	
	if (task->rk_cpursv_list && task->rk_cpursv_list->n) {
		printk("rk_resource_set_detach_process: pid %d, rd %d, core %d, rt_prio %d\n", task->pid, task->rk_resource_set->rd_entry, cpunum, task->rt_priority);
	}

	// Suspend the target task
	raw_spin_lock_irqsave(&rset->lock, flags);
	
	tmptask = NULL; // for global scheduling
	tmprsv = NULL;
	task->rk_cannot_schedule |= RK_TASK_TO_BE_DETACHED;
	if (task != current) {
		rk_suspend_task_now(task);

		while (task_curr(task)) {
			raw_spin_unlock_irqrestore(&rset->lock, flags);
			cpu_relax();
			raw_spin_lock_irqsave(&rset->lock, flags);
		}
	}
	else {
		// task == current
		task->rk_cannot_schedule |= RK_TASK_UNSCHEDULABLE;
		if (__get_cpu_var(rk_current_cpu_reserve)) {
			tmprsv = __get_cpu_var(rk_current_cpu_reserve);
			rk_rdtsc(&now);
			// Stop current task's enforcement timer. 
			// Current task will not be suspended by RK anymore.
			cpu_reserve_stop_account(tmprsv, &now);

#ifndef RK_GLOBAL_SCHED
			// Partitioned sched: do nothing
#else
			// Global scheduling: Activate the next task in tmprsv
			tmptask = rk_get_next_task_in_cpursv(rset, tmprsv, task, task);
#endif

			__get_cpu_var(rk_current_cpu_reserve) = NULL;
		}
	}	
	
	rk_list_del(&(task->rk_resource_set_link)); // Task is now detached from rset
	cpursv_list = task->rk_cpursv_list;
	task->rk_resource_set = NULL;
	task->rk_cpursv_list = NULL;

	raw_spin_unlock_irqrestore(&rset->lock, flags);

	// Restore to the original priority
	// - sched_setscheduler_nocheck() should not be called while holding rset->lock (deadlock)
	// - Since the task is suspended, it is ok to change its priority after unlock.
	if (task->rk_mutex_nested_level <= 0) {
		struct sched_param par;
		par.sched_priority = task->orig_sched_prio;
		sched_setscheduler_nocheck(task, task->orig_sched_policy, &par);
	}
	else {
		task_original_prio(task) = task->orig_sched_prio;
	}
	task->rk_cannot_schedule = RK_TASK_SCHEDULABLE; 

#ifdef CONFIG_RK_MEM
	if (rset->mem_reserve) {
		mem_reserve_detach_process(rset->mem_reserve->reserve, task);
	}
#endif

	// Delete procfs
	rk_procfs_rset_detach_process(task->pid);

	// Delete rset, if no task is attached to rset and rk_auto_cleanup is true.
	if (rset->rk_auto_cleanup && list_empty(&rset->task_list)) {
		printk("rk_resource_set_detach_process: destroy rset %d (cleanup_flag = true)\n", rset->rd_entry);

		/* Last Process in the Resource Set */
		rk_destroy_reserves(rset);	

		rk_list_del(&rset->rset_list);
		resource_set_descriptor[rset->rd_entry] = NULL;

		rk_procfs_rset_destroy(rset);

		memset(rset, 0, sizeof(struct rk_resource_set));
		kfree(rset);  

		rset = NULL;
	}

	rk_sem_up();
	kfree(cpursv_list);

	if (task != current) wake_up_process(task);
#ifndef RK_GLOBAL_SCHED
	// Partitioned sched: do nothing
#else
	// Global scheduling: Activate the selected task
	if (tmptask) wake_up_process(tmptask);
#endif

	rk_task_cleanup(task);
	
	// Thread group
	if (thread_group_leader(task) && rset && rset->rk_inherit) {
for_child_threads:
		rcu_read_lock();
		tmptask = task;
		while_each_thread(task, tmptask) {
			int child_pid = tmptask->pid;
			if (tmptask->rk_resource_set == NULL) continue;

			rcu_read_unlock();
			sys_rk_resource_set_detach_process(rd, child_pid);
			goto for_child_threads;
		}
		rcu_read_unlock();
	}
	return RK_SUCCESS;

error_sem_unlock:
	rk_sem_up();
	return RK_ERROR;
}


/* Creates a new resource set with a given name and attaches it to the process	*/
asmlinkage int sys_rk_resource_set_create(char *name, int inherit_flag, int cleanup_flag, int cpursv_policy)
{
    	char rs_name[RSET_NAME_LEN];
    	rk_resource_set_t rset;
   	unsigned int rd;

    	/* Get the resource set name from the user space				*/
    	if (name == NULL) {
		rs_name[0] = '\0';
    	}
    	else {
		if (copy_from_user(rs_name, name, RSET_NAME_LEN) != 0) {
			printk("sys_rk_resource_set_create: Could not copy the given name into kernel space\n");
			return RK_ERROR;
		}
		rs_name[RSET_NAME_LEN - 1] = '\0';	/* terminate string */
    	}

   	/* Allocate memory and zero it for the new resource set being created	*/
    	rset = kmalloc(sizeof(struct rk_resource_set), GFP_ATOMIC);	    
    	memset(rset, 0, sizeof(struct rk_resource_set));

    	INIT_LIST_HEAD(&(rset->task_list));
	raw_spin_lock_init(&rset->lock);

    	/* Assign the name to the resource set					*/
    	strcpy(rset->name, rs_name);

	if (inherit_flag) rset->rk_inherit = true;
	if (cleanup_flag) rset->rk_auto_cleanup = true;

	rset->cpursv_policy = CPURSV_MIGRATION_DEFAULT;
	if (cpursv_policy == CPURSV_NO_MIGRATION) 
		rset->cpursv_policy = CPURSV_NO_MIGRATION;
	else if (cpursv_policy == CPURSV_MIGRATION_FORKJOIN) 
		rset->cpursv_policy = CPURSV_MIGRATION_FORKJOIN;
	else if (cpursv_policy == NO_DEFAULT_CPURSV)
		rset->cpursv_policy = NO_DEFAULT_CPURSV;

	rk_sem_down();
    	/* Find a free resource set descriptor					*/
    	for (rd = 0; rd < MAX_RESOURCE_SETS; rd ++) {
		if (resource_set_descriptor[rd] == NULL) { 
	   		resource_set_descriptor[rd] = rset;
	  		break;
       		}
    	}
	if (rd == MAX_RESOURCE_SETS) {
		printk("ERROR: Exceeded the Maximum Number of Resource Sets\n");
		rk_sem_up();
		kfree(rset);
		return RK_ERROR;
	}
    	rset->rd_entry = rd;

    	/* Add the resource set to the list of online resource sets			*/
    	rk_list_add(&rset->rset_list, &rk_resource_set_root); 	

    	/* Initialize the wait queue for storing depleted tasks			*/
    	init_waitqueue_head(&rset->depleted_wait);	
    
	rk_procfs_rset_create(rset);

	rk_sem_up();
	printk("rk_resource_set_create: rd %d (%s)\n", rd, rs_name);

    	return rd;
}


/*  Destroys the passed resource set and frees the memory allocated to it	*/
asmlinkage int sys_rk_resource_set_destroy(int rd)
{
    	rk_resource_set_t rset;
    	struct task_struct *task;
    	unsigned long int flags;
	int pid, ret = RK_ERROR;
    
	if (rd < 0 || rd >= MAX_RESOURCE_SETS) {
		printk("rk_resource_set_destroy: Invalid resource set id\n");
		return RK_ERROR;
	}
    	printk("sys_rk_resource_set_destroy: Delete resource set %d\n", rd);
	while (1) {
		rk_sem_down();
		rset = resource_set_descriptor[rd];
		if (rset == NULL) break;
		if (current->rk_resource_set && current->rk_resource_set != rset) {
			printk("sys_rk_resource_set_destroy: Task with a resource set cannot destroy other resource sets\n");
			break;
		}

		ret = RK_SUCCESS;
		raw_spin_lock_irqsave(&rset->lock, flags);
		
		if (rset->task_list.next != &rset->task_list) {
			task = list_entry(rset->task_list.next, struct task_struct, rk_resource_set_link);
			pid = task->pid;
			raw_spin_unlock_irqrestore(&rset->lock, flags);
			rk_sem_up();

			sys_rk_resource_set_detach_process(rd, pid);
		}
		else { // No tasks are attached to rset
			raw_spin_unlock_irqrestore(&rset->lock, flags);	

			rk_destroy_reserves(rset);	

			rk_list_del(&rset->rset_list);
			resource_set_descriptor[rset->rd_entry] = NULL;

			rk_procfs_rset_destroy(rset);

			memset(rset, 0, sizeof(struct rk_resource_set));
			kfree(rset);  
			break;
		}
	}
	rk_sem_up();
	return ret;
}


void rk_resource_set_destroy_all(void)
{
	int rd;
	/* Find a free resource set descriptor					*/
    	for (rd = 0; rd < MAX_RESOURCE_SETS; rd ++) {
		if (resource_set_descriptor[rd] != NULL) { 
			sys_rk_resource_set_destroy(rd);
       		}
    	}
}


/* Create a reserve for a given resource set */
rk_reserve_t rk_reserve_create(rk_resource_set_t rs, rk_reserve_type_t type)
{
    	rk_reserve_t rsv;

    	rsv = kmalloc(sizeof(struct rk_reserve),GFP_ATOMIC);
    	memset(rsv, 0, sizeof(struct rk_reserve));

    	rsv->reservation_type = type;
    	rsv->parent_resource_set = rs;

    	return rsv;
}


/* 
 * Should be called with rk_sem held
 *
 * Called by 
 * - rk_destroy_reserves
 * - sys_rk_mem_reserve_delete
 */
void rk_delete_reserve(rk_reserve_t rsv, int index)
{
	if (rsv == NULL) return;

	if (rsv->reservation_type == RSV_CPU) {
		cpu_reserve_t cpu = rsv->reserve;

		rk_procfs_reserve_destroy(rsv, index);

		rk_cpu_reserve_delete(cpu);
		memset(rsv, 0, sizeof(struct rk_reserve));
		kfree(rsv);
	}
#ifdef CONFIG_RK_MEM
	else if (rsv->reservation_type == RSV_MEM) {
		mem_reserve_t mem = rsv->reserve;

		rk_procfs_reserve_destroy(rsv, index);

		rk_mem_reserve_delete(mem);
		memset(rsv, 0, sizeof(struct rk_reserve));
		kfree(rsv);
	}
#endif
}

/* 
 * Should be called with rk_sem held
 * 
 * Called by
 * - sys_rk_resource_set_detach_process
 * - sys_rk_resource_set_destroy
 */
void rk_destroy_reserves(rk_resource_set_t rset)
{
	int i;
	rk_reserve_t rsv;
	if (rset != NULL) {
		if (rset->nr_cpu_reserves) {
			for (i = 0; i < RK_MAX_ORDERED_LIST; i++) {
				if (!rset->cpu_reserves[i]) continue;
				rset->nr_cpu_reserves--;
				rsv = rset->cpu_reserves[i];
				rset->cpu_reserves[i] = NULL;
				rk_delete_reserve(rsv, i);
			}
		}
		if (rset->mem_reserve) {
			rsv = rset->mem_reserve;
			rset->mem_reserve = NULL;
			rk_delete_reserve(rsv, 0);	
		}
	}
}


