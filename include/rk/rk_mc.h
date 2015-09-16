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
 * rk_mc.h (For kernel mode)
 * - Data types
 * - Utility functions
 * - Systemcall prototypes
 */ 

#ifndef RK_MC_H
#define RK_MC_H


#include <linux/sched.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/hrtimer.h>
#include <linux/semaphore.h>
#include "rk_common.h"

extern struct semaphore rk_sem;
extern int cpu_reserves_kernel_scheduling_policy;
extern int cpu_reserves_current_min_priority;

extern int rk_sem_debug_pid;
extern char rk_sem_debug_fn[100];

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define rk_sem_down()		down(&rk_sem)
#define rk_sem_up()		up(&rk_sem)
//#define rk_sem_down()		do { down(&rk_sem); rk_sem_debug_pid = current->pid; sprintf(rk_sem_debug_fn, "%s", __FUNCTION__); } while (0)
//#define rk_sem_up()		do { rk_sem_debug_pid = 0; rk_sem_debug_fn[0] = 0; up(&rk_sem); } while (0)


typedef long long 		cpu_tick_data_t;
typedef long long *		cpu_tick_t;


/* Reservation state
 *	RSV_IS_NULL      : not running and haven't started yet
 *      RSV_IS_STARTED   : not running but started already
 *	RSV_IS_RUNNING   : running (current task)
 *	RSV_IS_DEPLETED  : not running and depleted
 *	RUNNING|DEPLETED : running but depleted
 */
enum rk_reserve_state {
	RSV_IS_NULL	=0x0,	
	RSV_IS_RUNNING	=0x1,
	RSV_IS_DEPLETED	=0x10,
	RSV_IS_STARTED  =0x100,
};
typedef	enum rk_reserve_state	rk_reserve_state_t;

/* 
 * RK Task schedulability 
 * - for struct task_struct::rk_cannot_schedule) 
 */
enum rk_task_schedule {
	RK_TASK_SCHEDULABLE    = 0,
	RK_TASK_UNSCHEDULABLE  = (1 << 0), 
	RK_TASK_BEING_ATTACHED = (1 << 1),
	RK_TASK_TO_BE_DETACHED = (1 << 2),
	RK_TASK_WAIT_FOR_TURN  = (1 << 3), // Global scheduling

	RK_TASK_RUNNING        = (1 << 4), 
};
#define RK_TASK_SCHED_MASK (RK_TASK_RUNNING - 1)

struct rk_vtimer {
	int cpunum;
	struct hrtimer t;
};

enum rk_timer_type{
	TMR_NULL = 0,
	TMR_REPLENISH_RSV,
	TMR_PERIOD_START,
	TMR_NEXT_PERIOD,
	TMR_POSIX,
	TMR_JIFFY,
	TMR_ENFORCE,
	TMR_BUFF_PERIOD,
};
typedef enum rk_timer_type 	rk_timer_type_t;


typedef struct rk_reserve * 	rk_reserve_t;
typedef struct rk_resource_set* rk_resource_set_t;


struct rk_timer{
	struct list_head	tmr_tmr;
	cpu_tick_data_t		tmr_expire;
	cpu_tick_data_t		prev_time;
	rk_timer_type_t		tmr_type;
	rk_reserve_t		reserve_link;
	void (*tmr_handler)(struct rk_timer *);
	int			overflow;
};
typedef struct rk_timer * 	rk_timer_t;


struct rk_reserve_ops {
	int (*read_proc)(rk_reserve_t, char*);
};

struct rk_reserve {
	rk_timer_t			reserve_replenish_timer;
	rk_timer_t			reserve_enforce_timer;
	volatile rk_resource_set_t	parent_resource_set;
	rk_reserve_type_t		reservation_type;
	rk_reserve_state_t 		reservation_state;	
	rk_reserve_param_data_t 	reservation_parameters; 
	void *				reserve;
	struct rk_reserve_ops   	*operations;		
	struct proc_dir_entry   	*rsv_proc_entry;
	int				reserve_index;
};

