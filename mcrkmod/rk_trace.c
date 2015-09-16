/*
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
 * rk_trace.c: RK tracing and debugging functions
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <rk/rk_mc.h>
#include <rk/rk_mem.h>
#include <rk/rk_virt.h>


/**************************************************************
 *
 * RK trace mechanisms
 *
 **************************************************************/
static inline void rk_trace_save_data(struct rk_trace_data_set *trace, int type, int onoff, int task_status, void *pmc)
{
	struct rk_trace_data *buf;
	unsigned long flags;
	int cpuid;

	raw_spin_lock_irqsave(&trace->lock, flags);
	cpuid = raw_smp_processor_id();
	buf = trace->cur_buf;
	if (trace->cur_size < trace->max_size) {
		int idx = trace->cur_size++;
		rk_rdtsc(&buf[idx].time);
		buf[idx].type = type; // 0: SCHED, 1:,...: User defined
		buf[idx].onoff = onoff;
		buf[idx].core = cpuid;
		buf[idx].task_status = task_status;
#ifdef RK_PROFILE_PMC
		if (pmc) {
			buf[idx].llc_count = ((struct pmc_counter*)pmc)->l3_miss;
			buf[idx].instr_count = ((struct pmc_counter*)pmc)->inst_retired_any;
		}
#endif
	}
	raw_spin_unlock_irqrestore(&trace->lock, flags);
}

static inline void __rk_trace_schedule(struct task_struct *prev, struct task_struct *next)
{
#ifdef RK_PROFILE_PMC
	struct pmc_counter pmc;
        int pmc_avail = false;
	
	if (prev && prev->rk_trace) {
                get_pmc_info(&pmc);
                pmc_avail = true;
		rk_trace_save_data(prev->rk_trace, RK_TRACE_TYPE_SCHED, false, prev->state, &pmc);
	}
	if (next && next->rk_trace) {
                if (pmc_avail == false) 
                        get_pmc_info(&pmc);
		rk_trace_save_data(next->rk_trace, RK_TRACE_TYPE_SCHED, true, next->state, &pmc);
	}
#else
	if (prev && prev->rk_trace) {
		rk_trace_save_data(prev->rk_trace, RK_TRACE_TYPE_SCHED, false, prev->state, NULL);
	}
	if (next && next->rk_trace) {
		rk_trace_save_data(next->rk_trace, RK_TRACE_TYPE_SCHED, true, next->state, NULL);
	}
#endif
}

#ifdef RK_TRACE
void rk_trace_fn(int type, int start_end)
{
#ifndef RK_TRACE_SUM
	if (current->rk_trace) {
		void *pmc_ptr = NULL;
#ifdef RK_PROFILE_PMC
		struct pmc_counter pmc;
		get_pmc_info(&pmc);
		pmc_ptr = &pmc;
#endif
		rk_trace_save_data(current->rk_trace, type, start_end, current->state, pmc_ptr);
	}
#endif
}
#endif // RK_TRACE

int sys_rk_trace_set(int pid, int size)
{
	struct task_struct *p;
	struct rk_trace_data_set *trace;

	printk("[Start RK Trace : pid %d, buffer size: %d]\n", pid, size);
	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if(p == NULL) {
		printk("sys_rk_trace_set: cannot find task with pid %d\n", pid);
		return RK_ERROR;
	}

	rk_sem_down();
	if (p->rk_trace) {
		printk("sys_rk_trace_set: pid %d already has started tracing\n", pid);
		goto error_sem_unlock;
	}
	if (size <= 1 || size > RK_TRACE_DATA_MAX) {
		printk("sys_rk_trace_set: invalid buffer size (1 < size < %d)\n", RK_TRACE_DATA_MAX);
		goto error_sem_unlock;
	}
	trace = vmalloc(sizeof(struct rk_trace_data_set));
	if (trace == NULL) {
		printk("sys_rk_trace_set: vmalloc error (rk_trace_data_set)\n");
		goto error_sem_unlock;
	}
	trace->buf[0] = vmalloc(sizeof(struct rk_trace_data) * size);
	if (trace->buf[0] == NULL) {
		printk("sys_rk_trace_set: vmalloc error (trace->buf[0])\n");
		vfree(trace);
		goto error_sem_unlock;
	}
	trace->buf[1] = vmalloc(sizeof(struct rk_trace_data) * size);
	if (trace->buf[1] == NULL) {
		printk("sys_rk_trace_set: vmalloc error (trace->buf[1])\n");
		vfree(trace->buf[0]);
		vfree(trace);
		goto error_sem_unlock;
	}
	memset(trace->buf[0], 0, sizeof(struct rk_trace_data) * size);
	memset(trace->buf[1], 0, sizeof(struct rk_trace_data) * size);
	trace->cur_buf = trace->buf[0];

	raw_spin_lock_init(&trace->lock);
	trace->cur_size = 0;
	trace->max_size = size;

	p->rk_trace = trace;
	rk_sem_up();

	return RK_SUCCESS;

error_sem_unlock:
	rk_sem_up();
	return RK_ERROR;
}

int sys_rk_trace_get(int pid, void *usr_buffer)
{
	struct task_struct *p;
	struct rk_trace_data_set *trace;
	struct rk_trace_data *buf;
	unsigned long flags;
	int cur_size;

	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if (p == NULL) {
		printk("sys_rk_trace_get: cannot find task with pid %d\n", pid);
		return RK_ERROR;
	}
	if (p->rk_trace == NULL) {
		printk("sys_rk_trace_get: doesn't have trace data\n");
		return RK_ERROR;
	}
	trace = p->rk_trace;
	raw_spin_lock_irqsave(&trace->lock, flags); 

	cur_size = trace->cur_size;
	if (cur_size == 0) {
		// nothing to copy
		raw_spin_unlock_irqrestore(&trace->lock, flags);
		return 0;
	}
	buf = trace->cur_buf;
	if (trace->cur_buf == trace->buf[0]) trace->cur_buf = trace->buf[1];
	else trace->cur_buf = trace->buf[0];

	trace->cur_size = 0;
	raw_spin_unlock_irqrestore(&trace->lock, flags);

	if (copy_to_user(usr_buffer, buf, cur_size * sizeof(struct rk_trace_data))) {
		printk("sys_rk_trace_get: copy_to_user failed\n");
		return RK_ERROR;
	}	
	return cur_size;
}


#ifdef RK_TRACE_SUM
static inline void rk_trace_set_sched_data(struct rk_trace_data_sum *p, bool flag)
{
#ifdef RK_TRACE_SUM_HISTORY
	if (p->nr_sched < RK_TRACE_SUM_NR_HISTORY) {
		int index = p->nr_sched++;
		p->sched_onoff[index] = flag;
		p->sched_core[index] = raw_smp_processor_id();
		rk_rdtsc(&p->sched_time[index]);
	}
	if (p->nr_l3miss < RK_TRACE_SUM_NR_HISTORY) {
		if (flag == false) {
			int index = p->nr_l3miss++;
			p->l3miss[index] = p->end.l3_miss - p->start.l3_miss;
		}
	}
#endif
}

#define RK_TRACE_SUM_ARCHIVE_MAX 20
int rk_trace_sum_terminated_pid[RK_MAX_CPUS][RK_TRACE_SUM_ARCHIVE_MAX] = {{0,},};
struct rk_trace_data_sum rk_trace_sum_terminated[RK_MAX_CPUS][RK_TRACE_SUM_ARCHIVE_MAX];

// Save PMC data into archive list (useful for terminating task)
void save_rk_trace_sum(struct task_struct *p)
{
	struct rk_trace_data_sum *trace = p->rk_trace;
	int i, cpuid;
	unsigned long flags;

	local_irq_save(flags);
	if (p == current) {
		printk("pid %d : save_rk_trace_sum\n", p->pid);
		get_pmc_info(&trace->end);

		trace->total.inst_retired_any += 
			trace->end.inst_retired_any - trace->start.inst_retired_any;
		trace->total.cpu_clk_unhalted += 
			trace->end.cpu_clk_unhalted - trace->start.cpu_clk_unhalted;

		trace->total.l1_hit += 
			trace->end.l1_hit - trace->start.l1_hit;
		trace->total.l2_hit += 
			trace->end.l2_hit - trace->start.l2_hit;
		trace->total.l3_hit += 
			trace->end.l3_hit - trace->start.l3_hit;
		trace->total.l3_miss += 
			trace->end.l3_miss - trace->start.l3_miss;

		trace->total.invariant_tsc += 
			trace->end.invariant_tsc - trace->start.invariant_tsc;

		rk_trace_set_sched_data(trace, false);
	}	
	cpuid = raw_smp_processor_id();
	for (i = 0; i < RK_TRACE_SUM_ARCHIVE_MAX; i++) {
		if (rk_trace_sum_terminated_pid[cpuid][i]) continue;
		rk_trace_sum_terminated_pid[cpuid][i] = p->pid;
		rk_trace_sum_terminated[cpuid][i] = *trace;
		local_irq_restore(flags);
		return;
	}
	// is full?
	for (i = 0; i < RK_TRACE_SUM_ARCHIVE_MAX; i++) {
		rk_trace_sum_terminated_pid[cpuid][i] = 0;
	}
	rk_trace_sum_terminated_pid[cpuid][0] = p->pid;
	rk_trace_sum_terminated[cpuid][0] = *trace;
	local_irq_restore(flags);
}

