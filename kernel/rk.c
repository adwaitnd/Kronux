/*
 * rk.c: additions to the kernel for rk support
 *
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

#ifdef CONFIG_RK

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <rk/rk_mc.h>

void (*rk_schedule_hook)(struct task_struct *, struct task_struct *) = NULL;
void (*rk_post_schedule_hook)(void) = NULL;
void (*rk_fork_hook)(struct task_struct *) = NULL;
void (*rk_exit_hook)(struct task_struct *) = NULL;

EXPORT_SYMBOL(rk_schedule_hook);
EXPORT_SYMBOL(rk_post_schedule_hook);
EXPORT_SYMBOL(rk_fork_hook);
EXPORT_SYMBOL(rk_exit_hook);

EXPORT_SYMBOL(sched_setscheduler_nocheck);
EXPORT_SYMBOL(sched_setaffinity);
EXPORT_SYMBOL(find_task_by_pid_ns);
EXPORT_SYMBOL(do_send_sig_info);
EXPORT_SYMBOL(task_curr);

#ifdef CONFIG_RK_MEM
void (*rk_add_page_rmap_hook)(struct page *page, bool is_anon) = NULL;
void (*rk_remove_page_rmap_hook)(struct page *page, mem_reserve_t mem) = NULL;
void (*rk_check_enough_pages_hook)(mem_reserve_t mem) = NULL;

EXPORT_SYMBOL(vma_merge);
EXPORT_SYMBOL(split_vma);
EXPORT_SYMBOL(get_gate_vma);
EXPORT_SYMBOL(find_vma_prev);
EXPORT_SYMBOL(lru_add_drain);

EXPORT_SYMBOL(rk_add_page_rmap_hook);
EXPORT_SYMBOL(rk_remove_page_rmap_hook);
EXPORT_SYMBOL(rk_check_enough_pages_hook);
#endif

// RK_TRACE
void (*rk_trace_schedule_hook)(struct task_struct*, struct task_struct*) = NULL;
void (*rk_trace_fn_hook)(int, int) = NULL;
EXPORT_SYMBOL(rk_trace_schedule_hook);
EXPORT_SYMBOL(rk_trace_fn_hook);

// RK_VIRT
int (*rk_hypercall_hook)(void*, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long) = NULL;
int (*rk_kvm_assigned_dev_intr_hook)(int, int, void*) = NULL;
int (*rk_kvm_assigned_dev_eoi_hook)(int, int) = NULL;
int (*rk_kvm_vm_ioctl_assign_irq_hook)(int, int, void*) = NULL;
int (*rk_kvm_vm_ioctl_deassign_dev_irq_hook)(void*) = NULL;

EXPORT_SYMBOL(rk_hypercall_hook);
EXPORT_SYMBOL(rk_kvm_assigned_dev_intr_hook);
EXPORT_SYMBOL(rk_kvm_assigned_dev_eoi_hook);
EXPORT_SYMBOL(rk_kvm_vm_ioctl_assign_irq_hook);
EXPORT_SYMBOL(rk_kvm_vm_ioctl_deassign_dev_irq_hook);

#endif