/* Remember the scheduling mode problems with resource sets and multiple reserves */
struct rk_resource_set{
	struct list_head	rset_list;
	char			name[RSET_NAME_LEN];
	int			rd_entry;
	struct proc_dir_entry   *rs_proc_dir;
	bool			rk_inherit;
	bool			rk_auto_cleanup;

	// Lock for task list and CPU reserves
	raw_spinlock_t 		lock;

	// Attached tasks
	int 			nr_tasks;
	struct list_head 	task_list;

	// CPU reserve 
	wait_queue_head_t       depleted_wait;
	rk_cpursv_policy_t	cpursv_policy;

	// Mem reserve 
	rk_reserve_t		mem_reserve;

	// CPU reserve set
	unsigned int 		nr_cpu_reserves;
	rk_reserve_t		cpu_reserves[RK_MAX_ORDERED_LIST];
};

/* 
 * RK deferred work for CPU resource enforcement/replenishment 
 */
#define RK_WORKQUEUE_MAX 512
enum {
	RK_WORK_WAKEUP 	         = (1 << 0),
	RK_WORK_ENFORCE          = (1 << 1),
	RK_WORK_REPLENISH	 = (1 << 2),
	RK_WORK_REPLENISH_WAKEUP = (1 << 2) | RK_WORK_WAKEUP,
	RK_WORK_MUTEX		 = (1 << 3),
};
struct rk_work_info {
	int type;
	void *args[3];
};
struct rk_workqueue {
	raw_spinlock_t lock;
	int cur_pos;
	int cur_size;
	struct rk_work_info work[RK_WORKQUEUE_MAX];
};

/* keep track of cpu capacity usage (i.e. 9999 = 99.99%) */
typedef	long		cpu_capacity_t;

/* cpu profile */
struct rk_cpu_profile_set { 
	raw_spinlock_t lock;
	int max_size; 
	int cur_pos;
	int cur_size;
	// double buffer
	struct rk_cpu_profile *cur_buf;
	struct rk_cpu_profile *buf[2];
	cpu_tick_data_t used_ticks; 
#ifdef RK_PROFILE_PMC
	struct pmc_counter start_pmc;
#endif
};

struct rk_trace_data_set {
	raw_spinlock_t lock;
	int max_size;
	int cur_size;
	// double buffer
	struct rk_trace_data *cur_buf;
	struct rk_trace_data *buf[2];
};

struct rk_event_data_set {
	raw_spinlock_t lock;
	int max_size;
	int cur_size;
	// double buffer
	struct rk_event_data *cur_buf;
	struct rk_event_data *buf[2];
};


struct cpu_reserve {
	/* Information about the current invocation of a task					*/
	struct task_struct *current_task; 		/* The task currently using this rsv	*/
	cpu_tick_data_t   used_ticks_in_cur_period;	/* CPU ticks used in current period     */
	cpu_tick_data_t	  avail_ticks_in_cur_period;	/* CPU ticks available in this period   */
	cpu_tick_data_t	  release_time_of_cur_period;	/* Release time of the current period   */
	cpu_tick_data_t   start_time_of_cur_exec;	/* Start time of current execution      */

	struct timespec	  absolute_deadline;
  
	/* Static data that is constant over all invocations					*/
	struct list_head  cpu_link;		        /* link all cpu reserves 		*/
	cpu_reserve_attr_data_t cpu_res_attr;           /* reserve attributes 			*/
	cpu_tick_data_t	  cpu_time_ticks;		/* ticks for cpu_time 			*/
	unsigned int      cpu_priority_index;           /* reserve priority index 		*/
	cpu_tick_data_t	  cpu_period_ticks;		/* ticks for cpu_period 		*/
	cpu_capacity_t	  cpu_capacity;			/* requested capacity 			*/
	rk_reserve_t      rsv;                          /* abstract reserve   			*/
	cpu_tick_data_t   cpu_deadline_ticks;          	/* ticks for cpu_deadline 		*/
	cpu_tick_data_t   cpu_eligible_deadline_ticks; 	/* the deadline of the eligible reserve */
	unsigned long     scheduling_policy;            /* scheduling policy 			*/
	unsigned int 	  vcpu_priority_index;          /* vcpu priority (used in a guest vm)	*/