int sys_rk_trace_sum_set(int pid)
{
	struct task_struct *p;
	struct rk_trace_data_sum *trace;

	printk("[Start RK Trace PMC : pid %d]\n", pid);
	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if(p == NULL) {
		printk("ERROR: cannot find task with pid %d\n", pid);
		return RK_ERROR;
	}
	if (p->rk_trace) {
		printk("ERROR: pid %d already has started tracing\n", pid);
		return RK_ERROR;
	}
	trace = vmalloc(sizeof(struct rk_trace_data_sum));
	if (trace == NULL) {
		printk("ERROR: vmalloc error\n");
		return RK_ERROR;
	}
	memset(trace, 0, sizeof(struct rk_trace_data_sum));
	p->rk_trace = trace;
	if (current == p) {
		get_pmc_info(&((struct rk_trace_data_sum*)p->rk_trace)->start);
		rk_trace_set_sched_data(p->rk_trace, true);
	}
	return RK_SUCCESS;
}

int sys_rk_trace_sum_get(int pid, void *usr_buffer)
{
	struct task_struct *p;
	struct rk_trace_data_sum *trace;
	int i, j, cpuid;

	printk("[Get RK Trace : pid %d]\n", pid);
	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if(p == NULL) {
		// lookup archive list
		cpuid = raw_smp_processor_id();
		for (i = 0; i < RK_TRACE_SUM_ARCHIVE_MAX; i++) {
			if (rk_trace_sum_terminated_pid[cpuid][i] != pid) continue;
			rk_trace_sum_terminated_pid[cpuid][i] = 0;
			if (copy_to_user(usr_buffer, &rk_trace_sum_terminated[cpuid][i], sizeof(struct rk_trace_data_sum))) {
				printk("ERROR: copy_to_user failed\n");
			}
			return RK_SUCCESS;
		}
		// lookup other core's archive list 
		for (j = 0; j < num_cpus; j++) {
			if (j == cpuid) continue;
			for (i = 0; i < RK_TRACE_SUM_ARCHIVE_MAX; i++) {
				if (rk_trace_sum_terminated_pid[j][i] != pid) continue;
				rk_trace_sum_terminated_pid[j][i] = 0;
				if (copy_to_user(usr_buffer, &rk_trace_sum_terminated[j][i], sizeof(struct rk_trace_data_sum))) {
					printk("ERROR: copy_to_user failed\n");
				}
				return RK_SUCCESS;
			}
		}
		printk("ERROR: cannot find task with pid %d\n", pid);
		return RK_ERROR;
	}
	if (p->rk_trace == NULL) {
		printk("ERROR: doesn't have trace data\n");
		return RK_ERROR;
	}
	trace = p->rk_trace;
	if (copy_to_user(usr_buffer, trace, sizeof(struct rk_trace_data_sum))) {
		printk("ERROR: copy_to_user failed\n");
	}
	vfree(trace);
	p->rk_trace = NULL;
	return RK_SUCCESS;
}

static inline void __rk_trace_sum_schedule(struct task_struct *prev, struct task_struct *next)
{
	struct rk_trace_data_sum *trace;
	if (prev && prev->rk_trace) {
		trace = prev->rk_trace;
		get_pmc_info(&trace->end);
		if (trace->start.invariant_tsc > 0) {
			trace->total.inst_retired_any += 
				trace->end.inst_retired_any - trace->start.inst_retired_any;
			trace->total.cpu_clk_unhalted += 
				trace->end.cpu_clk_unhalted - trace->start.cpu_clk_unhalted;

			trace->total.l1_hit += 
				trace->end.l1_hit - trace->start.l1_hit;
			trace->total.l2_hit += 
				trace->end.l2_hit - trace->start.l2_hit;
			trace->total.l3_hit += 
				trace->end.l3_hit - trace->start.l3_hit;
			trace->total.l3_miss += 
				trace->end.l3_miss - trace->start.l3_miss;

			trace->total.invariant_tsc += 
				trace->end.invariant_tsc - trace->start.invariant_tsc;

			rk_trace_set_sched_data(trace, false);
		}
	}
	if (next && next->rk_trace) {
		trace = next->rk_trace;
		get_pmc_info(&trace->start);

		rk_trace_set_sched_data(trace, true);
	}
}
#endif // RK_TRACE_SUM


/**************************************************************
 *
 * RK system event log
 *
 **************************************************************/
#ifdef RK_EVENT_LOG

struct rk_event_data_set *event_log = NULL;
void rk_event_log_init(void)
{
	struct rk_event_data_set *log;
	int size = RK_EVENT_LOG_SIZE;

	log = vmalloc(sizeof(struct rk_event_data_set));
	if (log == NULL) {
		printk("rk_event_log_init: vmalloc error (rk_event_data_set)\n");
		return;
	}
	log->buf[0] = vmalloc(sizeof(struct rk_event_data) * size);
	if (log->buf[0] == NULL) {
		printk("rk_event_log_init: vmalloc error (buf[0])\n");
		vfree(log);
		return;
	}    
	log->buf[1] = vmalloc(sizeof(struct rk_event_data) * size);
	if (log->buf[1] == NULL) {
		printk("rk_event_log_init: vmalloc error (buf[1])\n");
		vfree(log->buf[0]);
		vfree(log);
		return;
	}    
	memset(log->buf[0], 0, sizeof(struct rk_event_data) * size);
	memset(log->buf[1], 0, sizeof(struct rk_event_data) * size);
	log->cur_buf = log->buf[0];

	raw_spin_lock_init(&log->lock);
	log->cur_size = 0; 
	log->max_size = size;
	event_log = log;
}

void rk_event_log_cleanup(void)
{
	if (event_log) {
		struct rk_event_data_set *log = event_log;
		event_log = NULL;
		raw_spin_unlock_wait(&log->lock);
		vfree(log->buf[0]);
		vfree(log->buf[1]);
		vfree(log);
	}
}

void __rk_event_log_save(int type, int cpuid, int pid, unsigned long arg1, unsigned long arg2, unsigned long long time)
{
	struct rk_event_data *buf;
	unsigned long flags;

	if (event_log == NULL) return;

	raw_spin_lock_irqsave(&event_log->lock, flags);
	buf = event_log->cur_buf;
	
	if (event_log->cur_size < event_log->max_size) {
		int idx = event_log->cur_size++;
		buf[idx].time = time;
		buf[idx].type = type;
		buf[idx].cpuid = cpuid;
		buf[idx].pid = pid;
		buf[idx].arg1 = arg1;
		buf[idx].arg2 = arg2;
	}
	raw_spin_unlock_irqrestore(&event_log->lock, flags);
}

void rk_event_log_save(int type, int cpuid, int pid, unsigned long arg1, unsigned long arg2)
{
	cpu_tick_data_t tm;
	rk_rdtsc(&tm);
	__rk_event_log_save(type, cpuid, pid, arg1, arg2, tm);
}

int sys_rk_event_log_set(int pid)
{
	struct task_struct *p;
	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if (p == NULL) return RK_ERROR;
	
	p->rk_event_log = true;
	return RK_SUCCESS;
}

int sys_rk_event_log_get(void *usr_buffer)
{
	struct rk_event_data *buf;
	unsigned long flags;
	int cur_size;

	if (event_log == NULL) {
		printk("sys_rk_event_log_get: no event log buffer\n");
		return RK_ERROR;
	}
	raw_spin_lock_irqsave(&event_log->lock, flags);

	cur_size = event_log->cur_size;
	if (cur_size == 0) {
		// nothing to copy
		raw_spin_unlock_irqrestore(&event_log->lock, flags);
		return 0;
	}
	buf = event_log->cur_buf;
	if (event_log->cur_buf == event_log->buf[0]) event_log->cur_buf = event_log->buf[1];
	else event_log->cur_buf = event_log->buf[0];

	event_log->cur_size = 0;
	raw_spin_unlock_irqrestore(&event_log->lock, flags);

	if (copy_to_user(usr_buffer, buf, cur_size * sizeof(struct rk_event_data))) {
		printk("sys_rk_event_log_get: copy_to_user failed\n");
		return RK_ERROR;
	}
	return cur_size;
}

