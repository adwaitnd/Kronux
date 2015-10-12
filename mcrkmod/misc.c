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
 * misc.c: contains miscallaneous routines to initialize and cleanup RK module
 */

#include <linux/kernel.h>
#include <linux/unistd.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <rk/rk_mc.h>
#include <rk/rk_virt.h>

/* Import the system call table		*/
extern void *	sys_call_table[];

void *sys_ni_syscall;

asmlinkage int sys_rk_is_scheduling(void)
{
	return (current->rk_cannot_schedule & RK_TASK_SCHED_MASK) == RK_TASK_SCHEDULABLE;
}

void rk_system_calls_init(void)
{
	sys_ni_syscall = sys_call_table[__NR_rk_resource_set_create];
	sys_call_table[__NR_rk_resource_set_create]		= &sys_rk_resource_set_create;
	sys_call_table[__NR_rk_resource_set_destroy]		= &sys_rk_resource_set_destroy;
	sys_call_table[__NR_rk_cpu_reserve_create]		= &sys_rk_cpu_reserve_create;
	sys_call_table[__NR_rt_wait_for_next_period]		= &sys_rt_wait_for_next_period;
	sys_call_table[__NR_rk_setschedulingpolicy]		= &sys_rk_setschedulingpolicy;
	sys_call_table[__NR_rk_resource_set_attach_process]	= &sys_rk_resource_set_attach_process;
	sys_call_table[__NR_rk_get_start_of_current_period]	= &sys_rk_get_start_of_current_period;
	sys_call_table[__NR_rk_get_current_time]		= &sys_rk_get_current_time;
	sys_call_table[__NR_rk_is_scheduling]			= &sys_rk_is_scheduling;
	sys_call_table[__NR_rk_resource_set_detach_process]	= &sys_rk_resource_set_detach_process;
	sys_call_table[__NR_rk_pip_mutex]			= &sys_rk_pip_mutex;
	sys_call_table[__NR_rk_pcp_mutex]			= &sys_rk_pcp_mutex;
	sys_call_table[__NR_rk_hlp_mutex]			= &sys_rk_hlp_mutex;
	sys_call_table[__NR_rk_mpcp_mutex]			= &sys_rk_mpcp_mutex;
	sys_call_table[__NR_rk_vmpcp_mutex]			= &sys_rk_vmpcp_mutex;
	sys_call_table[__NR_rk_vmpcp_intervm_mutex]		= &sys_rk_vmpcp_intervm_mutex;
	sys_call_table[__NR_rk_getcpursv_prev_used_ticks]	= &sys_rk_getcpursv_prev_used_ticks;
	sys_call_table[__NR_rk_getcpursv_min_utilization]	= &sys_rk_getcpursv_min_utilization;
	sys_call_table[__NR_rk_getcpursv_max_utilization]	= &sys_rk_getcpursv_max_utilization;
	sys_call_table[__NR_rk_getcpursv_profile]		= &sys_rk_getcpursv_profile;
#ifdef CONFIG_RK_MEM
	sys_call_table[__NR_rk_mem_reserve_create]		= &sys_rk_mem_reserve_create;
	sys_call_table[__NR_rk_mem_reserve_delete]		= &sys_rk_mem_reserve_delete;
	sys_call_table[__NR_rk_mem_reserve_eviction_lock]	= &sys_rk_mem_reserve_eviction_lock;
#endif
	sys_call_table[__NR_rk_trace]				= &sys_rk_trace;
	sys_call_table[__NR_rk_get_start_of_next_vcpu_period]	= &sys_rk_get_start_of_next_vcpu_period;
	sys_call_table[__NR_rk_vchannel]			= &sys_rk_vchannel;
	sys_call_table[__NR_rk_vint_register_pseudo_vcpu]	= &sys_rk_vint_register_pseudo_vcpu;
	
	// For testing
	sys_call_table[__NR_rk_testing]				= &sys_rk_testing;
}

void rk_system_calls_cleanup(void)
{
	sys_call_table[__NR_rk_resource_set_create]		= sys_ni_syscall;
	sys_call_table[__NR_rk_resource_set_destroy]		= sys_ni_syscall;
	sys_call_table[__NR_rk_cpu_reserve_create]		= sys_ni_syscall;
	sys_call_table[__NR_rt_wait_for_next_period]		= sys_ni_syscall;
	sys_call_table[__NR_rk_setschedulingpolicy]		= sys_ni_syscall;
	sys_call_table[__NR_rk_resource_set_attach_process]	= sys_ni_syscall; 
	sys_call_table[__NR_rk_get_start_of_current_period]	= sys_ni_syscall;
	sys_call_table[__NR_rk_get_current_time]		= sys_ni_syscall;
	sys_call_table[__NR_rk_is_scheduling]			= sys_ni_syscall;
	sys_call_table[__NR_rk_resource_set_detach_process]	= sys_ni_syscall;
	sys_call_table[__NR_rk_pip_mutex]			= sys_ni_syscall; 
	sys_call_table[__NR_rk_pcp_mutex]			= sys_ni_syscall; 
	sys_call_table[__NR_rk_hlp_mutex]			= sys_ni_syscall; 
	sys_call_table[__NR_rk_mpcp_mutex]			= sys_ni_syscall; 
	sys_call_table[__NR_rk_vmpcp_mutex]			= sys_ni_syscall; 
	sys_call_table[__NR_rk_vmpcp_intervm_mutex]		= sys_ni_syscall; 
	sys_call_table[__NR_rk_getcpursv_prev_used_ticks]	= sys_ni_syscall;
	sys_call_table[__NR_rk_getcpursv_min_utilization]	= sys_ni_syscall;
	sys_call_table[__NR_rk_getcpursv_max_utilization]	= sys_ni_syscall;
	sys_call_table[__NR_rk_getcpursv_profile]		= sys_ni_syscall;
#ifdef CONFIG_RK_MEM
	sys_call_table[__NR_rk_mem_reserve_create]		= sys_ni_syscall;
	sys_call_table[__NR_rk_mem_reserve_delete]		= sys_ni_syscall;
	sys_call_table[__NR_rk_mem_reserve_eviction_lock]	= sys_ni_syscall;
#endif
	sys_call_table[__NR_rk_trace]				= sys_ni_syscall; 
	sys_call_table[__NR_rk_get_start_of_next_vcpu_period]	= sys_ni_syscall; 
	sys_call_table[__NR_rk_vchannel]			= sys_ni_syscall; 
	sys_call_table[__NR_rk_vint_register_pseudo_vcpu]	= sys_ni_syscall;
	
	// For testing
	sys_call_table[__NR_rk_testing]				= sys_call_table[0];
}