	/* Execution statistics */
	cpu_tick_data_t	  used_ticks_in_prev_period;
	cpu_capacity_t	  cpu_max_utilization;
	cpu_capacity_t	  cpu_min_utilization;
	struct rk_cpu_profile_set cpu_profile;

	/* Global scheduling: The task waking up. This field is used to avoid starvation */
	struct task_struct *waking_task;

	/* Enforcement control: enable/disable enforcement for each cpu reserve */
	bool do_enforcement;
	
	/* Virtual comm channel used by a host machine */
	void *vchannel_host;

	/* Global critical section levels */
	int gcs_count;

	/* vINT pseudo-VCPU support */ 
	cpu_tick_data_t pseudo_vcpu_oneshot_exec_time;
};

typedef struct cpu_reserve *cpu_reserve_t;


/*
 * Memory reservation 
 */
struct mem_reserve {
	struct list_head mem_link; /* link all mem reserves */
	rk_reserve_t rsv; /* abstract reserve */

	/* memory reservation pool */
	struct mem_reserve_page *reserved_pages;
	struct list_head **mem_free_list;
	struct list_head mem_active_list;
	struct list_head mem_inactive_list;
	//struct list_head mem_used_list[4];
	int mem_total_size, mem_aux_size; /* # of pages */
	int mem_used_size; /* active + inactive */
	int mem_active_size;
	int mem_inactive_size;
	int mem_peak_size;
	int mem_free_size;
	int **mem_free_size_detail;
	
	// List for shared page conservation
	struct list_head mem_conserved_list;
	int mem_conserved_size;

	//int mem_used_size_each[4];
	raw_spinlock_t mem_list_lock;

	/* reserve attributes */
	mem_reserve_attr_data_t mem_res_attr;
	mem_reserve_attr_data_t aux_res_attr;

	/* information about memory pool management */
	int page_fault_counter;
	int nr_attaching_tasks;

	/* cache and bank color assignment */
	int next_color_from_pagebins;
	int next_bank_color_from_pagebins;
	int next_color_to_tasks;
	int next_bank_color_to_tasks;

	/* auxiliary reserve for vColoring */
};

typedef struct mem_reserve *mem_reserve_t;

struct mem_reserve_page {
	struct page *page;
	struct list_head list; // list of reserved pages in a reservation 
	struct list_head shared; // list of owners of a shared page
	mem_reserve_t mem;
	unsigned executable	: 1;
	unsigned active_used    : 1;
	//unsigned category   	: 3;
	unsigned access_count	: 16;
};


/*
 * RK System calls
 */
asmlinkage int sys_rk_resource_set_create(char *name, int inherit_flag, int cleanup_flag, int cpursv_policy);
asmlinkage int sys_rk_resource_set_destroy(int rd);
asmlinkage int sys_rk_resource_set_attach_process(int rd, pid_t pid, struct rk_ordered_list*);
asmlinkage int sys_rk_resource_set_detach_process(int rd, pid_t pid);

asmlinkage int sys_rk_cpu_reserve_create(int rd, cpu_reserve_attr_t cpu_attr);
asmlinkage void sys_rk_setschedulingpolicy(int policy);
asmlinkage int sys_rk_is_scheduling(void);
asmlinkage int sys_rt_wait_for_next_period(void);
asmlinkage void sys_rk_get_start_of_current_period(unsigned long long *tm);
asmlinkage void sys_rk_get_current_time(unsigned long long *tm);
asmlinkage void sys_rk_getcpursv_prev_used_ticks(int rd, unsigned long long *ret);
asmlinkage void sys_rk_getcpursv_min_utilization(int rd, unsigned long *ret);
asmlinkage void sys_rk_getcpursv_max_utilization(int rd, unsigned long *ret);
asmlinkage int  sys_rk_getcpursv_profile(int mode, int id, void *data1, void *data2);