static inline void __rk_event_log_schedule(struct task_struct *prev, struct task_struct *next)
{
	cpu_tick_data_t ts = 0;
	int cpuid = -1;
	
	if (prev && prev->rk_event_log) {
		if (is_virtualized) {
			rk_send_vm_event(RK_EVENT_TYPE_VM_TASK_STOP, prev->pid);
		}
		else {
			rk_rdtsc(&ts);
			cpuid = raw_smp_processor_id();
			__rk_event_log_save(RK_EVENT_TYPE_TASK_STOP, cpuid, prev->pid, 0, prev->rt_priority, ts);
		}
	}
	if (next && next->rk_event_log) {
		if (is_virtualized) {
			rk_send_vm_event(RK_EVENT_TYPE_VM_TASK_START, next->pid);
		}
		else {
			if (ts == 0 || cpuid < 0) {
				rk_rdtsc(&ts);
				cpuid = raw_smp_processor_id();
			}
			__rk_event_log_save(RK_EVENT_TYPE_TASK_START, cpuid, next->pid, 0, next->rt_priority, ts);
		}
	}
}

#else
static inline void __rk_event_log_schedule(struct task_struct *prev, struct task_struct *next) {}
#endif // RK_EVENT_LOG


#ifdef RK_TRACE
void rk_trace_schedule(struct task_struct *prev, struct task_struct *next)
{
#ifdef RK_TRACE_SUM
	__rk_trace_sum_schedule(prev, next);
#else
	__rk_trace_schedule(prev, next);
#endif

	__rk_event_log_schedule(prev, next);
}
#endif // RK_TRACE


/**************************************************************
 *
 * RK trace & event log system call
 *
 **************************************************************/
asmlinkage int sys_rk_trace(int type, int nr, void *data)
{	
	int ret = RK_ERROR;
	switch (type) {
#ifndef RK_TRACE_SUM
	case RK_TRACE_SYSCALL_SET:
		ret = sys_rk_trace_set(nr, (long)data); // nr: pid, data: buffer_size
		break;
	case RK_TRACE_SYSCALL_GET:
		ret = sys_rk_trace_get(nr, data); // nr: pid, data: ptr_output_buffer
		break;
#else
	// RK_TRACE_SUM: Simple version of RK_TRACE (records only sum of exec times)
	case RK_TRACE_SYSCALL_SUM_SET:
		ret = sys_rk_trace_sum_set(nr);
		break;
	case RK_TRACE_SYSCALL_SUM_GET:
		ret = sys_rk_trace_sum_get(nr, data);
		break;
#endif
	case RK_TRACE_SYSCALL_EVENT_LOG_SET:
		ret = sys_rk_event_log_set(nr);
		break;
	case RK_TRACE_SYSCALL_EVENT_LOG_GET:
		ret = sys_rk_event_log_get(data);
		break;
	}
	return ret;
}



/**************************************************************
 *
 * Virtual memory debug functions 
 *
 **************************************************************/

#ifdef CONFIG_RK_MEM
int isolate_lru_page(struct page *page);
int page_referenced(struct page *page, int is_locked, struct mem_cgroup *cnt, unsigned long *vm_flags);
int rk_page_list_out(struct zone* zone, struct list_head *page_list, int n);
extern raw_spinlock_t mem_reserve_lock;

inline static unsigned long addr_lower(unsigned long addr)
{
	//return (addr / PAGE_SIZE) * PAGE_SIZE;
	return addr;
}
inline static unsigned long addr_upper(unsigned long addr)
{
	//return ((addr + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
	return addr;
}

int get_address_info(struct mm_struct *mm, unsigned long addr)
{
	if(addr_lower(mm->start_code) <= addr && addr <= addr_upper(mm->end_code)) return 0;
	if(addr_lower(mm->start_data) <= addr && addr <= addr_upper(mm->end_data)) return 1;
	if(addr_lower(mm->start_brk)  <= addr && addr <= addr_upper(mm->brk)) return 2;
	if(addr_lower(mm->arg_start)  <= addr && addr <= addr_upper(mm->arg_end)) return 3;
	if(addr_lower(mm->env_start)  <= addr && addr <= addr_upper(mm->env_end)) return 4;
	if(addr_lower(mm->start_stack) - (mm->stack_vm * PAGE_SIZE) <= addr && addr <= addr_upper(mm->start_stack)) return 5;
/*
	if(mm->start_code / PAGE_SIZE <= addr / PAGE_SIZE 
			&& addr / PAGE_SIZE <= mm->end_code / PAGE_SIZE) return 0;
	if(mm->start_data / PAGE_SIZE <= addr / PAGE_SIZE
			&& addr / PAGE_SIZE <= mm->end_data / PAGE_SIZE) return 1;
	if(mm->start_brk / PAGE_SIZE <= addr / PAGE_SIZE 
			&& addr / PAGE_SIZE <= mm->brk / PAGE_SIZE) return 2;
	if(mm->start_stack / PAGE_SIZE <= addr / PAGE_SIZE 
			&& addr / PAGE_SIZE <= mm->env_end / PAGE_SIZE) return 3;
*/
	return 6;	
}

