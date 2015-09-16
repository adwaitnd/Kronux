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
 * rk_common.h 
 * - RK common data types (user/kernel)
 */ 

#ifndef RK_COMMON_H
#define RK_COMMON_H

#define RK_MAX_CPUS 8

/*
 * RK options
 *
 * - RK_ADMISSION_TEST: Enable admission control test 
 * - RK_GLOBAL_SCHED: Enable global CPU scheduling for a multi-core processor
 * - RK_UNIQUE_PRIORITY: If this option is enabled, CPU reserves will be 
 *     assigned unique priorities even though their timing parameters are
 *     the same (ex, same deadlines). This option can be used for both 
 *     partitioned and global scheduling.
 * - RK_UNIQUE_PRIORITY_ACROSS_PROCESSORS: This option is only effective when 
 *     using partitioned scheduling (RK_GLOBAL_SCHED is not set). It is useful
 *     for testing Multiprocessor Priority Ceiling Protocol (MPCP).
 *
 * - RK_TRACE: A generic performance profiling mechanism that traces 
 *     execution history of specified tasks. It can be used for both 
 *     non-RK and RK-enabled tasks.
 *     - RK_TRACE_SUM: Records only the task execution time. When it is enabled,
 *         execution history is not recorded unless RK_TRACE_SUM_HISTORY is set.
 *         This option requires RK_PROFILE_PMC to be enabled.
 *     - RK_TRACE_SUM_HISTORY: Task execution time + Execution history
 *     - RK_EVENT_LOG: RK system event logger
 *
 * - RK_PROFILE_PMC: Profile tasks with hardware performance counters
 *
 * - RK_MEM_NO_SWAP: For systems with no swap partition (i.e. ARM)
 *
 * - RK_VIRT_SUPPORT: Enable Linux/RK para-virtualization support
 *
 * i.e. RK for SRX Project (Use CPU reserve): RK_TRACE, RK_PROFILE_PMC
 *      Cache/Bank coloring experiment: RK_TRACE, RK_TRACE_SUM, RK_PROFILE_PMC
 */

#define RK_ADMISSION_TEST
//#define RK_GLOBAL_SCHED
//#define RK_UNIQUE_PRIORITY
#define RK_UNIQUE_PRIORITY_ACROSS_PROCESSORS

#define RK_TRACE 
#ifdef RK_TRACE
	#define RK_TRACE_SUM
	//#define RK_TRACE_SUM_HISTORY
	#define RK_EVENT_LOG
#endif

#define RK_PROFILE_PMC

//#define RK_MEM_NO_SWAP // For systems with no swap partition (i.e. ARM odroid)
#define RK_VIRT_SUPPORT

// Platform selection (For hardware-specific PMC, bank coloring, ...)
//#define RK_X86_SANDYBRIDGE 	// Intel Sandy Bridge or newer architectures (e.g., Intel Core i7-2600, i5-2540)
//#define RK_X86_YORKFIELD	// i.e. Intel Core 2 Quad Q9700
//#define RK_ARM_EXYNOS		// Samsung Exynos4412 processor (ARM)
//#define RK_ARM_iMX6           // Freescale iMX6 processor (ARM)


#ifndef TRUE
#define TRUE 				1	
#endif

#ifndef FALSE
#define FALSE				0
#endif

#define RK_SUCCESS 			(0)
#define RK_ERROR			(-1)

#define NANOSEC_PER_SEC			(1000000000LL)
#define MICROSEC_PER_SEC		(1000000LL)

#define RSET_NAME_LEN			20

#define MAX_RESOURCE_SETS		128

/* RK priority range (Note: Linux RT priority range: 1-99) */
#define MAX_LINUXRK_PRIORITY		90
#define BASE_LINUXRK_PRIORITY		70


/* Signal to notify the occurrence of CPU enforcement  */
#ifdef __KERNEL__
	#include <linux/signal.h>