asmlinkage int sys_rk_mem_reserve_create(int rd, mem_reserve_attr_t usr_mem_attr, mem_reserve_attr_t usr_aux_attr);
asmlinkage int sys_rk_mem_reserve_delete(int rd);
asmlinkage int sys_rk_mem_reserve_eviction_lock(pid_t pid, unsigned long vaddr, size_t size, bool lock);

asmlinkage int sys_rk_trace(int type, int nr, void *data);

asmlinkage int sys_rk_get_start_of_next_vcpu_period(cpu_tick_t);
asmlinkage int sys_rk_vchannel(int type, int nr, void *data);
asmlinkage int sys_rk_vint_register_pseudo_vcpu(int rd, pseudo_vcpu_attr_t usr_pseudo_vcpu_attr);

asmlinkage int sys_rk_testing(int index, int nr, void *data);


/*
 * RK Functions and data declarations: Hooks
 */
extern void (*rk_schedule_hook)(struct task_struct *, struct task_struct *);
extern void (*rk_post_schedule_hook)(void);
extern void (*rk_fork_hook)(struct task_struct *);
extern void (*rk_exit_hook)(struct task_struct *);
#ifdef CONFIG_RK_MEM
extern void (*rk_add_page_rmap_hook)(struct page *page, bool is_anon);
extern void (*rk_remove_page_rmap_hook)(struct page *page, mem_reserve_t mem);
extern void (*rk_check_enough_pages_hook)(mem_reserve_t mem);
extern struct page* (*rk_alloc_pages_hook)(gfp_t gfp_mask, unsigned int order, bool* ret);
extern int (*rk_free_pages_hook)(struct page *page, unsigned int order);
#endif
extern void (*rk_trace_schedule_hook)(struct task_struct*, struct task_struct*);
extern void (*rk_trace_fn_hook)(int, int);

extern int  (*rk_hypercall_hook)(void*, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);
extern int (*rk_kvm_assigned_dev_intr_hook)(int, int, void*);
extern int (*rk_kvm_assigned_dev_eoi_hook)(int, int);
extern int (*rk_kvm_vm_ioctl_assign_irq_hook)(int, int, void*);
extern int (*rk_kvm_vm_ioctl_deassign_dev_irq_hook)(void*);


/*
 * RK Functions and data declarations: Resource sets 
 */
extern int num_cpus;
extern rk_resource_set_t *resource_set_descriptor;

rk_reserve_t rk_reserve_create(rk_resource_set_t rs, rk_reserve_type_t type);
void rk_resource_set_init(void);
void rk_delete_reserve(rk_reserve_t rsv, int index);
void rk_destroy_reserves(rk_resource_set_t rset);
int rk_default_resource_set_create(int cpunum);
int rk_default_resource_set_destroy(int rd);
int rk_default_cpu_reserve_create(int rd);
int rk_default_cpu_reserve_destroy(int rd);
void rk_resource_set_destroy_all(void);


/*
 * RK Functions and data declarations: Timers
 */
extern rk_resource_set_t *resource_set_descriptor;
extern DEFINE_PER_CPU(int, rk_has_timers);

void rk_timer_init(void);
void rk_timer_cleanup(void);
void rk_replenish_timer_create(rk_reserve_t rsv, cpu_tick_data_t ticks);
void reset_replenish_timer(rk_reserve_t rsv, cpu_tick_data_t ticks);
void rk_timer_remove(rk_timer_t, int cpunum);
void rk_timer_destroy(rk_timer_t, int cpunum);
void rk_enforce_timer_stop(rk_reserve_t rsv, int cpunum);
void rk_enforce_timer_start(rk_reserve_t rsv, cpu_tick_t next_available_ticks, cpu_tick_t start, int cpunum);


/*
 * RK Functions and data declarations: Task management (sched, fork, ...)
 */