//#define VERBOSE_VMINFO
int sys_rk_mem_reserve_show_task_vminfo(int pid)
{
	struct task_struct *p;
	struct mm_struct *mm;
	struct vm_area_struct *mmap;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	long unsigned int prev_state, prev_start, prev_end;
	int n_physical;
	//int mapcount = 0;
	int n_mem_reserve = 0;
	int n_mem_file = 0, n_mem_others = 0, n_mem_anon = 0;
	int n_estimated_total = 0;
	int n_shared = 0;
	//unsigned long vm_flags;
	int *color_usage, *bank_color_usage;
	int mem_rsv_colors = mem_reserve_get_nr_colors();
	int mem_rsv_bank_colors = mem_reserve_get_nr_bank_colors();
	int i;

	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if (p == NULL) {
		printk("rk_mem_reserve_show_task_vminfo: cannot find task %d\n", pid);
		return RK_ERROR;
	}

	color_usage = vzalloc(sizeof(int) * RK_MEM_MAX_COLOR);
	bank_color_usage = vzalloc(sizeof(int) * RK_MEM_MAX_COLOR);
	if (!color_usage || !bank_color_usage) {
		printk("rk_mem_reserve_show_task_vminfo: memory allocation error\n");
		if (color_usage) vfree(color_usage);
		if (bank_color_usage) vfree(bank_color_usage);
		return RK_ERROR;
	}
    
	printk("[Physical address of PID %d]\n", pid);
	/* 
	 * just 'mm' is null in kernel thread,
	 * so use active_mm 
	 */
	mm = p->active_mm;

	prev_state = prev_start = prev_end = 0;
	n_physical = 0;
	
	mmap = mm->mmap;
	printk("  Code : %lx - %lx\n", addr_lower(mm->start_code),  addr_upper(mm->end_code));
	printk("  Data : %lx - %lx\n", addr_lower(mm->start_data),  addr_upper(mm->end_data));
	printk("  Heap : %lx - %lx\n", addr_lower(mm->start_brk),   addr_upper(mm->brk));
	printk("  Arg  : %lx - %lx\n", addr_lower(mm->arg_start),   addr_upper(mm->arg_end));
	printk("  Env  : %lx - %lx\n", addr_lower(mm->env_start),   addr_upper(mm->env_end));
	printk("  Stack: %lx - %lx\n", addr_lower(mm->start_stack) - mm->stack_vm * PAGE_SIZE, addr_upper(mm->start_stack));
	printk("  Total: %lu, Shared: %lu, Exec: %lu, Stack: %lu\n", 
		mm->total_vm, mm->shared_vm, mm->exec_vm, mm->stack_vm);
	while(mmap) {
		unsigned long vaddr = mmap->vm_start;
		int n_local_shared = 0, n_local_private = 0;
		printk("##### Mem Region: vm_start:%lx - end:%lx (%lu bytes) - F:%lx", mmap->vm_start, mmap->vm_end, mmap->vm_end - mmap->vm_start, mmap->vm_flags);
		if (mmap->vm_flags & 0xf) {
			n_estimated_total += (mmap->vm_end - mmap->vm_start) / PAGE_SIZE;
			printk(" -> %lu pages\n", (mmap->vm_end - mmap->vm_start) / PAGE_SIZE);
		}
		else {
			printk("\n");
		}
		if (mmap->vm_flags & (VM_IO | VM_PFNMAP)) goto next_vma;
		while (vaddr < mmap->vm_end)	{
			pgd = pgd_offset(mmap->vm_mm, vaddr);
			if (pgd_none(*pgd) || !pgd_present(*pgd)) goto find_next_page;
			pud = pud_offset(pgd, vaddr);
			if (pud_none(*pud) || !pud_present(*pud)) goto find_next_page;
			pmd = pmd_offset(pud, vaddr);
			if (pmd_none(*pmd) || !pmd_present(*pmd)) goto find_next_page;
			pte = pte_offset_map(pmd, vaddr);
			//if (pte_none(*pte) || !pte_present(*pte)) goto unmap;
			if (pte_none(*pte)) goto unmap;
                       
			if (!pte_present(*pte)) {
				#ifdef VERBOSE_VMINFO
				printk("%lx|evicted page(pte_flag:%lx)\n", vaddr, pte_flags(*pte) & PTE_FLAGS_MASK);
				#endif
			}			
			else {
				struct page *page;
				unsigned long phy_addr;
				page = pte_page(*pte);
				//phy_addr = pte_val(*pte) & PTE_PFN_MASK;
				phy_addr = page_to_pfn(page);

				color_usage[mem_reserve_get_color_idx(page)]++;
				bank_color_usage[mem_reserve_get_bank_color_idx(page)]++;

				n_physical++;
				if (page_mapcount(page) > 1) {
					n_shared++;
					n_local_shared++;
				}
				else {
					n_local_private++;
				}

				if (PageMemReserve(page)) {
					struct mem_reserve_page *entry = page->rsv;
					bool is_shared = false;
					int access_count = -1;
					int nr_referenced;
					if (entry) {
						if (list_empty(&entry->shared) == false) is_shared = true;
						access_count = entry->access_count;
					}
					n_mem_reserve++;
					//printk("%lx|memrsv page(%lx)%lx:%lx, f:%x, c:%d, mc:%d, rsv:%d, %s, shr:%d, ac:%d\n", 
					//nr_referenced = page_referenced(page, false, NULL, &vm_flags);
					nr_referenced = 0;
					#ifdef VERBOSE_VMINFO
					printk("%lx|memrsv page(%lx)%lx pfn:%lx, f:%x, c:%d, mc:%d, %s, rsv:%d, shr:%d, ac:%d, ref:%d\n", 
						vaddr, pte_flags(*pte) & PTE_FLAGS_MASK, (unsigned long)page, phy_addr, 
						(unsigned int)page->flags, page_count(page), 
						page_mapcount(page),  
						page->mapping == NULL ? "N/A" : 
						(((unsigned long)page->mapping & 0x1) ? "Anon" : "File"),
						PageMemReserve(page),
						is_shared, access_count,
						nr_referenced);
					if (is_shared) {
						struct list_head *head, *shared_list;
						struct mem_reserve_page *shared;
						head = shared_list = &entry->shared;
						shared = entry;
						printk("   - ");
						do {
							printk("%d ", shared->mem->rsv->parent_resource_set->rd_entry);
							shared_list = shared->shared.next;
							shared = list_entry(shared_list, struct mem_reserve_page, shared);
						} while (shared_list != head);
						printk("\n");
					}
					#endif
				}
				else {
					#ifdef VERBOSE_VMINFO
					printk("%lx|page(%lx)%lx pfn:%lx, f:%x, c:%d, mc:%d, %s\n", 
						vaddr, pte_flags(*pte) & PTE_FLAGS_MASK, (unsigned long)page, phy_addr,
						(unsigned int)page->flags, page_count(page), 
						page_mapcount(page), 
						page->mapping == NULL ? "N/A" : 
						(((unsigned long)page->mapping & 0x1) ? "Anon" : "File")
						);
						//vm_area_str[get_address_info(mm, n)]);
					#endif
				}
				if (page->mapping == NULL) n_mem_others++;
				else if ((unsigned long)page->mapping & 0x1) n_mem_anon++;
				else n_mem_file++;
			}


			// now, this page exists in physical memory
			//physical_addr = pte_val(*pte)&(0xfffff000);
			//physical_addr = pte_val(*pte);

			// virt_to_page(n) <- only valid for kernel linear address
			//printk("%lx\n", (unsigned long)pte_page(*pte));
unmap:
			pte_unmap(pte);
find_next_page:
			vaddr += PAGE_SIZE;
		}
		printk("-----------n_local_shared:%d, n_local_private:%d\n", n_local_shared, n_local_private);
next_vma:
		mmap = mmap->vm_next;
	}
	printk("PID %d - n_physical: %d, n_mem_reserve:%d, n_estimated_total:%d\n", pid, n_physical, n_mem_reserve, n_estimated_total);
	printk("       - n_anon(data): %d, n_file(page-cache): %d, n_others(N/A): %d\n", n_mem_anon, n_mem_file, n_mem_others);
	//printk("         (file rss: %lu, anon rss: %lu)\n", get_mm_counter(mm, file_rss), get_mm_counter(mm, anon_rss));
	printk("         (file rss: %lu, anon rss: %lu)\n", get_mm_counter(mm, MM_FILEPAGES), get_mm_counter(mm, MM_ANONPAGES));
	printk("       - shared :%d (%lu)\n", n_shared, n_shared * PAGE_SIZE);
	printk("%lu / %lu \n", (unsigned long)sizeof(struct page), (unsigned long)sizeof(struct mem_reserve_page));

	printk("[Cache color usage of PID %d]\n", pid);
	for (i = 0; i < mem_rsv_colors; i++) {
		printk("color %d: %d\n", i, color_usage[i]);
	}
	printk("[Bank color usage of PID %d]\n", pid);
	for (i = 0; i < mem_rsv_bank_colors; i++) {
		printk("color %d: %d\n", i, bank_color_usage[i]);
	}
	vfree(color_usage);
	vfree(bank_color_usage);
	return RK_SUCCESS;
}