#else
	#include <signal.h>
#endif
#define SIG_RK_ENFORCED			SIGUNUSED


/*
 * CPU reserve argument parameter
 */
enum rt_process_type {
	APERIODIC	=0x1,
	PERIODIC	=0x2,
	SPORADIC	=0x4,
};
typedef enum rt_process_type	rt_process_type_t;


/*
 * Reserve argument parameter
 */
enum rk_reserve_mode
{
	RSV_HARD	=0x1,
	RSV_FIRM	=0x2,
	RSV_SOFT	=0x4,
	RSV_CRIT	=0x8,
};
typedef enum rk_reserve_mode	rk_reserve_mode_t;


struct rk_reserve_param {
	rk_reserve_mode_t	sch_mode;	/* scheduling */
	rk_reserve_mode_t	enf_mode;	/* enforcement */
	rk_reserve_mode_t	rep_mode;	/* replenishment */
};
typedef	struct rk_reserve_param	rk_reserve_param_data_t;

enum rk_reserve_type
{
	RSV_NULL	=0x0,
	RSV_CPU		=0x1,
	RSV_NET		=0x2,
	RSV_RCV         =0x3,
	RSV_MEM,
	RSV_DEV,
	RSV_DISK,
};
typedef	enum rk_reserve_type	rk_reserve_type_t;

#ifdef  __KERNEL__
	#include <linux/time.h>
#else 
	#include <time.h>
	/* Typedefs for user programs */
	typedef void *			rk_resource_set_t;
	typedef void *			rk_reserve_t;
#endif /* __KERNEL__ */

struct cpu_reserve_attr {
	struct timespec compute_time;
	struct timespec period;
	struct timespec deadline;
	struct timespec blocking_time;
	struct timespec release_jitter;
	struct timespec start_time; // wall clock
	rk_reserve_param_data_t  reserve_mode;
	int cpunum;

	/* send SIG_RK_ENFORCED to tasks when CPU enforcement occurs */
	unsigned char notify_when_enforced;
};

typedef struct cpu_reserve_attr cpu_reserve_attr_data_t;
typedef struct cpu_reserve_attr* cpu_reserve_attr_t;

enum rk_cpursv_policy {
	CPURSV_NO_MIGRATION = 0,
	CPURSV_MIGRATION_DEFAULT,
	CPURSV_MIGRATION_FORKJOIN,
	NO_DEFAULT_CPURSV,
};
typedef enum rk_cpursv_policy	rk_cpursv_policy_t;


// RK ordered list (used for the cpursv list)
#define RK_MAX_ORDERED_LIST	16
struct rk_ordered_list {
	int n;
	int cur_idx;
	char elem[RK_MAX_ORDERED_LIST];
};


// Pseudo-VCPU registration for vINT
struct pseudo_vcpu_attr {
	int pseudo_vcpu_cpursv; 	// Pseudo-VCPU cpursv index
	int host_irq_no;		// Host irq number ( physical interrupt) 
	int guest_irq_no;		// Guest irq number (virtual interrupt)
	struct timespec intr_exec_time; // Execution time for a single virtual interrupt handling
};

typedef struct pseudo_vcpu_attr pseudo_vcpu_attr_data_t;
typedef struct pseudo_vcpu_attr* pseudo_vcpu_attr_t;


#define RK_MEM_MAX_COLOR 128
struct mem_reserve_attr {
	unsigned long long mem_size;
	rk_reserve_mode_t reserve_mode;
	unsigned long long swap_size;

	/* cache coloring info */
	unsigned char colors[RK_MEM_MAX_COLOR];
	int nr_colors;
	/* bank coloring info */
	unsigned char bank_colors[RK_MEM_MAX_COLOR];
	int nr_bank_colors;
};

typedef struct mem_reserve_attr mem_reserve_attr_data_t;
typedef struct mem_reserve_attr* mem_reserve_attr_t;