void rk_schedule(struct task_struct *prev, struct task_struct *next);
void rk_post_schedule(void);
void rk_fork(struct task_struct *tsk);
void rk_exit(struct task_struct *tsk);
void rk_task_cleanup(struct task_struct *tsk);


/*
 * RK Functions and data declarations: CPU reserves
 */
extern int cpu_reserves_scheduling_policy;
extern int cpu_reserves_kernel_scheduling_policy;
extern DEFINE_PER_CPU(struct rk_workqueue*, cpu_workqueue);
extern DEFINE_PER_CPU(rk_reserve_t, rk_current_cpu_reserve);
extern DEFINE_PER_CPU(struct task_struct*, rk_post_schedule_wakeup);

void cpu_reserves_init(void);
void cpu_reserves_cleanup(void);
void cpu_reserve_start_account(rk_reserve_t, struct task_struct*, cpu_tick_t);
void cpu_reserve_stop_account(rk_reserve_t, cpu_tick_t);
void rk_cpu_reserve_delete(cpu_reserve_t cpu);
void cpu_reserve_enforce(rk_reserve_t rsv);
void cpu_reserve_replenish(rk_reserve_t rsv, cpu_tick_t start_ticks, cpu_tick_t period);
void cpursv_profile_update(cpu_tick_t start, cpu_tick_t now, cpu_tick_t used_ticks, struct rk_cpu_profile_set *profile);
void rk_prepare_task_for_cpursv(int type, struct task_struct *task, int cpunum, int prio);
struct task_struct* rk_get_next_task_in_cpursv(rk_resource_set_t rset, rk_reserve_t rsv, struct task_struct *start, struct task_struct *except);


/*
 * RK Functions and data declarations: Memory reserves
 */
#ifdef CONFIG_RK_MEM
void mem_reserves_init(void);
void mem_reserves_cleanup(void);
int rk_mempool_read_proc(char *buf);
void rk_mem_reserve_delete(mem_reserve_t mem);
struct page* rk_alloc_pages(gfp_t gfp_mask, unsigned int order, bool* ret);
int rk_free_pages(struct page *page, unsigned int order);
void rk_add_page_rmap(struct page *page, bool is_anon);
void rk_remove_page_rmap(struct page *page, mem_reserve_t mem);
void rk_check_enough_pages(mem_reserve_t mem);
void mem_reserve_attach_process(mem_reserve_t mem, struct task_struct *p);
void mem_reserve_detach_process(mem_reserve_t mem, struct task_struct *p);
int sys_rk_mem_reserve_show_color_info(int color_idx);
int mem_reserve_get_nr_colors(void);
int mem_reserve_get_color_idx(struct page* page);
int mem_reserve_get_nr_bank_colors(void);
int mem_reserve_get_bank_color_idx(struct page* page);
#endif

struct kvm_vcpu;
#if defined(CONFIG_RK_MEM) && defined(RK_VIRT_SUPPORT)
int rk_mem_reserve_assign_guest_task_colors(struct kvm_vcpu *vcpu, unsigned long key, unsigned long colorbits);
int rk_mem_reserve_traverse_guest_page_table(struct kvm_vcpu *vcpu);
#else
static inline int rk_mem_reserve_assign_guest_task_colors(struct kvm_vcpu *vcpu, unsigned long key, unsigned long colorbits) { return RK_ERROR; }
static inline int rk_mem_reserve_traverse_guest_page_table(struct kvm_vcpu *vcpu) { return RK_ERROR; }
#endif


/*
 * RK Functions and data declarations: RK mutex
 */
void rk_mutex_init(void);
void rk_mutex_cleanup(void);
int rk_mutex_read_proc(int mid, char *buf);
void rk_mutex_task_cleanup(struct task_struct *task);
int sys_rk_pip_mutex(int cmd, int arg1, int arg2);
int sys_rk_pcp_mutex(int cmd, int arg1, int arg2);
int sys_rk_hlp_mutex(int cmd, int arg1, int arg2);
int sys_rk_mpcp_mutex(int cmd, int arg1, int arg2);
int sys_rk_vmpcp_mutex(int cmd, int arg1, int arg2);
int sys_rk_vmpcp_intervm_mutex(int cmd, int arg1, int arg2);