int sys_rk_mem_reserve_show_reserved_pages(int rd)
{
	rk_resource_set_t rset;
	struct mem_reserve_page *entry;
	int i, j;
	mem_reserve_t mem;
	int measured_free = 0;
	int measured_act = 0;
	int measured_inact = 0;

	if (rd < 0 || rd >= MAX_RESOURCE_SETS) {
		printk("show_reserved_pages: invalid rd\n");
		return RK_ERROR;
	}
	rk_sem_down();
	rset = resource_set_descriptor[rd];
	if (rset == NULL || rset->mem_reserve == NULL) {
		goto error_sem_unlock;
	}
 	mem = rset->mem_reserve->reserve;	
	if (mem == NULL) {
		printk("show_reserved_pages: mem_reserve is null\n");
		goto error_sem_unlock;
	}
	printk("[Memory Reservation Info: Resource Set %d]\n", rd);

	raw_spin_lock(&mem_reserve_lock);
	printk("****** Free list ******\n");
	for (i = 0; i < MEM_RSV_COLORS; i++) 
	for (j = 0; j < MEM_RSV_BANK_COLORS; j++) {
		list_for_each_entry(entry, &mem->mem_free_list[i][j], list) {
			#ifdef VERBOSE_VMINFO
			struct page* page = entry->page;
			if (entry == NULL) printk(" - Entry: NULL - \n");
			else if (page == NULL) printk(" - Page: NULL - \n");
			else {
				bool is_shared = false;
				int access_count = -1;
				if (list_empty(&entry->shared) == false) is_shared = true;
				access_count = entry->access_count;
				printk("page:%lx, pfn:%lx, f:%x, c:%d, mc:%d, rsv:%d, %s, ex:%d, shr:%d, ac:%d\n", 
					(unsigned long)page, 
					page_to_pfn(page),
					(unsigned int)page->flags, 
					page_count(page), 
					page_mapcount(page), 
					PageMemReserve(page), 
					page->mapping == NULL ? "N/A" 
						: (((unsigned long)page->mapping & 0x1) 
						? "Anon" : "File"),
					entry->executable, 
					is_shared, access_count);
				if (is_shared) {
					struct list_head *head, *shared_list;
					struct mem_reserve_page *shared;
					head = shared_list = &entry->shared;
					shared = entry;
					printk("   - ");
					do {
						printk("%d ", shared->mem->rsv->parent_resource_set->rd_entry);
						shared_list = shared->shared.next;
						shared = list_entry(shared_list, struct mem_reserve_page, shared);
					} while (shared_list != head);
					printk("\n");
				}
			}
			#endif
			measured_free++;
		}
		printk(" - cache %d, bank %d : %d pages\n", i, j, mem->mem_free_size_detail[i][j]);
	}

	for (i = 0; i < 2; i++) {
		struct list_head *mem_list;
		if (i == 0) {
			printk("****** Active list ******\n");
			mem_list = &mem->mem_active_list;
		}
		else {
			printk("****** Inactive list ******\n");
			mem_list = &mem->mem_inactive_list;
		}
		list_for_each_entry(entry, mem_list, list) {
			#ifdef VERBOSE_VMINFO
			struct page* page = entry->page;
			if (entry == NULL) printk(" - Entry NULL - \n");
			else if (page == NULL) printk(" - NULL - \n");
			else {
				bool is_shared = false;
				int access_count = -1;
				if (list_empty(&entry->shared) == false) is_shared = true;
				access_count = entry->access_count;

				printk("entry:%lx, page:%lx(pfn:%lx), f:%x, c:%d, mc:%d, rsv:%d, %s, ex:%d, shr:%d, ac:%d\n", 
					(unsigned long)entry,
					(unsigned long)page, 
					page_to_pfn(page),
					(unsigned int)page->flags, 
					page_count(page), 
					page_mapcount(page), 
					PageMemReserve(page), 
					page->mapping == NULL ? "N/A" 
						: (((unsigned long)page->mapping & 0x1) 
						? "Anon" : "File"),
					entry->executable, 
					is_shared, access_count);
				if (is_shared) {
					struct list_head *head, *shared_list;
					struct mem_reserve_page *shared;
					head = shared_list = &entry->shared;
					shared = entry;
					printk("   - ");
					do {
						printk("%d ", shared->mem->rsv->parent_resource_set->rd_entry);
						shared_list = shared->shared.next;
						shared = list_entry(shared_list, struct mem_reserve_page, shared);
					} while (shared_list != head);
					printk("\n");
				}
			}
			#endif
			if (i == 0) measured_act++;
			else measured_inact++;
		}
		if (i == 0) printk(" - %d pages\n", measured_act);
		else printk(" - %d pages\n", measured_inact);
	}

	printk("Summary - total: %d, free:%d, used:%d (act:%d, inact:%d), conserved:%d\n", 
		mem->mem_total_size, mem->mem_free_size, 
		mem->mem_used_size, mem->mem_active_size, mem->mem_inactive_size,
		mem->mem_conserved_size);
	printk(" - cache colors: %d { ", mem->mem_res_attr.nr_colors);
	for (i = 0; i < mem->mem_res_attr.nr_colors; i++) printk("%d ", mem->mem_res_attr.colors[i]);
	printk("}\n");
	printk(" - bache colors: %d { ", mem->mem_res_attr.nr_bank_colors);
	for (i = 0; i < mem->mem_res_attr.nr_bank_colors; i++) printk("%d ", mem->mem_res_attr.bank_colors[i]);
	printk("}\n");


	if (measured_free != mem->mem_free_size) printk("ERR - mem_free_size:%d, n list:%d\n", mem->mem_free_size, measured_free);
	if (measured_act != mem->mem_active_size) printk("ERR - mem_active_size:%d, n list:%d\n", mem->mem_active_size, measured_free);
	if (measured_inact != mem->mem_inactive_size) printk("ERR - mem_inactive_size:%d, n list:%d\n", mem->mem_inactive_size, measured_free);

	raw_spin_unlock(&mem_reserve_lock);
	rk_sem_up();
	return RK_SUCCESS;

error_sem_unlock:
	rk_sem_up();
	return RK_ERROR;
}

int sys_rk_mem_reserve_swap_out_page(int pid, void *addr)
{
	struct task_struct *p;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	struct page *page;
	int i, n_list_now = 0, n_evicted = 0;
	struct zone *zone = NULL;
	LIST_HEAD(page_list);
	unsigned long vm_flags;

	printk("[Swap out page : pid %d, vaddr:%lx]\n", pid, (unsigned long)addr);
	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if(p == NULL) {
		printk("ERROR: cannot find task with pid %d\n", pid);
		return RK_ERROR;
	}
	for (i = 0; i < 128; i++) {
		unsigned long n = (unsigned long)addr + PAGE_SIZE * i;
		pgd = pgd_offset(p->mm, n);
		if (pgd_none(*pgd) || !pgd_present(*pgd)) continue;
		pud = pud_offset(pgd, n);
		if (pud_none(*pud) || !pud_present(*pud)) continue; 
		pmd = pmd_offset(pud, n);
		if (pmd_none(*pmd) || !pmd_present(*pmd)) continue; 
		pte = pte_offset_map(pmd, n);
		if (pte_none(*pte) || !pte_present(*pte)) {
			printk("ERROR: page is not present\n");		
			goto unmap;
		}
		page = pte_page(*pte);
		
		if (PageWriteback(page)) {
			printk("ERROR: page is under writeback\n");		
			goto unmap; // page under writeback
		}
		if (!PageLRU(page)) {
			printk("ERROR: page is under writeback\n");		
			goto unmap;
		}
		if (isolate_lru_page(page)) {
			printk("ERROR: failed to isolate from LRU\n");		
			goto unmap;
		}
		if (PageMemReserve(page)) {
			// Clear unevictable flag (because it's isolated from LRU)
			lock_page(page);
			ClearPageUnevictable(page);
			unlock_page(page);
		}
		page_referenced(page, false, NULL, &vm_flags);

		/*
		if (page->rsv && !list_empty(&((struct mem_reserve_page*)(page->rsv))->shared)) {
			if (!trylock_page(page)) goto unmap;

			if (SWAP_SUCCESS == try_to_unmap(page, TTU_RK_UNMAP | TTU_IGNORE_ACCESS)) {
				printk("UNMAP!! (page:%lx)\n", (unsigned long)page);
				n_list_now++;
			}
			else {
				printk("UNMAP failed!! (page:%lx)\n", (unsigned long)page);
			}
			unlock_page(page);
			goto unmap;
		}*/
		zone = page_zone(page);
		n_list_now++;
		list_add(&page->lru, &page_list);
		printk(" - page: %lx\n", (unsigned long)page);
unmap:
		pte_unmap(pte);
	}
	if (n_list_now) {
		n_evicted = rk_page_list_out(zone, &page_list, n_list_now);
	}
	printk(" - requested: %d, evicted: %d\n", n_list_now, n_evicted);

	return RK_SUCCESS;	
}


int sys_rk_mem_reserve_do_alloc_test(int nr)
{
	unsigned long long t1, t2;
	int i, n = nr;
	int NR_MAX = 25600;
	struct page** tmp_page;

	if (n > NR_MAX) n = NR_MAX;
	tmp_page = vmalloc(sizeof(struct page*) * n);
	if (!tmp_page) goto error;

	for (i = 0; i < n; i++) tmp_page[i] = NULL;

	rk_rdtsc(&t1);
	for (i = 0; i < n; i++) {
		tmp_page[i] = alloc_page(GFP_HIGHUSER_MOVABLE);
		if (tmp_page[i] == NULL) goto error;
	}
	rk_rdtsc(&t2);

	printk("alloc(%d) : %llu nsec\n", n, t2 - t1);

	rk_rdtsc(&t1);
	for (i = 0; i < n; i++) {
		__free_page(tmp_page[i]);
	}
	rk_rdtsc(&t2);
	printk("free(%d): %llu nsec\n", n, t2 - t1);
	vfree(tmp_page);
	return RK_SUCCESS;

error:
	printk("ERROR!! Test stopped\n");
	if (tmp_page) {
		for (i = 0; i < n; i++) {
			if (tmp_page[i] == NULL) break;
			__free_page(tmp_page[i]);
		}
		vfree(tmp_page);
	}
	return RK_ERROR;
}

