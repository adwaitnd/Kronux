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
 * rt_process.c: library to support realtime tasks, has utilities like waiting for a period
 */

#include <rk/rk_mc.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <asm/uaccess.h>


extern int debug_rd;

asmlinkage int sys_rt_wait_for_next_period(void)
{
	rk_resource_set_t rset;
	unsigned long flags;
	int cpuid;

	rset = current->rk_resource_set;
	if (rset == NULL) {
		printk("rt_wait_for_next_period: task %d has no resource sets.\n", current->pid);
		return RK_ERROR;
	}

	raw_spin_lock_irqsave(&rset->lock, flags);
	if (rset->nr_cpu_reserves < 0) {
		printk("rt_wait_for_next_period: task %d has no cpu reserves.\n", current->pid);	
		goto error_spin_unlock;
	}
	cpuid = raw_smp_processor_id();
	raw_spin_unlock_irqrestore(&rset->lock, flags);

	if (debug_rd == rset->rd_entry) {
		printk("wait_for_next_period: pid %d cpu %d\n", current->pid, cpuid);
	}
	sleep_on(&(current->rk_resource_set->depleted_wait));
	
	return RK_SUCCESS; 

error_spin_unlock:
	raw_spin_unlock_irqrestore(&rset->lock, flags);
	return RK_ERROR;
} 