/*
 * RK Functions and data declarations: procfs 
 */
void rk_procfs_init(void);
void rk_procfs_cleanup(void);
void rk_procfs_rset_create(rk_resource_set_t rset);
void rk_procfs_rset_destroy(rk_resource_set_t rset);
void rk_procfs_rset_attach_process(rk_resource_set_t rset, int pid);
void rk_procfs_rset_detach_process(int pid);
void rk_procfs_reserve_create(rk_reserve_t rsv, int index);
void rk_procfs_reserve_destroy(rk_reserve_t rsv, int index);
void rk_procfs_mutex_create(int mid);
void rk_procfs_mutex_destroy(int mid);


/*
 * RK Functions and data declarations: trace and event log mechanisms
 */
void rk_pmc_init(void);
void rk_pmc_cleanup(void);
int sys_rk_mem_reserve_show_task_vminfo(int pid);
int sys_rk_mem_reserve_show_reserved_pages(int rd);

#ifdef RK_TRACE

void rk_trace_schedule(struct task_struct *prev, struct task_struct *next);
void rk_trace_fn(int, int);
#define DO_RK_TRACE_FN(A, B)				\
	if (rk_trace_fn_hook && current->rk_trace)	\
		rk_trace_fn_hook(A, B)
#ifdef RK_TRACE_SUM
	void save_rk_trace_sum(struct task_struct *p);
#endif

#else // RK_TRACE

static inline void rk_trace_schedule(struct task_struct *prev, struct task_struct *next) {}
static inline void rk_trace_fn(int type, int start_end) {}
#define DO_RK_TRACE_FN(A, B)

#endif // RK_TRACE


#ifdef RK_EVENT_LOG
void rk_event_log_init(void);
void rk_event_log_cleanup(void);
void __rk_event_log_save(int type, int cpuid, int pid, unsigned long arg1, unsigned long arg2, unsigned long long time);
void rk_event_log_save(int type, int cpuid, int pid, unsigned long arg1, unsigned long arg2);
int sys_rk_event_log_set(int pid);
int sys_rk_event_log_get(void *usr_buffer);
#else
static inline void rk_event_log_init(void) {}
static inline void rk_event_log_cleanup(void) {}
static inline void __rk_event_log_save(int type, int cpuid, int pid, unsigned long arg1, unsigned long arg2, unsigned long long time) {}
static inline void rk_event_log_save(int type, int cpuid, int pid, unsigned long arg1, unsigned long arg2) {}
static inline int sys_rk_event_log_set(int pid) { return RK_ERROR; }
static inline int sys_rk_event_log_get(void *usr_buffer) { return RK_ERROR; }
#endif // RK_EVENT_LOG


/*
 * RK Inline helper functions
 */
static inline void rk_list_del(struct list_head *entry)
{
	list_del(entry);
}

static inline void rk_list_add(struct list_head *new, struct list_head *head)
{
	list_add(new, head);
}

static inline void rk_rdtsc(cpu_tick_t data_p)
{
	struct timespec val;
	getnstimeofday(&val);
	(*data_p) = ((cpu_tick_data_t)val.tv_sec) * ((cpu_tick_data_t)NANOSEC_PER_SEC);
	(*data_p) += ((cpu_tick_data_t)val.tv_nsec);	
}

static inline void rk_rdtsc_timespec(struct timespec *val)
{
	getnstimeofday(val);
}

/*
 * Conversions between tick and nanosec
 * rk_cpu_ticks_per_second:NANOSEC = ticks:nanosec
 */
static inline void nanosec2tick(cpu_tick_t ns, cpu_tick_t tick)
{
	(*tick) = (*ns);
}

static inline void tick2nanosec(cpu_tick_t tick, cpu_tick_t ns)
{
	(*ns) = (*tick);
}