int sys_rk_mem_reserve_traverse_page_table(int pid)
{
	struct task_struct *p;
	struct mm_struct *mm;
	unsigned long i, j, k, l;

	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if (p == NULL) {
		printk("rk_mem_reserve_traverse_page_table: cannot find task %d\n", pid);
		return RK_ERROR;
	}
	mm = p->active_mm;
	for (i = 0; i < PTRS_PER_PGD; ++i) {
		pgd_t *pgd = mm->pgd + i;
		if (pgd_none(*pgd) || !pgd_present(*pgd)) continue;
		if (!(pgd_val(*pgd) & _PAGE_USER)) continue; // x86

		for (j = 0; j < PTRS_PER_PUD; ++j) {
			pud_t *pud = (pud_t *)pgd_page_vaddr(*pgd) + j;
			if (pud_none(*pud) || !pud_present(*pud)) continue;
			if (!(pud_val(*pud) & _PAGE_USER)) continue; // x86

			for (k = 0; k < PTRS_PER_PMD; ++k) {
				pmd_t *pmd = (pmd_t *)pud_page_vaddr(*pud) + k;
				if (pmd_none(*pmd) || !pmd_present(*pmd)) continue;
				if (!(pmd_val(*pmd) & _PAGE_USER)) continue;

				for (l = 0; l < PTRS_PER_PTE; ++l) {
					struct page *p;
					unsigned long pfn;
					unsigned long vfn;
					pte_t *pte = (pte_t *)pmd_page_vaddr(*pmd) + l;
					if (!pte || !pte_present(*pte)) continue;
					if (!(pte_val(*pte) & _PAGE_USER)) continue; // x86
					if ((pte_flags(*pte) & _PAGE_GLOBAL)) continue; // x86

					p = pte_page(*pte);
					pfn = page_to_pfn(p);
					vfn = ((i << PGDIR_SHIFT) | (j << PUD_SHIFT) | (k << PMD_SHIFT) | (l << PAGE_SHIFT)) >> PAGE_SHIFT;
					printk("vfn %lx, pfn %lx (pte flag: %lx)\n", vfn, pfn, pte_flags(*pte) & PTE_FLAGS_MASK);
				}
			}
		}
	}
	
	return RK_SUCCESS;
}


#endif // CONFIG_RK_MEM



/**************************************************************
 *
 * Hardware Performance Counters
 *
 **************************************************************/
#ifdef RK_PROFILE_PMC
#include <rk/rk_pmc.h>

#if defined(RK_X86_SANDYBRIDGE)

void __rk_pmc_init(void)
{
	struct core_event_desc core_event_desc[4];
	struct fixed_event_ctrl_reg ctrl_reg;
	struct event_select_reg event_select_reg;
	cpumask_t cpumask;
	int i, j;
	uint64_t regs;

	// Disable Intel Speed Step/Turbo Boost
	rdmsrl(MSR_IA32_MISC_ENABLE, regs);
	printk("RK: MSR_IA32_MISC_ENABLE : %llx ", regs);
	regs |= MSR_IA32_MISC_ENABLE_TURBO_DISABLE;
	//regs &= ~MSR_IA32_MISC_ENABLE_TURBO_DISABLE;
	//regs |= MSR_IA32_MISC_ENABLE_PREFETCH_DISABLE; // doesn't work
	//regs |= MSR_IA32_MISC_ENABLE_ADJ_PREF_DISABLE; // doesn't work
	//regs |= MSR_IA32_MISC_ENABLE_DCU_PREF_DISABLE; // doesn't work
	//regs |= MSR_IA32_MISC_ENABLE_IP_PREF_DISABLE; // doesn't work
	regs &= ~MSR_IA32_MISC_ENABLE_ENHANCED_SPEEDSTEP; // Disable Speed Step
	//misc_enable |= MSR_IA32_MISC_ENABLE_ENHANCED_SPEEDSTEP;
	printk("-> %llx (TurboBoost disabled)\n", regs);
	wrmsrl(MSR_IA32_MISC_ENABLE, regs);

	// Core event setting (Sandy Bridge)
	/*
	// L3 Miss
	core_event_desc[0].event_number = ARCH_LLC_MISS_EVTNR;
	core_event_desc[0].umask_value = ARCH_LLC_MISS_UMASK;
	// L3 Unshared Hit
	core_event_desc[1].event_number = MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_NONE_EVTNR;
	core_event_desc[1].umask_value = MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_NONE_UMASK;
	// L3 HitM
	core_event_desc[2].event_number = MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_HITM_EVTNR;
	core_event_desc[2].umask_value = MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_HITM_UMASK;
	// L2 Hit
	core_event_desc[3].event_number = MEM_LOAD_UOPS_RETIRED_L2_HIT_EVTNR;
	core_event_desc[3].umask_value = MEM_LOAD_UOPS_RETIRED_L2_HIT_UMASK;
	*/


	/*
	// IMC Counter. doesn't work for sandy bridge..
	core_event_desc[0].event_number = UNC_QMC_NORMAL_READ_EVTNR; 
	core_event_desc[0].umask_value  = UNC_QMC_NORMAL_READ_ANY_UMASK;
	core_event_desc[1].event_number = UNC_QMC_WRITES_FULL_EVTNR;
	core_event_desc[1].umask_value  = UNC_QMC_WRITES_FULL_ANY_UMASK;
	*/
	core_event_desc[0].event_number = MEM_LOAD_UOPS_RETIRED_EVTNR;
	core_event_desc[0].umask_value  = MEM_LOAD_UOPS_RETIRED_L1_HIT_UMASK;
	core_event_desc[1].event_number = MEM_LOAD_UOPS_RETIRED_EVTNR;
	core_event_desc[1].umask_value  = MEM_LOAD_UOPS_RETIRED_L2_HIT_UMASK;
	core_event_desc[2].event_number = MEM_LOAD_UOPS_LLC_HIT_RETIRED_EVTNR;
	core_event_desc[2].umask_value  = MEM_LOAD_UOPS_LLC_HIT_RETIRED_L3_UMASK;
	core_event_desc[3].event_number = ARCH_LLC_MISS_EVTNR;
	core_event_desc[3].umask_value = ARCH_LLC_MISS_UMASK;

	for (i = 0; i < num_cpus; i++) {
		// Set CPU affinity of task
		cpus_clear(cpumask);
		cpu_set(i, cpumask);
		set_cpus_allowed_ptr(current, &cpumask);

		// Disable counters while programming
		wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0);

		// Setup fixed-function performance counters
		rdmsrl(MSR_CORE_PERF_FIXED_CTR_CTRL, ctrl_reg.value);

		ctrl_reg.fields.os0 = 1;
		ctrl_reg.fields.usr0 = 1;
		ctrl_reg.fields.any_thread0 = 0;
		ctrl_reg.fields.enable_pmi0 = 0;

		ctrl_reg.fields.os1 = 1;
		ctrl_reg.fields.usr1 = 1;
		ctrl_reg.fields.any_thread1 = 1; //(perfmon_version >= 3) ? 1 : 0;         // sum the nuber of cycles from both logical cores on one physical core
		ctrl_reg.fields.enable_pmi1 = 0;

		ctrl_reg.fields.os2 = 1;
		ctrl_reg.fields.usr2 = 1;
		ctrl_reg.fields.any_thread2 = 1; //(perfmon_version >= 3) ? 1 : 0;         // sum the nuber of cycles from both logical cores on one physical core
		ctrl_reg.fields.enable_pmi2 = 0;
		
		wrmsrl(MSR_CORE_PERF_FIXED_CTR_CTRL, ctrl_reg.value);

		// # of event selector : 4
		for (j = 0; j < 4; j++) {
			rdmsrl(MSR_P6_EVNTSEL0 + j, event_select_reg.value);

			event_select_reg.fields.event_select = core_event_desc[j].event_number;
			event_select_reg.fields.umask = core_event_desc[j].umask_value;
			event_select_reg.fields.usr = 1;
			event_select_reg.fields.os = 1;
			event_select_reg.fields.edge = 0;
			event_select_reg.fields.pin_control = 0;
			event_select_reg.fields.apic_int = 0;
			event_select_reg.fields.any_thread = 0;
			event_select_reg.fields.enable = 1;
			event_select_reg.fields.invert = 0;
			event_select_reg.fields.cmask = 0;
			
			wrmsrl(MSR_P6_PERFCTR0, 0);
			wrmsrl(MSR_P6_EVNTSEL0 + j, event_select_reg.value);
		}

		// start counting, enable all (4 programmable + 3 fixed) counters
		regs = (1ULL << 0) + (1ULL << 1) + (1ULL << 2) + (1ULL << 3) + (1ULL << 32) + (1ULL << 33) + (1ULL << 34);
		wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, regs);
	}
}

void rk_pmc_cleanup() {}