#ifdef RK_PROFILE_PMC
#include "rk_pmc.h"
#endif

/* 
 * RK CPU Profile Datatypes (kernel/user) 
 */
#define CPU_PROFILE_DATA_MAX	10000
struct rk_cpu_profile {
	struct timespec release;
	struct timespec completion;
	int utilization; 
#ifdef RK_PROFILE_PMC
	struct pmc_counter pmc;
#endif
};


/* 
 * RK Task Trace Datatypes (kernel/user) 
 */

// RK trace syscall: types
#define RK_TRACE_SYSCALL_SET		0
#define RK_TRACE_SYSCALL_GET		1
#define RK_TRACE_SYSCALL_SUM_SET	2
#define RK_TRACE_SYSCALL_SUM_GET	3
#define RK_TRACE_SYSCALL_EVENT_LOG_SET	4
#define RK_TRACE_SYSCALL_EVENT_LOG_GET	5

#define FN_START 	1
#define FN_END		0

#define RK_TRACE_TYPE_SCHED	0
#define RK_TRACE_TYPE_CS	1 // critical section

#define RK_TRACE_DATA_MAX	10000
struct rk_trace_data {
	unsigned long long time;
	unsigned char type; 
	unsigned char onoff;
	unsigned char core;
	unsigned char task_status;
#ifdef RK_PROFILE_PMC
	unsigned long long llc_count;
	unsigned long long instr_count;
#endif
};


#ifdef RK_TRACE_SUM

// Execution history record size for RK_TRACE_SUM_HISTORY
#define RK_TRACE_SUM_NR_HISTORY	10000
struct rk_trace_data_sum {
	struct pmc_counter total, start, end;
#ifdef RK_TRACE_SUM_HISTORY
	int nr_sched;
	unsigned char sched_onoff[RK_TRACE_SUM_NR_HISTORY];
	unsigned char sched_core[RK_TRACE_SUM_NR_HISTORY];
	unsigned long long sched_time[RK_TRACE_SUM_NR_HISTORY];
	int nr_l3miss;
	unsigned long long l3miss[RK_TRACE_SUM_NR_HISTORY];
#endif
};
#else
struct rk_trace_data_sum {};
#endif


/* 
 * RK System Event Log
 */
#define RK_EVENT_TYPE_TASK_START	0
#define RK_EVENT_TYPE_TASK_STOP		1
#define RK_EVENT_TYPE_TASK_ENTER_CS	2
#define RK_EVENT_TYPE_TASK_EXIT_CS	3
#define RK_EVENT_TYPE_VM_TASK_START	100
#define RK_EVENT_TYPE_VM_TASK_STOP	101
#define RK_EVENT_TYPE_VM_TASK_ENTER_CS	102
#define RK_EVENT_TYPE_VM_TASK_EXIT_CS	103

#define RK_EVENT_LOG_SIZE	10000
struct rk_event_data {
	unsigned long long time;
	short type;
	short cpuid;
	int pid;
	unsigned long arg1, arg2;
};


/*
 * RK vchannel syscall: types
 */
#define RK_VCHANNEL_SYSCALL_REGISTER_HOST	0
#define RK_VCHANNEL_SYSCALL_REGISTER_GUEST	1
#define RK_VCHANNEL_SYSCALL_SEND_CMD		2


/* 
 * RK mutex commands and flags (for syscalls)
 */
enum rk_mutex_cmds {
	RK_MUTEX_OPEN,
	RK_MUTEX_CLOSE,
	RK_MUTEX_DESTROY,
	RK_MUTEX_LOCK,
	RK_MUTEX_TRYLOCK,
	RK_MUTEX_UNLOCK,
};

enum rk_mutex_mode {
	MTX_CREATE 	= (1 << 0),
	MTX_OVERRUN	= (1 << 1),
	__MTX_MASK	= (1 << 2) - 1,
};

#endif /* RK_COMMON_H */