// This function can only suspend tasks with cpursv
static inline void rk_suspend_task_now(struct task_struct *task)
{
	int cpunum;
        // Suspend the task before attaching it to rset
        // - It will be woken up at the beginning of next period by replenish timer
	// Note: We cannot change other tasks' state anytime since it may break the kernel assumptions.
	//       So we use an indirect way to suspend tasks by using rk_cannot_schedule.
        // __set_task_state(task, TASK_UNINTERRUPTIBLE);
        task->rk_cannot_schedule |= RK_TASK_UNSCHEDULABLE;

	if (task_curr(task)) {
		set_tsk_need_resched(task);
		cpunum = task_cpu(task);
		if (cpunum != smp_processor_id()) {
			smp_mb(); // should be visible for other cpus
			smp_send_reschedule(cpunum);
			//printk("smp send %d cpunum %d rk_state %d\n", task->pid, cpunum, task->rk_cannot_schedule);
		}
	}
}

// RK worker threads
DECLARE_PER_CPU(struct task_struct*, rk_worker);

// Push to the destination cpu's work queue. 
// Pushed work will be handled by a rk-worker thread.
static inline int rk_push_to_workqueue(int dest_cpu, int type, void* arg1, void* arg2, void* arg3) 
{
	unsigned long flags;
	int ret = RK_ERROR;
	struct rk_workqueue *queue;
	
	raw_spin_lock_irqsave(&per_cpu(cpu_workqueue, dest_cpu)->lock, flags);

	queue = per_cpu(cpu_workqueue, dest_cpu);
	if (queue->cur_size >= RK_WORKQUEUE_MAX) {
#ifdef CONFIG_X86
		printk("rk_push_to_workqueue: queue is full (cpunum %d, curr %d %s)\n", dest_cpu, per_cpu(current_task, dest_cpu)->pid, per_cpu(current_task, dest_cpu)->comm);
#else
		printk("rk_push_to_workqueue: queue is full (cpunum %d)\n", dest_cpu); // Note: current_task is x86-specific 
#endif
		goto error;
	}

	queue->work[queue->cur_pos].type    = type;
	queue->work[queue->cur_pos].args[0] = arg1;
	queue->work[queue->cur_pos].args[1] = arg2;
	queue->work[queue->cur_pos].args[2] = arg3;

	queue->cur_size++;
	queue->cur_pos = (queue->cur_pos + 1) % RK_WORKQUEUE_MAX;
	ret = RK_SUCCESS;

error:
	raw_spin_unlock_irqrestore(&per_cpu(cpu_workqueue, dest_cpu)->lock, flags);
	return ret;
}

// Called by a rk-worker thread.
static inline int rk_pop_from_workqueue(int dest_cpu, struct rk_work_info *output)
{
	unsigned long flags;
	int index, ret = RK_ERROR;
	struct rk_workqueue *queue;
	
	raw_spin_lock_irqsave(&per_cpu(cpu_workqueue, dest_cpu)->lock, flags);

	queue = per_cpu(cpu_workqueue, dest_cpu);
	if (queue->cur_size <= 0) goto error;

	index = ((queue->cur_pos + RK_WORKQUEUE_MAX) - queue->cur_size) % RK_WORKQUEUE_MAX;
	*output = queue->work[index];

	queue->cur_size--;
	ret = RK_SUCCESS;

error:
	raw_spin_unlock_irqrestore(&per_cpu(cpu_workqueue, dest_cpu)->lock, flags);
	return ret;
}

// Returns RK_SUCCESS if a task has an activated cpu reserve
// Should be called with rset->lock held
static inline int rk_check_task_cpursv(struct task_struct *task) 
{
	if (task == NULL) return RK_ERROR;
	if (task->rk_resource_set == NULL) return RK_ERROR;
	if (task->rk_cpursv_list == NULL) return RK_ERROR;
	if (task->rk_cpursv_list->cur_idx < 0) return RK_ERROR;
	return RK_SUCCESS;	
}