void get_pmc_info(struct pmc_counter *pmc)
{
	rdmsrl(MSR_CORE_PERF_FIXED_CTR0, pmc->inst_retired_any);
	rdmsrl(MSR_CORE_PERF_FIXED_CTR1, pmc->cpu_clk_unhalted);
	//rdmsrl(MSR_CORE_PERF_FIXED_CTR2, pmc->cpu_clk_unhalted_ref);

	rdmsrl(MSR_P6_PERFCTR0 + 0, pmc->l1_hit);
	rdmsrl(MSR_P6_PERFCTR0 + 1, pmc->l2_hit);
	rdmsrl(MSR_P6_PERFCTR0 + 2, pmc->l3_hit);
	rdmsrl(MSR_P6_PERFCTR0 + 3, pmc->l3_miss);

	rdmsrl(MSR_IA32_TSC, pmc->invariant_tsc);
}

#elif defined(RK_X86_YORKFIELD)

void __rk_pmc_init(void)
{
	struct core_event_desc core_event_desc[4];
	struct fixed_event_ctrl_reg ctrl_reg;
	struct event_select_reg event_select_reg;
	cpumask_t cpumask;
	int i, j;
	uint64_t regs;
	uint64_t core_perf_ctl = 0;

	// Core event setting (Yorkfield)
	core_event_desc[0].event_number = ARCH_LLC_MISS_EVTNR;
	core_event_desc[0].umask_value = ARCH_LLC_MISS_UMASK;

	for (i = 0; i < num_cpus; i++) {
		// Set CPU affinity of task
		cpus_clear(cpumask);
		cpu_set(i, cpumask);
		set_cpus_allowed_ptr(current, &cpumask);

		// Disable Intel Speed Step/Turbo Boost 
		// - The Yorkfield quad core processor is a pair of two dual-core processors. 
		// - Hence, we need to setup MSRs for each pair of cpus.
		if (i % 2 == 0) {
			rdmsrl(MSR_IA32_MISC_ENABLE, regs);
			printk("RK: MSR_IA32_MISC_ENABLE : %llx ", regs);

			// regs &= ~MSR_IA32_MISC_ENABLE_TURBO_DISABLE; // Enable Intel Dynamic Acceleration (IDA), predecessor of Turbo Boost
			regs |= MSR_IA32_MISC_ENABLE_TURBO_DISABLE; // Disable Intel Dynamic Acceleration (IDA), predecessor of Turbo Boost
			regs |= MSR_IA32_MISC_ENABLE_PREFETCH_DISABLE; // Works for Core 2 series
			regs |= MSR_IA32_MISC_ENABLE_ADJ_PREF_DISABLE; // Works for Core 2 series
			// Note: Do not enable/disable SpeedStep since we need to setup freq of cores
			// regs |= MSR_IA32_MISC_ENABLE_ENHANCED_SPEEDSTEP; // Enable Speed Step
			// regs &= ~MSR_IA32_MISC_ENABLE_ENHANCED_SPEEDSTEP; // Disable Speed Step
			printk("-> %llx \n", regs);
			wrmsrl(MSR_IA32_MISC_ENABLE, regs);

			// Read PERF_CTL (SpeedStep clock frequency)
			//rdmsrl(MSR_IA32_PERF_STATUS, regs);
			//printk("perf status -> %llx \n", regs);
			//rdmsrl(MSR_IA32_PERF_CTL, regs);
			//printk("perf control-> %llx \n", regs);

			// Set the frequency of the other cores as that of the first core
			if (i == 0) rdmsrl(MSR_IA32_PERF_CTL, core_perf_ctl);
			else wrmsrl(MSR_IA32_PERF_CTL, core_perf_ctl); 
		}

		// Disable counters while programming
		wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0);

		// Setup fixed-function performance counters
		rdmsrl(MSR_CORE_PERF_FIXED_CTR_CTRL, ctrl_reg.value);

		ctrl_reg.fields.os0 = 1;
		ctrl_reg.fields.usr0 = 1;
		ctrl_reg.fields.any_thread0 = 0; // N/A in Yorkfield
		ctrl_reg.fields.enable_pmi0 = 0;

		ctrl_reg.fields.os1 = 1;
		ctrl_reg.fields.usr1 = 1;
		ctrl_reg.fields.any_thread1 = 0; // N/A in Yorkfield
		ctrl_reg.fields.enable_pmi1 = 0;

		ctrl_reg.fields.os2 = 1;
		ctrl_reg.fields.usr2 = 1;
		ctrl_reg.fields.any_thread2 = 0; // N/A in Yorkfield
		ctrl_reg.fields.enable_pmi2 = 0;
		
		wrmsrl(MSR_CORE_PERF_FIXED_CTR_CTRL, ctrl_reg.value);

		// Programmable Counter - # of events: 1
		for (j = 0; j < 1; j++) {
			rdmsrl(MSR_P6_EVNTSEL0 + j, event_select_reg.value);

			event_select_reg.fields.event_select = core_event_desc[j].event_number;
			event_select_reg.fields.umask = core_event_desc[j].umask_value;
			event_select_reg.fields.usr = 1;
			event_select_reg.fields.os = 1;
			event_select_reg.fields.edge = 0;
			event_select_reg.fields.pin_control = 0;
			event_select_reg.fields.apic_int = 0;
			event_select_reg.fields.any_thread = 0;
			event_select_reg.fields.enable = 1;
			event_select_reg.fields.invert = 0;
			event_select_reg.fields.cmask = 0;
			
			wrmsrl(MSR_P6_PERFCTR0, 0);
			wrmsrl(MSR_P6_EVNTSEL0 + j, event_select_reg.value);
		}

		// start counting, enable all (2 programmable + 3 fixed) counters
		//regs = (1ULL << 0) + (1ULL << 1) + (1ULL << 32) + (1ULL << 33) + (1ULL << 34); // for 2 programmable counters
		regs = (1ULL << 0) + (1ULL << 32) + (1ULL << 33) + (1ULL << 34); // for 1 programmable counter
		wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, regs);
	}
}

void rk_pmc_cleanup() {}

void get_pmc_info(struct pmc_counter *pmc)
{
	rdmsrl(MSR_CORE_PERF_FIXED_CTR0, pmc->inst_retired_any);
	rdmsrl(MSR_CORE_PERF_FIXED_CTR1, pmc->cpu_clk_unhalted);
	//rdmsrl(MSR_CORE_PERF_FIXED_CTR2, pmc->cpu_clk_unhalted_ref);
	//rdmsrl(MSR_CORE_PERF_FIXED_CTR2, pmc->cpu_clk_unhalted);

	rdmsrl(MSR_P6_PERFCTR0 + 0, pmc->l3_miss);

	rdmsrl(MSR_IA32_TSC, pmc->invariant_tsc);
}

#else // Other cases: Use PERF_EVENT

#include <linux/perf_event.h>
struct perf_event *rk_perf_event[RK_MAX_CPUS][3] = {{NULL,}};

#ifdef RK_ARM_iMX6
#include <asm/hardware/cache-l2x0.h>
#include <asm/io.h>
// From <arch/arm/plat-mxc/include/mach/mx6.h>
#define ARM_PERIPHBASE                  0x00A00000
#define PERIPBASE_VIRT                  0xF2000000
#define L2_BASE_ADDR                    (ARM_PERIPHBASE + 0x2000)
#define IO_ADDRESS(x) (void __force __iomem *)((x) + PERIPBASE_VIRT)
#endif