int init_module(void)
{
	int cpunum;

	sema_init(&rk_sem, 1);

	/* Assumption:
	 * 	We assume that the cpus are labeled from 0 ... (num_cpus - 1)
	 *	This needs to be modified for systems with different cpu numbering
	 */
	num_cpus = 0;
 	for_each_online_cpu(cpunum) {
    		printk("CPU %d Discovered\n", cpunum);
		num_cpus ++;
		if (num_cpus > RK_MAX_CPUS) {
			printk("Error: this system has more CPUs than RK_MAX_CPUS (%d)\n", RK_MAX_CPUS);
			return -1;
		}
	}
 
  	rk_timer_init();
	  
  	printk("RK Timers Initialized!!!\n");

	/* Initialize system calls, resource set, reserves */
  	rk_system_calls_init();
	rk_resource_set_init();
	rk_procfs_init();	
	rk_virt_init();
	rk_mutex_init();

	cpu_reserves_init();
#ifdef CONFIG_RK_MEM
	mem_reserves_init();
#endif

	/* Initialize kernel hooks */
	rk_schedule_hook = rk_schedule;
  	rk_exit_hook = rk_exit; 
  	rk_fork_hook = rk_fork;

#ifdef CONFIG_RK_MEM
	rk_alloc_pages_hook = rk_alloc_pages;
	rk_free_pages_hook = rk_free_pages;
	rk_add_page_rmap_hook = rk_add_page_rmap;
	rk_remove_page_rmap_hook = rk_remove_page_rmap;
	rk_check_enough_pages_hook = rk_check_enough_pages;
#endif

#ifdef RK_TRACE
	rk_trace_schedule_hook = rk_trace_schedule;
	rk_trace_fn_hook = rk_trace_fn;
#endif

#ifdef RK_VIRT_SUPPORT
	rk_hypercall_hook = rk_hypercall_handler;
	rk_kvm_assigned_dev_intr_hook = rk_kvm_assigned_dev_intr_handler;
	rk_kvm_assigned_dev_eoi_hook = rk_kvm_assigned_dev_eoi_handler;
	rk_kvm_vm_ioctl_assign_irq_hook = rk_kvm_vm_ioctl_assign_irq_handler;
	rk_kvm_vm_ioctl_deassign_dev_irq_hook = rk_kvm_vm_ioctl_deassign_dev_irq_handler;
#endif

#ifndef RK_GLOBAL_SCHED
	rk_post_schedule_hook = NULL;
  	printk("RK Partitioned Scheduling enabled\n");
#else
	rk_post_schedule_hook = rk_post_schedule;
  	printk("RK Global Scheduling enabled\n");
#endif

	rk_event_log_init();
	rk_pmc_init();

  	printk("RK Module loaded!\n\n");

	return 0;
}
EXPORT_SYMBOL(init_module);

void cleanup_module(void)
{
	/* Clean up system calls and timers 		*/
	rk_system_calls_cleanup();
	
	/* Clean up all other reservations 		*/
	rk_resource_set_destroy_all();

	/* Clean up the timers				*/
  	rk_timer_cleanup();

	/* Remove hooks					*/
  	rk_schedule_hook = NULL;
	rk_post_schedule_hook = NULL;
  	rk_fork_hook = NULL;
  	rk_exit_hook = NULL;

#ifdef CONFIG_RK_MEM
	rk_alloc_pages_hook = NULL;
	rk_free_pages_hook = NULL;
	rk_add_page_rmap_hook = NULL;
	rk_remove_page_rmap_hook = NULL;
	rk_check_enough_pages_hook = NULL;
#endif
	rk_trace_schedule_hook = NULL;
	rk_trace_fn_hook = NULL;
	rk_hypercall_hook = NULL;
	rk_kvm_assigned_dev_intr_hook = NULL;
	rk_kvm_assigned_dev_eoi_hook = NULL;
	rk_kvm_vm_ioctl_assign_irq_hook = NULL;
	rk_kvm_vm_ioctl_deassign_dev_irq_hook = NULL;

	// Clean up reserves
	cpu_reserves_cleanup();
#ifdef CONFIG_RK_MEM
	mem_reserves_cleanup();
#endif

	rk_mutex_cleanup();

	// Clean up procfs 
	rk_procfs_cleanup();

	rk_event_log_cleanup();
	rk_pmc_cleanup();
	rk_virt_cleanup();

  	printk("RK Module unloaded!\n");
}
EXPORT_SYMBOL(cleanup_module);

MODULE_AUTHOR("Carnegie Mellon University");
MODULE_DESCRIPTION("Linux/RK Module");
MODULE_LICENSE("GPL");