// Should be called with rset->lock held
static inline rk_reserve_t rk_get_task_current_cpursv(struct task_struct *task) 
{
	int cpursv_index;
	if (rk_check_task_cpursv(task) == RK_ERROR) return NULL;

	cpursv_index = task->rk_cpursv_list->elem[task->rk_cpursv_list->cur_idx];
	return ((rk_resource_set_t)task->rk_resource_set)->cpu_reserves[cpursv_index];
}

// Returns the index of rset->cpu_reserves[] being used by the task
// Should be called with rset->lock held
static inline int rk_get_task_current_cpursv_index(struct task_struct *task) 
{
	int cpursv_index;
	if (rk_check_task_cpursv(task) == RK_ERROR) return RK_ERROR;

	cpursv_index = task->rk_cpursv_list->elem[task->rk_cpursv_list->cur_idx];
	return cpursv_index;
}

// Should be called with rset->lock held
static inline int rk_get_task_current_cpursv_cpunum(struct task_struct *task) 
{
	int cpursv_index;
	cpu_reserve_t cpursv;
	if (rk_check_task_cpursv(task) == RK_ERROR) return -1;

	cpursv_index = task->rk_cpursv_list->elem[task->rk_cpursv_list->cur_idx];
	cpursv = ((rk_resource_set_t)task->rk_resource_set)->cpu_reserves[cpursv_index]->reserve;
	return cpursv->cpu_res_attr.cpunum;
}

// Should be called with rset->lock held
// - task is assumed to be assigned a cpu reserve
static inline rk_reserve_t __rk_get_task_next_cpursv(struct task_struct *task) 
{
	rk_resource_set_t rset;
	int prev_index, next_index;
	rset = task->rk_resource_set;
	prev_index = next_index = task->rk_cpursv_list->cur_idx;
	while (1) {
		next_index = (next_index + 1) % (task->rk_cpursv_list->n);

		if (next_index == prev_index) break;
		if (rset->cpu_reserves[(int)task->rk_cpursv_list->elem[next_index]]) break;
	}
	return rset->cpu_reserves[(int)task->rk_cpursv_list->elem[next_index]];
}

// Should be called with rset->lock held
// - task is assumed to be assigned a cpu reserve
static inline rk_reserve_t __rk_set_task_next_cpursv(struct task_struct *task) 
{
	rk_resource_set_t rset;
	int prev_index, next_index;
	rset = task->rk_resource_set;
	prev_index = next_index = task->rk_cpursv_list->cur_idx;
	while (1) {
		next_index = (next_index + 1) % (task->rk_cpursv_list->n);

		if (next_index == prev_index) break;
		if (rset->cpu_reserves[(int)task->rk_cpursv_list->elem[next_index]]) break;
	}
	task->rk_cpursv_list->cur_idx = next_index;
	return rset->cpu_reserves[(int)task->rk_cpursv_list->elem[next_index]];
}

// Should be called with rset->lock held
// - task is assumed to be assigned a cpu reserve
static inline rk_reserve_t __rk_get_task_default_cpursv(struct task_struct *task)
{
	return ((rk_resource_set_t)task->rk_resource_set)->cpu_reserves[(int)task->rk_cpursv_list->elem[0]];
}

// Should be called with rset->lock held
// - task is assumed to be assigned a cpu reserve
static inline rk_reserve_t __rk_set_task_default_cpursv(struct task_struct *task)
{
	task->rk_cpursv_list->cur_idx = 0;
	return ((rk_resource_set_t)task->rk_resource_set)->cpu_reserves[(int)task->rk_cpursv_list->elem[0]];
}

// Should be called with rset->lock held
// - task is assumed to be assigned a cpu reserve
static inline int __rk_get_task_default_cpursv_cpunum(struct task_struct *task)
{
	cpu_reserve_t cpursv;
	cpursv = ((rk_resource_set_t)task->rk_resource_set)->cpu_reserves[(int)task->rk_cpursv_list->elem[0]]->reserve;
	return cpursv->cpu_res_attr.cpunum;
}

#endif /* RK_MC_H */