void __rk_pmc_init(void) 
{
        struct perf_event_attr pmc_attr = { 
                .type = PERF_TYPE_HARDWARE,
                .pinned = 1,
                .disabled = 0,
        };
        cpumask_t cpumask;
	int i = 0;

	for (i = 0; i < num_cpus; i++) {
		cpus_clear(cpumask);
		cpu_set(i, cpumask);
		set_cpus_allowed_ptr(current, &cpumask);

#ifdef RK_ARM_iMX6
		// iMX.6 Patch: set SUNIDEN and SUIDEN bit on Secure Debug Enable Register
		// (https://community.freescale.com/thread/302685)
		{
			u32 val = 0b11;
			asm volatile("mcr p15, 0, %0, c1, c1, 1" : : "r" (val));
		}
		// Instructions
		//pmc_attr.type = PERF_TYPE_RAW;
		//pmc_attr.config = 0x68; // ARMV7_PERFCTR_INST_OUT_OF_RENAME_STAGE;
		pmc_attr.type = PERF_TYPE_HARDWARE,
		pmc_attr.config = PERF_COUNT_HW_INSTRUCTIONS;
		rk_perf_event[i][0] = perf_event_create_kernel_counter(&pmc_attr, i, NULL, NULL);
		if (rk_perf_event[i][0]) {
			rk_perf_event[i][0]->pmu->stop(rk_perf_event[i][0], 0);
			rk_perf_event[i][0]->pmu->start(rk_perf_event[i][0], 0);
		}
		// CPU Cycles
		pmc_attr.type = PERF_TYPE_HARDWARE;
		pmc_attr.config = PERF_COUNT_HW_CPU_CYCLES;
		rk_perf_event[i][1] = perf_event_create_kernel_counter(&pmc_attr, i, NULL, NULL);
		if (rk_perf_event[i][1]) {
			rk_perf_event[i][1]->pmu->stop(rk_perf_event[i][1], 0);
			rk_perf_event[i][1]->pmu->start(rk_perf_event[i][1], 0);
		}
		// Cache Miss (L1)
		pmc_attr.type = PERF_TYPE_HARDWARE;
		pmc_attr.config = PERF_COUNT_HW_CACHE_MISSES;
		rk_perf_event[i][2] = perf_event_create_kernel_counter(&pmc_attr, i, NULL, NULL);
		if (rk_perf_event[i][2]) {
			rk_perf_event[i][2]->pmu->stop(rk_perf_event[i][2], 0);
			rk_perf_event[i][2]->pmu->start(rk_perf_event[i][2], 0);
		}

		// L310 setup
		{                        
			u32 val;
			val = 0xf;
			writel(val, IO_ADDRESS(L2_BASE_ADDR + L2X0_EVENT_CNT1_CFG)); // DRREQ
			val = 0xb;
			writel(val, IO_ADDRESS(L2_BASE_ADDR + L2X0_EVENT_CNT0_CFG)); // DRHIT
			val = 0x3;
			writel(val, IO_ADDRESS(L2_BASE_ADDR + L2X0_EVENT_CNT_CTRL));

			val = readl(IO_ADDRESS(L2_BASE_ADDR + L2X0_DEBUG_CTRL));
			printk("L2X0_DEBUG_CTRL: %x\n", val);
		}
#else
		// Instructions
		pmc_attr.config = PERF_COUNT_HW_INSTRUCTIONS;
		rk_perf_event[i][0] = perf_event_create_kernel_counter(&pmc_attr, i, NULL, NULL, NULL);
		if (rk_perf_event[i][0]) {
			rk_perf_event[i][0]->pmu->stop(rk_perf_event[i][0], 0);
			rk_perf_event[i][0]->pmu->start(rk_perf_event[i][0], 0);
		}
		// CPU Cycles
		pmc_attr.config = PERF_COUNT_HW_CPU_CYCLES;
		rk_perf_event[i][1] = perf_event_create_kernel_counter(&pmc_attr, i, NULL, NULL, NULL);
		if (rk_perf_event[i][1]) {
			rk_perf_event[i][1]->pmu->stop(rk_perf_event[i][1], 0);
			rk_perf_event[i][1]->pmu->start(rk_perf_event[i][1], 0);
		}
		// LLC Miss 
		pmc_attr.config = PERF_COUNT_HW_CACHE_MISSES;
		rk_perf_event[i][2] = perf_event_create_kernel_counter(&pmc_attr, i, NULL, NULL, NULL);
		if (rk_perf_event[i][2]) {
			rk_perf_event[i][2]->pmu->stop(rk_perf_event[i][2], 0);
			rk_perf_event[i][2]->pmu->start(rk_perf_event[i][2], 0);
		}
#endif
        }
}
void rk_pmc_cleanup(void) 
{
	int i = 0;
	for (i = 0; i < num_cpus; i++) {
		if (rk_perf_event[i][0]) perf_event_release_kernel(rk_perf_event[i][0]);
		if (rk_perf_event[i][1]) perf_event_release_kernel(rk_perf_event[i][1]);
		if (rk_perf_event[i][2]) perf_event_release_kernel(rk_perf_event[i][2]);
	}	
}
void get_pmc_info(struct pmc_counter *pmc) 
{
	/*
	memset(pmc, 0, sizeof(struct pmc_counter));
	rk_rdtsc(&pmc->invariant_tsc);
	pmc->cpu_clk_unhalted = pmc->invariant_tsc;
	*/
	int cpuid = raw_smp_processor_id();
	
	if (rk_perf_event[cpuid][0]) {
		rk_perf_event[cpuid][0]->pmu->read(rk_perf_event[cpuid][0]);
		pmc->inst_retired_any = local64_read(&rk_perf_event[cpuid][0]->count);
	}

	if (rk_perf_event[cpuid][1]) {
		rk_perf_event[cpuid][1]->pmu->read(rk_perf_event[cpuid][1]);
		pmc->cpu_clk_unhalted = local64_read(&rk_perf_event[cpuid][1]->count);
		pmc->invariant_tsc = pmc->cpu_clk_unhalted;
	}

#ifdef RK_ARM_iMX6
	if (rk_perf_event[cpuid][2]) {
		rk_perf_event[cpuid][2]->pmu->read(rk_perf_event[cpuid][2]);
		pmc->l2_hit = local64_read(&rk_perf_event[cpuid][2]->count);
	}
	{
		u32 l2_ref, l2_hit;
		/*
		u32 val = 0x3;
		writel(val, IO_ADDRESS(L2_BASE_ADDR + L2X0_EVENT_CNT1_CFG)); // DRREQ
		writel(val, IO_ADDRESS(L2_BASE_ADDR + L2X0_EVENT_CNT0_CFG)); // DRHIT
		*/
		l2_ref = readl(IO_ADDRESS(L2_BASE_ADDR + L2X0_EVENT_CNT1_VAL));
		l2_hit = readl(IO_ADDRESS(L2_BASE_ADDR + L2X0_EVENT_CNT0_VAL));
		pmc->l3_hit = l2_hit;
		pmc->l3_miss = l2_ref - l2_hit;
		/*
		val = 0xf;
		writel(val, IO_ADDRESS(L2_BASE_ADDR + L2X0_EVENT_CNT1_CFG)); // DRREQ
		val = 0xb;
		writel(val, IO_ADDRESS(L2_BASE_ADDR + L2X0_EVENT_CNT0_CFG)); // DRHIT
		*/
	}
#else
	if (rk_perf_event[cpuid][2]) {
		rk_perf_event[cpuid][2]->pmu->read(rk_perf_event[cpuid][2]);
		pmc->l3_miss = local64_read(&rk_perf_event[cpuid][2]->count);
	}
#endif
}

#endif  

void get_pmc_info_all_core(struct pmc_counter *pmc)
{
	cpumask_t cpumask;
	int i;
	for (i = 0; i < num_cpus; i++) {
		// Set CPU affinity of task
		cpus_clear(cpumask);
		cpu_set(i, cpumask);
		set_cpus_allowed_ptr(current, &cpumask);

		get_pmc_info(&pmc[i]);
	}
}

#else // RK_PROFILE_PMC

void __rk_pmc_init(void) {}
void rk_pmc_cleanup() {}

#endif // RK_PROFILE_PMC

void rk_pmc_init()
{
	// Do no use PMC when running in a virtual machine
	if (is_virtualized) return;
	__rk_pmc_init();
}


/**************************************************************
 *
 * RK systemcall for testing
 *
 **************************************************************/
void sys_rt_suspend_until_rsv_start(void)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
}

int debug_rd = -1;
asmlinkage int sys_rk_testing(int index, int nr, void *data)
{	
	int ret = RK_ERROR;
	switch (index) {
	case 0x100:
		break;
	case 0x101:
		break;
	case 0x102:
		sys_rt_suspend_until_rsv_start();
		ret = RK_SUCCESS;
		break;
	case 0x103:
		debug_rd = nr;
		ret = RK_SUCCESS;
		break;

#ifdef CONFIG_RK_MEM
	case 0x200:
		ret = sys_rk_mem_reserve_show_reserved_pages(nr);
		break;
	case 0x201:
		ret = sys_rk_mem_reserve_show_task_vminfo(nr);
		break;
	case 0x202:
		ret = sys_rk_mem_reserve_do_alloc_test(nr);
		break;
	case 0x203:
		ret = sys_rk_mem_reserve_swap_out_page(nr, data);
		break;
	case 0x204:
		ret = sys_rk_mem_reserve_show_color_info(nr);
		break;
	case 0x205: 
		ret = sys_rk_mem_reserve_traverse_page_table(nr);
		break;
#endif
	}
	return ret;
}


