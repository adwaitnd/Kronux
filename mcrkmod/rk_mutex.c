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
 * rk_mutex.c: RK mutex with real-time synchronization protocols
 */

#include <linux/slab.h>
#include <rk/rk_mc.h>
#include <rk/rk_mutex.h>
#include <rk/rk_virt.h>

//#define VERBOSE_RK_MUTEX
#ifdef VERBOSE_RK_MUTEX
	#define rkmtx_dbg(...) printk(__VA_ARGS__)
#else
	#define rkmtx_dbg(...)
#endif

// Save critical section enter/exit info
#define CRITICAL_SECTION_INFO
#ifdef CRITICAL_SECTION_INFO
	#define rkmtx_trace_fn(a,b)	rk_trace_fn(a,b)
	#define rkmtx_vm_event(a,b)	rk_send_vm_event(a,b)
	#define rkmtx_event(a,b,c,d,e)	rk_event_log_save(a,b,c,d,e)
#else
	#define rkmtx_trace_fn(a,b)
	#define rkmtx_vm_event(a,b)
	#define rkmtx_event(a,b,c,d,e)
#endif

rk_mutex_t *rk_mutex_desc = NULL;
LIST_HEAD(online_mutex_head); 
raw_spinlock_t mutex_desc_lock;
DEFINE_PER_CPU(int, vcpu_gcs_count);

#ifdef RK_VIRT_SUPPORT
rk_intervm_mutex_t *rk_intervm_mutex_desc = NULL;
raw_spinlock_t intervm_mutex_desc_lock;
#endif

#define TYPE_NAME_LENGTH 20
static char type_names[__NR_RK_MUTEX_TYPES][TYPE_NAME_LENGTH] = {
	[RK_MUTEX_PIP]   	 = "pip",
	[RK_MUTEX_PCP]   	 = "pcp",
	[RK_MUTEX_HLP]   	 = "hlp",
	[RK_MUTEX_MPCP]  	 = "mpcp",
	[RK_MUTEX_VMPCP] 	 = "vmpcp",
};

int rk_mutex_open(int type, int key, int mode);
int rk_mutex_destroy(int type, int key, int kill_waiters);
int rk_mutex_lock(int type, int mid, int is_trylock);
int rk_mutex_unlock(int type, int mid);
int __rk_mutex_unlock(int type, int mid, struct task_struct *owner);
int rk_intervm_mutex_unlock_all(struct task_struct *task);
int rk_intervm_mutex_remove_from_waitlist(int mid, struct task_struct *task); 

void rk_mutex_init(void)
{
	int i;

	// create an array for mutex keys
	rk_mutex_desc = kmalloc(sizeof(rk_mutex_t) * MAX_RK_MUTEX_DESC, GFP_ATOMIC);
	if (!rk_mutex_desc) {
		printk("Error: Could not allocate memory for rk mutex keys\n");
		return;
	}
	for (i = 0; i < MAX_RK_MUTEX_DESC; i++) {
		rk_mutex_desc[i] = NULL;
	}
	for_each_online_cpu(i) {
		per_cpu(vcpu_gcs_count, i) = 0;
	}
	
	// initialize the heads of online mutexes
	INIT_LIST_HEAD(&online_mutex_head);

	raw_spin_lock_init(&mutex_desc_lock);
	
#ifdef RK_VIRT_SUPPORT
	// create an array for intervm mutex keys (host only)
	if (is_virtualized == TRUE) return;
	rk_intervm_mutex_desc = kmalloc(sizeof(rk_intervm_mutex_t) * MAX_RK_INTERVM_MUTEX_DESC, GFP_ATOMIC);
	if (!rk_mutex_desc) {
		printk("Error: Could not allocate memory for rk intervm mutex keys\n");
		return;
	}
	for (i = 0; i < MAX_RK_INTERVM_MUTEX_DESC; i++) {
		rk_intervm_mutex_desc[i] = NULL;
	}
	raw_spin_lock_init(&intervm_mutex_desc_lock);
#endif
}

void rk_mutex_cleanup(void)
{
	int i;

	rkmtx_dbg("rk_mutex_cleanup\n");
	if (rk_mutex_desc) {
		for (i = 0; i < MAX_RK_MUTEX_DESC; i++) {
			rk_mutex_t mutex = rk_mutex_desc[i];
			if (!mutex) continue;
			rk_mutex_destroy(mutex->type, mutex->key, TRUE);
		}
		kfree(rk_mutex_desc);
		rk_mutex_desc = NULL;
	}
#ifdef RK_VIRT_SUPPORT
	if (rk_intervm_mutex_desc) {
		for (i = 0; i < MAX_RK_INTERVM_MUTEX_DESC; i++) {
			rk_intervm_mutex_t mutex = rk_intervm_mutex_desc[i];
			if (!mutex) continue;
			rk_intervm_mutex_destroy_handler(mutex->type, mutex->key);
		}
		kfree(rk_intervm_mutex_desc);
		rk_intervm_mutex_desc = NULL;
	}
#endif
}


/////////////////////////////////////////////////////////////////////////////
//
// RK Mutex procfs
//
/////////////////////////////////////////////////////////////////////////////

#define RK_MUTEX_PROCFS_BUF_SIZE 1024
#define RK_MUTEX_PROCFS_BUF_THRESHOLD 950
char rk_mutex_procfs_buf[RK_MUTEX_PROCFS_BUF_SIZE];
int rk_mutex_read_proc(int mid, char *buf)
{
	unsigned long flags;
	rk_mutex_t mutex;
	struct task_struct *task;
	char *p;

	p = buf;
	if (mid < 0 || mid >= MAX_RK_MUTEX_DESC || !rk_mutex_desc) {
		p += sprintf(p, "N/A\n");
		return (p - buf);
	}

	raw_spin_lock_irqsave(&mutex_desc_lock, flags);
	mutex = rk_mutex_desc[mid];
	if (mutex == NULL) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		p += sprintf(p, "N/A\n");
		return (p - buf);
	}
	raw_spin_lock(&mutex->lock);
	raw_spin_unlock(&mutex_desc_lock);

	p = rk_mutex_procfs_buf;
	p += sprintf(p, "type    : %s\n", type_names[mutex->type]);
	p += sprintf(p, "key     : %d\n", mutex->key);
	p += sprintf(p, "ceiling : %d\n", mutex->ceiling);
	p += sprintf(p, "count   : %d\n", mutex->count);
	if (mutex->owner) {
		p += sprintf(p, "owner   : %d\n", mutex->owner->pid);
	}
	else {
		p += sprintf(p, "owner   : --\n");
	}
	if (list_empty(&mutex->wait_list)) {
		p += sprintf(p, "waitlist: --\n");
	}
	else {
		p += sprintf(p, "waitlist:");
		list_for_each_entry(task, &mutex->wait_list, rk_mutex_wait_link) {
			p += sprintf(p, " %d(pr:%d)", task->pid, task->rt_priority);
			if ((int)(p - rk_mutex_procfs_buf) > RK_MUTEX_PROCFS_BUF_THRESHOLD) break;
		}
		p += sprintf(p, "\n");
	}
	raw_spin_unlock_irqrestore(&mutex->lock, flags);
	sprintf(buf, "%s", rk_mutex_procfs_buf);
	return (p - rk_mutex_procfs_buf);
}


/////////////////////////////////////////////////////////////////////////////
//
// RK Mutex: system calls and helper functions 
//
/////////////////////////////////////////////////////////////////////////////

static inline int __rk_mutex_operation(int protocol, int cmd, int key, int mode)
{
	switch (cmd) {
	case RK_MUTEX_OPEN:
		return rk_mutex_open(protocol, key, mode);
	case RK_MUTEX_DESTROY:
		return rk_mutex_destroy(protocol, key, FALSE);
	case RK_MUTEX_LOCK: 
		return rk_mutex_lock(protocol, key, FALSE);
	case RK_MUTEX_TRYLOCK:
		return rk_mutex_lock(protocol, key, TRUE);
	case RK_MUTEX_UNLOCK:
		return rk_mutex_unlock(protocol, key);
	}
	return RK_ERROR;
}

// RK mutex system calls
int sys_rk_pip_mutex(int cmd, int key, int mode)
{
	return __rk_mutex_operation(RK_MUTEX_PIP, cmd, key, mode); 
}
int sys_rk_pcp_mutex(int cmd, int key, int mode)
{
	return __rk_mutex_operation(RK_MUTEX_PCP, cmd, key, mode); 
}
int sys_rk_hlp_mutex(int cmd, int key, int mode)
{
	return __rk_mutex_operation(RK_MUTEX_HLP, cmd, key, mode); 
}
int sys_rk_mpcp_mutex(int cmd, int key, int mode)
{
	return __rk_mutex_operation(RK_MUTEX_MPCP, cmd, key, mode); 
}
int sys_rk_vmpcp_mutex(int cmd, int key, int mode)
{
	return __rk_mutex_operation(RK_MUTEX_VMPCP, cmd, key, mode); 
}

// rk_mutex_task_exit
// 
// Called by 
// - rk_task_cleanup
void rk_mutex_task_cleanup(struct task_struct *task)
{
	unsigned long flags;
	rk_mutex_t mutex;
	int mid;
	
	if (!task) return;
	if (!task->rk_mutex_inherited_prio_list) return;
	rkmtx_dbg("info: mutex_task_exit (pid %d)\n", task->pid);

	// Unlock mutexes owned by the task
	while (!list_empty(&task->rk_mutex_list)) {
		mutex = list_entry(task->rk_mutex_list.next, struct rk_mutex, owner_link);
		__rk_mutex_unlock(mutex->type, mutex->mid, task);
	}
	
	// Remove the task from the wait list of mutex mid
	mid = task->rk_mutex_wait_on;
	if (mid >= 0 && mid < MAX_RK_MUTEX_DESC && rk_mutex_desc) {
		raw_spin_lock_irqsave(&mutex_desc_lock, flags);
		mutex = rk_mutex_desc[mid];
		if (mutex) {
			raw_spin_lock(&mutex->lock);
			task->rk_mutex_wait_on = -1;
			list_del_init(&task->rk_mutex_wait_link);
			raw_spin_unlock(&mutex->lock);
		}
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
	}
	if (is_virtualized) {
		// Unlock inter-vm mutexes owned by the task
		rk_intervm_mutex_unlock_all(task);
		// Remove the task from the wait list of inter-vm mutex
		if (mid > RK_INTERVM_MUTEX_WAIT_ON_OFFSET) {
			mid -= RK_INTERVM_MUTEX_WAIT_ON_OFFSET;
			rk_intervm_mutex_remove_from_waitlist(mid, task);
		}
	}
	else {
		// Unlock inter-vm mutexes owned by guest tasks running on this vcpu
		rk_intervm_mutex_unlock_all_handler(0, task);
		// Remove the tasks of this vcpu from the wait list of inter-vm mutex
		rk_intervm_mutex_remove_from_waitlist_handler(-1, 0, task);
	}	
	
	// Free inherited priority list
	if (task->rk_mutex_inherited_prio_list) {
		short *ptr = task->rk_mutex_inherited_prio_list;
		task->rk_mutex_inherited_prio_list = NULL;
		kfree(ptr);
	}
}

// Change task rt_priority to its current inherited prio
//
// Called by (need_indirect = FALSE)
// - rk_mutex_destroy (via rk_mutex_restore_priority)
// - rk_mutex_lock 
// - __rk_mutex_unlock
// - __rk_mutex_unlock (via rk_mutex_restore_priority)
// - __rk_vmpcp_finish_gcs_handler (via rk_mutex_restore_priority)
// - rk_intervm_mutex_lock
// - rk_intervm_mutex_destroy_handler (via rk_mutex_restore_priority)
// - __rk_vmpcp_finish_gcs_handler (via rk_mutex_restore_priority)
// - __rk_vmpcp_start_gcs_handler
// - __rk_intervm_mutex_lock_handler
// - rk_intervm_mutex_unlock_handler
// - rk_intervm_mutex_unlock_handler (via rk_mutex_restore_priority)
void rk_mutex_change_task_priority(struct task_struct *task, int need_indirect)
{
	if (!task) return;
	if (task->rt_priority == task_inherited_prio(task)) return;
	
	rkmtx_dbg("info: change priority (pid %d prio %d -> %d)\n", task->pid, task->rt_priority, task_inherited_prio(task));
	
	// needs to change priority
	if (need_indirect == FALSE) {
		struct sched_param par;
		int policy;
		if (task->rk_resource_set) {
			par.sched_priority = task_inherited_prio(task);
			policy = cpu_reserves_kernel_scheduling_policy;
		}
		else {
			par.sched_priority = task->orig_sched_prio;
			policy = task->orig_sched_policy;
		}
		sched_setscheduler_nocheck(task, policy, &par);
	}
	else {
		rk_resource_set_t rset = task->rk_resource_set;
		if (rset) {
			unsigned long flags;
			int cpunum = -1;
			raw_spin_lock_irqsave(&rset->lock, flags);
			if (rk_check_task_cpursv(task) == RK_SUCCESS) {
				cpunum = __rk_get_task_default_cpursv_cpunum(task);
				// Push to rk workqueue (sched_setscheduler cannot be called in ISR)
				rk_push_to_workqueue(cpunum, RK_WORK_MUTEX, task, 
					(void*)(long)cpunum, (void*)(long)task_inherited_prio(task));
			}
			raw_spin_unlock_irqrestore(&rset->lock, flags);
			if (cpunum >= 0) wake_up_process(per_cpu(rk_worker, cpunum));
		}
	}
}

// rk_mutex_restore_priority
// - mutex->lock must be released before calling this function
// 
// Called by (need_indirect = FALSE)
// - rk_mutex_destroy
// - __rk_mutex_unlock
// - __rk_vmpcp_finish_gcs_handler
// - rk_intervm_mutex_destroy_handler
// - rk_intervm_mutex_unlock_handler
void rk_mutex_restore_priority(struct task_struct *task, int need_indirect)
{
	if (!task) return;
	if (!task->rk_mutex_inherited_prio_list) return;
	if (task->rk_mutex_nested_level <= 0) {
		printk("WARNING: rk_mutex_restore_priority: no priority to restore (pid %d)\n", task->pid);
		return;
	}

	--task->rk_mutex_nested_level;
	rk_mutex_change_task_priority(task, need_indirect);
}


#ifdef RK_VIRT_SUPPORT
// vMPCP: increase and restore VCPU priority for intra-VM mutex
//
// Called by
// - rk_mutex_lock
// - __rk_mutex_unlock
void rk_mutex_increase_vcpu_priority_from_guest(struct task_struct *task, int mode)
{
	rk_resource_set_t rset;
	unsigned long flags;
	int need_vmpcp_hypercall = FALSE;

	rset = task->rk_resource_set;
	if (rset == NULL) return;

	raw_spin_lock_irqsave(&rset->lock, flags);
	if (rk_check_task_cpursv(task) == RK_SUCCESS) {
		int cpunum = __rk_get_task_default_cpursv_cpunum(task);
		if (per_cpu(vcpu_gcs_count, cpunum)++ == 0) 
			need_vmpcp_hypercall = TRUE;
	}
	raw_spin_unlock_irqrestore(&rset->lock, flags);
	if (need_vmpcp_hypercall) rk_vmpcp_start_gcs(mode);
}

// Called by
// - rk_mutex_destroy
// - __rk_mutex_unlock
void rk_mutex_restore_vcpu_priority_from_guest(struct task_struct *task)
{
	rk_resource_set_t rset;
	unsigned long flags;
	int need_vmpcp_hypercall = FALSE;

	if (task == NULL) return;
	rset = task->rk_resource_set;
	if (rset == NULL) return;

	raw_spin_lock_irqsave(&rset->lock, flags);
	if (rk_check_task_cpursv(task) == RK_SUCCESS) {
		int cpunum = __rk_get_task_default_cpursv_cpunum(task);
		if (--per_cpu(vcpu_gcs_count, cpunum) == 0) 
			need_vmpcp_hypercall = TRUE;
		if (per_cpu(vcpu_gcs_count, cpunum) < 0) 
			per_cpu(vcpu_gcs_count, cpunum) = 0;
	}
	raw_spin_unlock_irqrestore(&rset->lock, flags);
	if (need_vmpcp_hypercall) rk_vmpcp_finish_gcs();
}

// Called by
// - rk_mutex_lock
int rk_mutex_vmpcp_priority_ge(struct task_struct *t1, struct task_struct *t2)
{
	rk_resource_set_t rset;
	unsigned long flags;
	int vcpu_prio[2] = {0,}, task_prio[2] = {0,};
	struct task_struct* tsk[2] = {t1, t2};
	int i;
	
	// check vcpu prio and task prio
	for (i = 0; i < 2; i++) {
		rset = tsk[i]->rk_resource_set;
		if (rset == NULL) {
			task_prio[i] = tsk[i]->rt_priority;
			continue;
		}
		raw_spin_lock_irqsave(&rset->lock, flags);
		if (rk_check_task_cpursv(tsk[i]) == RK_SUCCESS) {
			cpu_reserve_t rsv = __rk_get_task_default_cpursv(tsk[i])->reserve;
			vcpu_prio[i] = rsv->vcpu_priority_index;
			task_prio[i] = rsv->cpu_priority_index;
		}
		raw_spin_unlock_irqrestore(&rset->lock, flags);
	}
	// compare t1 and t2
	if (vcpu_prio[0] > vcpu_prio[1]) return TRUE;
	if (vcpu_prio[0] == vcpu_prio[1] && task_prio[0] >= task_prio[1]) return TRUE;
	return FALSE;
}
#else // RK_VIRT_SUPPORT
static inline void rk_mutex_increase_vcpu_priority_from_guest(struct task_struct *task, int mode) {}
static inline void rk_mutex_restore_vcpu_priority_from_guest(struct task_struct *task) {}
static inline int rk_mutex_vmpcp_priority_ge(struct task_struct *t1, struct task_struct *t2) { return 0; }
#endif // RK_VIRT_SUPPORT


// Allocate rk_mutex_inherited_prio_list for current task
// - This will be freed when the task is cleaned up (rk_mutex_task_cleanup)
//
// Called by
// - rk_mutex_open
// - rk_hypercall_handler (HYP_rk_create_vcpu_inherited_prio_list)
// - rk_intervm_mutex_open
// - rk_intervm_mutex_open_handler
int rk_mutex_create_inherited_prio_list(void)
{
	if (current->rk_mutex_inherited_prio_list) return RK_SUCCESS;

	current->rk_mutex_inherited_prio_list 
		= kmalloc(sizeof(short) * MAX_RK_MUTEX_NESTED_LEVEL, GFP_ATOMIC);
	if (!current->rk_mutex_inherited_prio_list) return RK_ERROR;

	memset(current->rk_mutex_inherited_prio_list, 0, sizeof(short) * MAX_RK_MUTEX_NESTED_LEVEL);
	task_inherited_prio(current) = current->rt_priority;

	return RK_SUCCESS;
}

// rk_mutex_open
// - Opens a mutex associated with the key and returns its id
// - If such a mutex doesn't exist and MTX_CREATE is set, creates a new mutex.
// 
// Note for PCP and HLP
// - The priority ceiling of a mutex is auto-adjusted based on the priorities
//   of tasks opening that mutex.
int rk_mutex_open(int type, int key, int mode)
{
	unsigned long flags;
	rk_mutex_t mutex;
	int mid = MAX_RK_MUTEX_DESC;

	if (!rk_mutex_desc) return RK_ERROR;
	if (key < 0) {
		printk("rk_%s_mutex_open: invalid key (%d)\n", type_names[type], key);
		return RK_ERROR;
	}
	if (rk_check_task_cpursv(current) == RK_ERROR) {
		printk("rk_%s_mutex_open: needs rk cpu reservation\n", type_names[type]);
		return RK_ERROR;
	}
#ifndef RK_GLOBAL_SCHED
	if (type == RK_MUTEX_VMPCP && !is_virtualized) {
		printk("rk_%s_mutex_open: cannot run in a non-virtualized env\n", type_names[type]);
		return RK_ERROR;
	}
#else
	if (type == RK_MUTEX_VMPCP) {
		printk("rk_%s_mutex_open: works under partitioned scheduling\n", type_names[type]);
		return RK_ERROR;
	}
#endif
	// Allocate rk_mutex_inherited_prio_list for current task
	if (!current->rk_mutex_inherited_prio_list) {
		if (rk_mutex_create_inherited_prio_list() == RK_ERROR) {
			printk("rk_%s_mutex_open: failed to allocate memory for prio_list of pid %d\n", 
				type_names[type], current->pid);
			return RK_ERROR;
		}
	}
	// vMPCP: Allocate rk_mutex_inherited_prio_list for vcpu
	if (type == RK_MUTEX_VMPCP) rk_create_vcpu_inherited_prio_list();

	// Find a mutex with the key value
	raw_spin_lock_irqsave(&mutex_desc_lock, flags);
	list_for_each_entry(mutex, &online_mutex_head, online_mutex_link) {
		if (mutex->key == key) {
			mid = mutex->mid;
			break;
		}
	}
	if (mid < MAX_RK_MUTEX_DESC) {
		if (mutex->type != type) {
			raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
			printk("rk_%s_mutex_open: mutex type mismatch\n", type_names[type]);
			return RK_ERROR;
		}
		// Adjust priority ceiling
		raw_spin_lock(&mutex->lock);
		if (mutex->ceiling < current->rt_priority)
			mutex->ceiling = current->rt_priority;
		rkmtx_dbg("info: %s_open (mid %d, key %d, ceil %d, pid %d)\n", 
			type_names[type], mid, mutex->key, mutex->ceiling, current->pid);
		raw_spin_unlock(&mutex->lock);
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		return mid;
	}
	if ((mode & MTX_CREATE) == 0) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		printk("rk_%s_mutex_open: cannot find a mutex with the key %d\n", type_names[type], key);
		return RK_ERROR;
	}

	// Allocate memory and zero it for the new mutex being created 
	mutex = kmalloc(sizeof(struct rk_mutex), GFP_ATOMIC);
	if (!mutex) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		printk("rk_%s_mutex_open: failed to allocate memory for rk mutex\n", type_names[type]);
		return RK_ERROR;
	}
	memset(mutex, 0, sizeof(struct rk_mutex));

	raw_spin_lock_init(&mutex->lock);
	INIT_LIST_HEAD(&mutex->wait_list);
	INIT_LIST_HEAD(&mutex->online_mutex_link);
	INIT_LIST_HEAD(&mutex->owner_link);
	mutex->type = type;
	mutex->mode = (mode & __MTX_MASK);
	mutex->key = key;
	mutex->ceiling = current->rt_priority;

	// Find an empty slot
	for (mid = 0; mid < MAX_RK_MUTEX_DESC; mid++) {
		if (rk_mutex_desc[mid] == NULL) {
			rk_mutex_desc[mid] = mutex;
			mutex->mid = mid;
			break;
		}
	}
	if (mid == MAX_RK_MUTEX_DESC) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		kfree(mutex);
		printk("rk_%s_mutex_open: exceeded the maximum number of rk mutexes (%d)\n", type_names[type], MAX_RK_MUTEX_DESC);
		return RK_ERROR;
	}
	// Add the mutex to the online mutex list
	list_add_tail(&mutex->online_mutex_link, &online_mutex_head);

	rkmtx_dbg("info: %s_open: created (mid %d, key %d, ceil %d, pid %d)\n", 
		type_names[type], mid, mutex->key, mutex->ceiling, current->pid);
	raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);

	rk_procfs_mutex_create(mid);
	return mid;
}

// rk_mutex_destroy
// - Destroys a mutex associated with the key
//
// Called by
// - system call
// - rk_mutex_cleanup
int rk_mutex_destroy(int type, int key, int kill_waiters)
{
	unsigned long flags;
	rk_mutex_t mutex;
	struct task_struct *task, *tmp;
	int mid = MAX_RK_MUTEX_DESC;

	if (!rk_mutex_desc) return RK_ERROR;
	if (key < 0) {
		printk("rk_%s_mutex_destroy: invalid key (%d)\n", type_names[type], key);
		return RK_ERROR;
	}
#ifndef RK_GLOBAL_SCHED
	if (type == RK_MUTEX_VMPCP && !is_virtualized) {
		printk("rk_%s_mutex_destroy: cannot run in a non-virtualized env\n", type_names[type]);
		return RK_ERROR;
	}
#else
	if (type == RK_MUTEX_VMPCP) {
		printk("rk_%s_mutex_destroy: works under partitioned scheduling\n", type_names[type]);
		return RK_ERROR;
	}
#endif
	// Find a mutex with the key 
	raw_spin_lock_irqsave(&mutex_desc_lock, flags);
	list_for_each_entry(mutex, &online_mutex_head, online_mutex_link) {
		if (mutex->key == key) {
			mid = mutex->mid;
			break;
		}
	}
	if (mid == MAX_RK_MUTEX_DESC) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		printk("rk_%s_mutex_destroy: cannot find a mutex with the key %d\n", type_names[type], key);
		return RK_ERROR;
	}
	if (rk_mutex_desc[mid]->type != type) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		printk("rk_%s_mutex_destroy: mutex type mismatch\n", type_names[type]);
		return RK_ERROR;
	}
	// Remove from the online mutex list
	mutex = rk_mutex_desc[mid];
	rk_mutex_desc[mid] = NULL;
	list_del(&mutex->online_mutex_link);
	rkmtx_dbg("info: %s_destroy: remove mutex %d (key %d)\n", type_names[type], mid, mutex->key);

	raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);

	rk_procfs_mutex_destroy(mid);

	// Wait for any task touching the mutex
	raw_spin_unlock_wait(&mutex->lock);

	// Restore the priority of the owner task
	if (type == RK_MUTEX_VMPCP) {
		rk_mutex_restore_vcpu_priority_from_guest(mutex->owner);
	}
	rk_mutex_restore_priority(mutex->owner, FALSE);

	list_del(&mutex->owner_link);

	// Wake up (or kill) all tasks waiting on this mutex
	list_for_each_entry_safe(task, tmp, &mutex->wait_list, rk_mutex_wait_link) {
		task->rk_mutex_wait_on = -1;
		list_del_init(&task->rk_mutex_wait_link);
		if (kill_waiters) {
			struct siginfo sig;
			memset(&sig, 0, sizeof(sig));
			sig.si_signo = SIGKILL;
			sig.si_code  = SI_KERNEL;
			do_send_sig_info(SIGKILL, &sig, task, false);
			printk("RK: kill pid %d waiting on rk_mutex id %d\n", task->pid, mid);
		}
		else {
			rkmtx_dbg("info: %s_destroy: waiking up %d\n", type_names[type], task->pid);
			wake_up_process(task);
		}
	}
	kfree(mutex);
	return RK_SUCCESS;
}

static void do_immediate_priority_ceiling(int type, int local_ceiling, struct task_struct *task)
{
	if (type == RK_MUTEX_HLP && local_ceiling > task_inherited_prio(task)) {
		task_inherited_prio(task) = local_ceiling;
	}
	else if ((type == RK_MUTEX_MPCP || type == RK_MUTEX_VMPCP) 
		 && rk_global_ceiling(task) > task_inherited_prio(task)) {

		task_inherited_prio(task) = rk_global_ceiling(task);
		if (task_inherited_prio(task) >= MAX_LINUXRK_PRIORITY) {
			task_inherited_prio(task) = MAX_LINUXRK_PRIORITY - 1;
		}
	}
}

static inline int needs_immediate_prio_ceiling(int type)
{
	if (type == RK_MUTEX_HLP || type == RK_MUTEX_MPCP || type == RK_MUTEX_VMPCP) 
		return TRUE;
	return FALSE;
}

// rk_mutex_lock
int rk_mutex_lock(int type, int mid, int is_trylock)
{
	unsigned long flags;
	rk_mutex_t mutex;
	struct task_struct *tmp;
	int highest_locked_ceiling;
	struct task_struct *blocking_task;
	int try_count = 1;

	if (mid < 0 || mid >= MAX_RK_MUTEX_DESC) {
		printk("rk_%s_mutex_lock: invalid mutex id (%d)\n", type_names[type], mid);
		return RK_ERROR;
	}
	if (current->rk_mutex_nested_level >= MAX_RK_MUTEX_NESTED_LEVEL) {
		printk("rk_%s_mutex_lock: current task has reached the maximum nested level (%d)\n", type_names[type], MAX_RK_MUTEX_NESTED_LEVEL);
		return RK_ERROR;
	}
#ifndef RK_GLOBAL_SCHED
	if (type == RK_MUTEX_VMPCP && !is_virtualized) {
		printk("rk_%s_mutex_lock: cannot run in a non-virtualized env\n", type_names[type]);
		return RK_ERROR;
	}
#else
	if (type == RK_MUTEX_VMPCP) {
		printk("rk_%s_mutex_lock: works under partitioned scheduling\n", type_names[type]);
		return RK_ERROR;
	}
#endif

retry_locking:
	// Check if rk mutex is available (need to check whenever retrying)
	if (!rk_mutex_desc) return RK_ERROR;
	// Should be checked here to handle the task-exit case
	if (!current->rk_mutex_inherited_prio_list) { 
		printk("rk_%s_mutex_lock: needs to open mutex first\n", type_names[type]);
		return RK_ERROR;
	}

	highest_locked_ceiling = -1;
	blocking_task = NULL;

	raw_spin_lock_irqsave(&mutex_desc_lock, flags);
	mutex = rk_mutex_desc[mid];
	if (mutex == NULL) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		printk("rk_%s_mutex_lock: mutex %d does not exist\n", type_names[type], mid);
		return RK_ERROR;
	}
	if (mutex->type != type) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		printk("rk_%s_mutex_lock: mutex type mismatch\n", type_names[type]);
		return RK_ERROR;
	}
	if (rk_check_task_cpursv(current) == RK_ERROR) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		printk("rk_%s_mutex_lock: needs rk cpu reservation\n", type_names[type]);
		return RK_ERROR;
	}
	// PCP: get the highest priority ceiling of any mutex locked by other tasks
	//      (highest_locked_ceiling)
	if (type == RK_MUTEX_PCP) {
		rk_mutex_t tmpmtx;
		list_for_each_entry(tmpmtx, &online_mutex_head, online_mutex_link) {
			if (tmpmtx == mutex) continue;
			raw_spin_lock(&tmpmtx->lock);
#ifndef RK_GLOBAL_SCHED
			// Partitioned scheduling
			// (don't need a rset->lock here)
			if (tmpmtx->owner && tmpmtx->ceiling > highest_locked_ceiling
			    && rk_get_task_current_cpursv_cpunum(tmpmtx->owner) == raw_smp_processor_id()) {
#else
			// Global scheduling
			if (tmpmtx->owner && tmpmtx->ceiling > highest_locked_ceiling) {
#endif
				highest_locked_ceiling = tmpmtx->ceiling;
				blocking_task = tmpmtx->owner;
			}
			raw_spin_unlock(&tmpmtx->lock);
		}
	}
	raw_spin_lock(&mutex->lock);
	raw_spin_unlock(&mutex_desc_lock);

	// Ignore if the current task is waiting on the mutex
	if (!list_empty(&current->rk_mutex_wait_link)) {
		rkmtx_dbg("info: %s_lock: pid %d woken up, but continues to wait (mid %d, key %d)\n", 
			type_names[type], current->pid, mutex->mid, mutex->key);
		raw_spin_unlock_irqrestore(&mutex->lock, flags);
		goto wait_on_mutex;
	}

	// Mutex can be locked by the current task
	// - if the current task is already the owner of the mutex, or
	// - if the mutex is not locked by any task
	// - PCP: the current task's priority is higher than the priority 
	//        ceilings of all mutexes locked by other tasks
	// - Other protocols: highest_locked_ceiling == -1, so no effect.
	if ((!mutex->owner && (int)current->rt_priority > highest_locked_ceiling) // need casting
	    || mutex->owner == current) {
		int mode = mutex->mode;

		// First time to lock this mutex
		// - PIP, PCP: Increase nested level if mutex->count == 0
		// - HLP, MPCP, vMPCP: Immediate priority ceiling protocols 
		//     - Increase nested level only if mutex->count == 0 & try_count == 1
		//     - In other cases, nested level has been already increased by prev owner
		if (mutex->count == 0 && (needs_immediate_prio_ceiling(type) == FALSE || try_count == 1)) { 
			task_inherited_prio(current) = current->rt_priority;
			current->rk_mutex_nested_level++;
			task_inherited_prio(current) = current->rt_priority;
		}
		mutex->owner = current;
		mutex->count++;
		if (list_empty(&mutex->owner_link)) {
			list_add_tail(&mutex->owner_link, &current->rk_mutex_list);
		}
		// HLP, MPCP, vMPCP: immediate priority ceiling 
		do_immediate_priority_ceiling(type, mutex->ceiling, current);

		raw_spin_unlock_irqrestore(&mutex->lock, flags);
		rkmtx_trace_fn(RK_TRACE_TYPE_CS, true);
		if (type == RK_MUTEX_VMPCP) {
			rkmtx_vm_event(RK_EVENT_TYPE_VM_TASK_ENTER_CS, current->pid);
			if (try_count == 1) rk_mutex_increase_vcpu_priority_from_guest(current, mode);
		}
		if (current->rt_priority < task_inherited_prio(current)) {
			rkmtx_dbg("info: %s_lock: immediate prio ceiling (mid %d, key %d, pid %d prio %d -> %d)\n", 
				type_names[type], mutex->mid, mutex->key,
				current->pid, current->rt_priority, task_inherited_prio(current)); 
			rk_mutex_change_task_priority(current, FALSE);
		}

		rkmtx_dbg("info: %s_lock: acquire mutex (mid %d, key %d, pid %d)\n", 
			type_names[type], mutex->mid, mutex->key, current->pid);
		return RK_SUCCESS;
	}
	
	// Mutex cannot be locked by the current task
	// -> trylock
	if (is_trylock == TRUE) {
		raw_spin_unlock_irqrestore(&mutex->lock, flags);
		return RK_ERROR;
	}

	// Mutex cannot be locked by the current task
	// -> Add the current task to the wait_list
	list_for_each_entry(tmp, &mutex->wait_list, rk_mutex_wait_link) {
		if (type != RK_MUTEX_VMPCP && tmp->rt_priority >= current->rt_priority) continue;
		else if (type == RK_MUTEX_VMPCP && rk_mutex_vmpcp_priority_ge(tmp, current)) continue;
		list_add(&current->rk_mutex_wait_link, tmp->rk_mutex_wait_link.prev);
		break;
	}
	if (list_empty(&current->rk_mutex_wait_link)) {
		list_add_tail(&current->rk_mutex_wait_link, &mutex->wait_list);
	}
	current->rk_mutex_wait_on = mid;
	
	// Priority inheritance: check priority
	// - Note for PCP: if the mutex has an owner, the owner has a higher 
	//   priority than the ceilings of any mutexes currently locked. 
	//   So, the owner is the one actually blocking the current task.
	if (mutex->owner) blocking_task = mutex->owner;

	tmp = list_entry(mutex->wait_list.next, struct task_struct, rk_mutex_wait_link);
	if (blocking_task && tmp->rt_priority > blocking_task->rt_priority) {
		task_inherited_prio(blocking_task) = tmp->rt_priority;
	}

	raw_spin_unlock_irqrestore(&mutex->lock, flags);
	rkmtx_dbg("info: %s_lock: added to wait list (mid %d, key %d, pid %d)\n", 
		type_names[type], mutex->mid, mutex->key, current->pid);
	
	// Priority inheritance: change priority
	if (blocking_task && blocking_task->rt_priority < task_inherited_prio(blocking_task)) {
		rkmtx_dbg("info: %s_lock: priority inheritance (pid %d prio %d -> %d)\n", 
			type_names[type], blocking_task->pid, 
			blocking_task->rt_priority, task_inherited_prio(blocking_task));
		rk_mutex_change_task_priority(blocking_task, FALSE);
	}
	
wait_on_mutex:
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
	try_count++;
	goto retry_locking;
}

// __rk_mutex_unlock
//
// Called by 
// - system call (rk_mutex_unlock)
// - rk_mutex_task_cleanup
int __rk_mutex_unlock(int type, int mid, struct task_struct *owner)
{
	unsigned long flags;
	rk_mutex_t mutex;
	struct task_struct *task;
	int mode, local_ceiling;

	if (!rk_mutex_desc) return RK_ERROR;
	if (mid < 0 || mid >= MAX_RK_MUTEX_DESC) {
		printk("rk_%s_mutex_unlock: invalid mutex id (%d)\n", type_names[type], mid);
		return RK_ERROR;
	}
#ifndef RK_GLOBAL_SCHED
	if (type == RK_MUTEX_VMPCP && !is_virtualized) {
		printk("rk_%s_mutex_unlock: cannot run in a non-virtualized env\n", type_names[type]);
		return RK_ERROR;
	}
#else
	if (type == RK_MUTEX_VMPCP) {
		printk("rk_%s_mutex_unlock: works under partitioned scheduling\n", type_names[type]);
		return RK_ERROR;
	}
#endif

	raw_spin_lock_irqsave(&mutex_desc_lock, flags);
	mutex = rk_mutex_desc[mid];
	if (mutex == NULL) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		printk("rk_%s_mutex_unlock: mutex %d does not exist\n", type_names[type], mid);
		return RK_ERROR;
	}
	if (mutex->type != type) {
		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		printk("rk_%s_mutex_unlock: mutex type mismatch\n", type_names[type]);
		return RK_ERROR;
	}
	raw_spin_lock(&mutex->lock);
	raw_spin_unlock(&mutex_desc_lock);

	// Mutex is not locked by owner task
	if (mutex->owner != owner) {
		raw_spin_unlock_irqrestore(&mutex->lock, flags);
		return RK_ERROR;
	}
	// Mutex is still held by the owner task
	if (--mutex->count > 0) {
		raw_spin_unlock_irqrestore(&mutex->lock, flags);
		return RK_SUCCESS;	
	}
	// Mutex is unlocked (mutex->count == 0)
	task = NULL;
	mutex->owner = NULL;
	mode = mutex->mode;
	local_ceiling = mutex->ceiling;
	list_del_init(&mutex->owner_link);
	if (!list_empty(&mutex->wait_list)) {
		// Wake up the highest-priority waiting task
		task = list_entry(mutex->wait_list.next, struct task_struct, rk_mutex_wait_link);
		list_del_init(&task->rk_mutex_wait_link);
		task->rk_mutex_wait_on = -1;
		// Set the new owner of the mutex
		// - PCP: owner will be set later in rk_mutex_lock() by the task
		//        (due to the check of highest_locked_ceiling in rk_mutex_lock)
		if (type != RK_MUTEX_PCP) {
			mutex->owner = task;
			list_add_tail(&mutex->owner_link, &task->rk_mutex_list);
		}
		rkmtx_dbg("info: %s_unlock: wakeup pid %d (mid %d, key %d, owner %d)\n", 
			type_names[type], task->pid, mutex->mid, mutex->key, owner->pid);
	}
	else {
		rkmtx_dbg("info: %s_unlock: no one is waiting (mid %d, key %d, owner %d)\n", 
			type_names[type], mutex->mid, mutex->key, owner->pid);
	}
	raw_spin_unlock_irqrestore(&mutex->lock, flags);
	rkmtx_trace_fn(RK_TRACE_TYPE_CS, false);
	if (type == RK_MUTEX_VMPCP) {
		rkmtx_vm_event(RK_EVENT_TYPE_VM_TASK_EXIT_CS, owner->pid);
	}
	
	if (task) {
		// HLP, MPCP, vMPCP: immediate priority ceiling 
		if (needs_immediate_prio_ceiling(type) == TRUE) {
			task_inherited_prio(task) = task->rt_priority;
			task->rk_mutex_nested_level++;
			task_inherited_prio(task) = task->rt_priority;

			do_immediate_priority_ceiling(type, local_ceiling, task);

			if (task->rt_priority < task_inherited_prio(task)) {
				rkmtx_dbg("info: %s_unlock: immediate prio ceiling (mid %d, key %d, pid %d prio %d -> %d)\n", 
					type_names[type], mutex->mid, mutex->key,
					task->pid, task->rt_priority, task_inherited_prio(task)); 
				rk_mutex_change_task_priority(task, FALSE);
			}
		}
		// Wake up task
		wake_up_process(task);

		// vMPCP: immediate priority ceiling for vcpu
		if (type == RK_MUTEX_VMPCP) {
			rk_mutex_increase_vcpu_priority_from_guest(task, mode);
		}
	}

	// PCP: wake up a task with the highest priority among all the tasks 
	//      blocked by the priority ceiling of the current mutex
	if (type == RK_MUTEX_PCP) {
		rk_mutex_t tmpmtx, highest_mtx = NULL;
		int highest_prio = -1;
		raw_spin_lock_irqsave(&mutex_desc_lock, flags);
		list_for_each_entry(tmpmtx, &online_mutex_head, online_mutex_link) {
			if (tmpmtx == mutex) continue;
			raw_spin_lock(&tmpmtx->lock);
			if (!tmpmtx->owner && !list_empty(&tmpmtx->wait_list)) {
				task = list_entry(tmpmtx->wait_list.next, struct task_struct, rk_mutex_wait_link);
				if ((int)task->rt_priority > highest_prio) { // need casting
					highest_prio = task->rt_priority;
					highest_mtx= tmpmtx;
				}
			}
			raw_spin_unlock(&tmpmtx->lock);
		}
		if (highest_mtx) {
			raw_spin_lock(&highest_mtx->lock);
			task = list_entry(highest_mtx->wait_list.next, struct task_struct, rk_mutex_wait_link);
			list_del_init(&task->rk_mutex_wait_link);
			task->rk_mutex_wait_on = -1;
			raw_spin_unlock(&highest_mtx->lock);

			rkmtx_dbg("info: %s_unlock: wakeup pid %d blocked due to pcp\n", type_names[type], task->pid);
		}
		else task = NULL;

		raw_spin_unlock_irqrestore(&mutex_desc_lock, flags);
		if (task) wake_up_process(task);
	}
	
	// Restore original priority
	if (type == RK_MUTEX_VMPCP) {
		rk_mutex_restore_vcpu_priority_from_guest(owner);
	}
	rk_mutex_restore_priority(owner, FALSE);

	return RK_SUCCESS;
}

int rk_mutex_unlock(int type, int mid)
{
	return __rk_mutex_unlock(type, mid, current);
}


/////////////////////////////////////////////////////////////////////////////
//
// RK Mutex: vmpcp hypercall handlers
//
/////////////////////////////////////////////////////////////////////////////

int __rk_vmpcp_start_gcs_handler(int mode)
{
#ifndef RK_GLOBAL_SCHED
	rk_reserve_t rsv;
	cpu_reserve_t cpursv;
	int cpunum;

	if (current->rk_mutex_inherited_prio_list == NULL) return RK_ERROR;
	cpunum = raw_smp_processor_id();

	// Increase vcpu priority
	task_inherited_prio(current) = current->rt_priority;
	current->rk_mutex_nested_level++;
	task_inherited_prio(current) = rk_global_ceiling(current);
	//rk_mutex_change_task_priority(current, TRUE);
	rk_mutex_change_task_priority(current, FALSE);

	// Disable CPU enforcement if overrun is enabled
	if (mode & MTX_OVERRUN) {
		rsv = rk_get_task_current_cpursv(current);
		cpursv = rsv ? rsv->reserve : NULL;
		if (cpursv && cpursv->gcs_count++ == 0) {
			cpursv->do_enforcement = FALSE;
			rk_enforce_timer_stop(rsv, cpunum);
			//printk("__rk_vmpcp_start_gcs_handler: stop enforce timer (%d)\n", current->pid);
		}
	}

	return RK_SUCCESS;
#else
	return RK_ERROR;
#endif
}

int __rk_vmpcp_finish_gcs_handler(void)
{
#ifndef RK_GLOBAL_SCHED
	cpu_reserve_t cpursv;
	rk_reserve_t rsv;
	int cpunum;

	if (current->rk_mutex_inherited_prio_list == NULL) return RK_ERROR;
	cpunum = raw_smp_processor_id();
	
	// Restore vcpu priority
	//rk_mutex_restore_priority(current, TRUE);
	rk_mutex_restore_priority(current, FALSE);

	// Enable CPU enforcement if it has been disabled
	rsv = rk_get_task_current_cpursv(current);
	cpursv = rsv ? rsv->reserve : NULL;
	if (cpursv && --cpursv->gcs_count == 0) {
		if (cpursv->do_enforcement == FALSE && cpursv->cpu_time_ticks != cpursv->cpu_period_ticks) {
			cpu_tick_data_t next;
			cpursv->do_enforcement = TRUE;
			next = cpursv->avail_ticks_in_cur_period - cpursv->used_ticks_in_cur_period;
			rk_enforce_timer_start(rsv, &next, &cpursv->start_time_of_cur_exec, cpunum);
			//printk("__rk_vmpcp_finish_gcs_handler: restart enforce timer (%d)\n", current->pid);
		}
	}

	return RK_SUCCESS;
#else
	return RK_ERROR;
#endif
}


/////////////////////////////////////////////////////////////////////////////
//
// RK Inter-VM Mutex
//
/////////////////////////////////////////////////////////////////////////////

#ifdef RK_VIRT_SUPPORT

#include <linux/kvm_para.h>

int rk_intervm_mutex_open(int type, int key, int mode);
int rk_intervm_mutex_destroy(int type, int key);
int rk_intervm_mutex_lock(int type, int mid, int is_trylock);
int rk_intervm_mutex_unlock(int type, int mid);

int sys_rk_vmpcp_intervm_mutex(int cmd, int key, int mode)
{
	switch (cmd) {
	case RK_MUTEX_OPEN:
		return rk_intervm_mutex_open(RK_MUTEX_VMPCP, key, mode);
	case RK_MUTEX_DESTROY:
		return rk_intervm_mutex_destroy(RK_MUTEX_VMPCP, key);
	case RK_MUTEX_LOCK: 
		return rk_intervm_mutex_lock(RK_MUTEX_VMPCP, key, FALSE);
	case RK_MUTEX_TRYLOCK:
		return rk_intervm_mutex_lock(RK_MUTEX_VMPCP, key, TRUE);
	case RK_MUTEX_UNLOCK:
		return rk_intervm_mutex_unlock(RK_MUTEX_VMPCP, key);
	}
	return RK_ERROR;
}

// Guest task priority ceiling
// - Called by vchannel_manager (when cmd is received from host)
void rk_intervm_mutex_vmpcp_lock_acquired(struct task_struct *task)
{
	task->rk_mutex_wait_on = -1;

	task_inherited_prio(task) = task->rt_priority;
	task->rk_mutex_nested_level++;
	task_inherited_prio(task) = task->rt_priority;
	do_immediate_priority_ceiling(RK_MUTEX_VMPCP, 0, task);

	if (task->rt_priority < task_inherited_prio(task)) {
		rkmtx_dbg("info: %s_intervm_lock: immediate prio ceiling (pid %d prio %d -> %d)\n", 
			type_names[type], task->pid, task->rt_priority, task_inherited_prio(task)); 
		rk_mutex_change_task_priority(task, FALSE);
	}

	wake_up_process(task);
}

static inline void* rk_intervm_vchannel_host(struct task_struct* vcpu)
{
	if (rk_check_task_cpursv(vcpu) == RK_SUCCESS) {
		cpu_reserve_t rsv = __rk_get_task_default_cpursv(vcpu)->reserve;
		return rsv->vchannel_host;
	}
	return NULL;
}

// rk_intervm_mutex_open (for guest)
// - Opens a mutex associated with the key and returns its id
// - If such a mutex doesn't exist and MTX_CREATE is set, creates a new mutex.
int rk_intervm_mutex_open(int type, int key, int mode) 
{
	int cpunum;
	if (key < 0) {
		printk("rk_%s_intervm_mutex_open: invalid key (%d)\n", type_names[type], key);
		return RK_ERROR;
	}
	if (rk_check_task_cpursv(current) == RK_ERROR) {
		printk("rk_%s_intervm_mutex_open: needs rk cpu reservation\n", type_names[type]);
		return RK_ERROR;
	}
	if (type != RK_MUTEX_VMPCP) {
		printk("rk_%s_intervm_mutex_open: invalid type (%d)\n", type_names[type], type);
		return RK_ERROR;
	}
	if (!is_virtualized) {
		printk("rk_%s_intervm_mutex_open: cannot run in a non-virtualized env\n", type_names[type]);
		return RK_ERROR;
	}
	if (current->rk_resource_set->cpursv_policy != CPURSV_NO_MIGRATION) {
		printk("rk_%s_intervm_mutex_open: resource set must be set CPURSV_NO_MIGRATION\n", type_names[type]);
		return RK_ERROR;
	}
	cpunum = raw_smp_processor_id();
	if (!per_cpu(vchannel_guest, cpunum)) {
		printk("rk_%s_intervm_mutex_open: cannot run without RK vchannel\n", type_names[type]);
		return RK_ERROR;
	}
	
	// Allocate rk_mutex_inherited_prio_list for current task
	if (!current->rk_mutex_inherited_prio_list) {
		if (rk_mutex_create_inherited_prio_list() == RK_ERROR) {
			printk("rk_%s_intervm_mutex_open: failed to allocate memory for prio_list of pid %d\n", 
				type_names[type], current->pid);
			return RK_ERROR;
		}
	}
	
	// Request to the host machine
	if (kvm_hypercall3(HYP_rk_intervm_mutex_open, type, key, mode) == RK_ERROR) {
		printk("rk_%s_intervm_mutex_open: request rejected by host\n", type_names[type]);
		return RK_ERROR;
	}
	return RK_SUCCESS;
}

// rk_intervm_mutex_open_handler (executed by host)
int rk_intervm_mutex_open_handler(int type, int key, int mode) 
{ 
	unsigned long flags;
	rk_intervm_mutex_t mutex;
	int i, mid = MAX_RK_INTERVM_MUTEX_DESC;

	if (!rk_intervm_mutex_desc) return RK_ERROR;
	if (key < 0) {
		printk("rk_%s_intervm_mutex_open_handler: invalid key (%d)\n", type_names[type], key);
		return RK_ERROR;
	}
	if (rk_check_task_cpursv(current) == RK_ERROR) {
		printk("rk_%s_intervm_mutex_open_handler: vcpu needs rk cpu reservation\n", type_names[type]);
		return RK_ERROR;
	}
	if (current->rk_resource_set->cpursv_policy != CPURSV_NO_MIGRATION) {
		printk("rk_%s_intervm_mutex_open_handler: vcpu must be set CPURSV_NO_MIGRATION\n", type_names[type]);
		return RK_ERROR;
	}
	if (rk_intervm_vchannel_host(current) == NULL) {
		printk("rk_%s_intervm_mutex_open_handler: vcpu must have RK vchannel\n", type_names[type]);
		return RK_ERROR;
	}
	if (type != RK_MUTEX_VMPCP) {
		printk("rk_%s_intervm_mutex_open_handler: invalid type (%d)\n", type_names[type], type);
		return RK_ERROR;
	}
	if (is_virtualized) {
		printk("rk_%s_intervm_mutex_open_handler: cannot run in a virtual machine\n", type_names[type]);
		return RK_ERROR;
	}
	
	// Allocate rk_mutex_inherited_prio_list for current task (vcpu)
	if (!current->rk_mutex_inherited_prio_list) {
		if (rk_mutex_create_inherited_prio_list() == RK_ERROR) {
			printk("rk_%s_intervm_mutex_open_handler: failed to allocate memory for prio_list of pid %d\n", 
				type_names[type], current->pid);
			return RK_ERROR;
		}
	}

	// Find a mutex with the key value
	raw_spin_lock_irqsave(&intervm_mutex_desc_lock, flags);
	for (i = 0; i < MAX_RK_INTERVM_MUTEX_DESC; i++) {
		mutex = rk_intervm_mutex_desc[i];
		if (mutex && mutex->key == key) {
			mid = mutex->mid;
			break;
		}
	}
	if (mid < MAX_RK_INTERVM_MUTEX_DESC) {
		if (mutex->type != type) {
			raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
			printk("rk_%s_intervm_mutex_open_handler: mutex type mismatch\n", type_names[type]);
			return RK_ERROR;
		}
		rkmtx_dbg("info: %s_intervm_open_handler (mid %d, key %d, pid %d)\n", 
			type_names[type], mid, mutex->key, current->pid);
		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		return mid;
	}
	if ((mode & MTX_CREATE) == 0) {
		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		printk("rk_%s_intervm_mutex_open_handler: cannot find a mutex with the key %d\n", type_names[type], key);
		return RK_ERROR;
	}

	// Allocate memory and zero it for the new mutex being created 
	mutex = kmalloc(sizeof(struct rk_intervm_mutex), GFP_ATOMIC);
	if (!mutex) {
		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		printk("rk_%s_intervm_mutex_open_handler: failed to allocate memory for rk mutex\n", type_names[type]);
		return RK_ERROR;
	}
	memset(mutex, 0, sizeof(struct rk_intervm_mutex));

	raw_spin_lock_init(&mutex->lock);
	mutex->type = type;
	mutex->mode = (mode & __MTX_MASK);
	mutex->key = key;

	// Find an empty slot
	for (mid = 0; mid < MAX_RK_INTERVM_MUTEX_DESC; mid++) {
		if (rk_intervm_mutex_desc[mid] == NULL) {
			rk_intervm_mutex_desc[mid] = mutex;
			mutex->mid = mid;
			break;
		}
	}
	if (mid == MAX_RK_INTERVM_MUTEX_DESC) {
		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		kfree(mutex);
		printk("rk_%s_intervm_mutex_open_handler: exceeded the maximum number of rk mutexes (%d)\n", 
			type_names[type], MAX_RK_INTERVM_MUTEX_DESC);
		return RK_ERROR;
	}

	rkmtx_dbg("info: %s_intervm_open_handler: created (mid %d, key %d, pid %d)\n", 
		type_names[type], mid, mutex->key, current->pid);
	raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);

	return mid;
}

// rk_intervm_mutex_destroy
// - Destroys a mutex associated with the key
//
// Called by
// - system call
// - rk_mutex_cleanup (by host)
int rk_intervm_mutex_destroy(int type, int key)
{
	if (key < 0) {
		printk("rk_%s_intervm_mutex_destroy: invalid key (%d)\n", type_names[type], key);
		return RK_ERROR;
	}
	if (type != RK_MUTEX_VMPCP) {
		printk("rk_%s_intervm_mutex_destroy: invalid type (%d)\n", type_names[type], type);
		return RK_ERROR;
	}
	if (!is_virtualized) {
		printk("rk_%s_intervm_mutex_destroy: cannot run in a non-virtualized env\n", type_names[type]);
		return RK_ERROR;
	}
	// Request to the host machine
	if (kvm_hypercall2(HYP_rk_intervm_mutex_destroy, type, key) == RK_ERROR) {
		printk("rk_%s_intervm_mutex_destroy: request rejected by host\n", type_names[type]);
		return RK_ERROR;
	}
	return RK_SUCCESS;
}

// rk_intervm_mutex_destroy_handler (executed by host)
int rk_intervm_mutex_destroy_handler(int type, int key)
{
	unsigned long flags;
	rk_intervm_mutex_t mutex;
	struct task_struct *vcpu;
	rk_vchannel_cmd cmd;
	int i, mid = MAX_RK_INTERVM_MUTEX_DESC;

	if (!rk_intervm_mutex_desc) return RK_ERROR;
	if (key < 0) {
		printk("rk_%s_intervm_mutex_destroy_handler: invalid key (%d)\n", type_names[type], key);
		return RK_ERROR;
	}
	if (type != RK_MUTEX_VMPCP) {
		printk("rk_%s_intervm_mutex_destroy_handler: invalid type (%d)\n", type_names[type], type);
		return RK_ERROR;
	}
	if (is_virtualized) {
		printk("rk_%s_intervm_mutex_destroy_handler: cannot run in a virtual machine\n", type_names[type]);
		return RK_ERROR;
	}

	// Find a mutex with the key 
	raw_spin_lock_irqsave(&intervm_mutex_desc_lock, flags);
	for (i = 0; i < MAX_RK_INTERVM_MUTEX_DESC; i++) {
		mutex = rk_intervm_mutex_desc[i];
		if (mutex && mutex->key == key) {
			mid = mutex->mid;
			break;
		}
	}
	if (mid == MAX_RK_INTERVM_MUTEX_DESC) {
		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		printk("rk_%s_intervm_mutex_destroy_handler: cannot find a mutex with the key %d\n", type_names[type], key);
		return RK_ERROR;
	}
	if (rk_intervm_mutex_desc[mid]->type != type) {
		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		printk("rk_%s_intervm_mutex_destroy_handler: mutex type mismatch\n", type_names[type]);
		return RK_ERROR;
	}
	// Remove from the mutex list
	mutex = rk_intervm_mutex_desc[mid];
	rk_intervm_mutex_desc[mid] = NULL;
	rkmtx_dbg("info: %s_intervm_destroy_handler: remove mutex %d (key %d)\n", type_names[type], mid, mutex->key);

	raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);

	// Wait for any task touching the mutex
	raw_spin_unlock_wait(&mutex->lock);

	// Restore the priority of the owner vcpu and task
	if (mutex->owner.vcpu) {
		vcpu = mutex->owner.vcpu;
		// vcpu
		//rk_mutex_restore_priority(vcpu, TRUE);
		rk_mutex_restore_priority(vcpu, FALSE);
		// task
		cmd.cmd = RK_VCHANNEL_CMD_MUTEX_RESTORE_PRIO;
		cmd.pid = mutex->owner.task_id;
		rk_vchannel_send_cmd(rk_intervm_vchannel_host(vcpu), &cmd);
	}

	// Wake up all tasks waiting on this mutex
	for (i = 0; i < mutex->wait_list_size; i++) {
		vcpu = mutex->wait_list[i].vcpu;
		if (vcpu == NULL) continue;

		cmd.cmd = RK_VCHANNEL_CMD_MUTEX_WAKEUP;
		cmd.pid = mutex->wait_list[i].task_id;
		rk_vchannel_send_cmd(rk_intervm_vchannel_host(vcpu), &cmd);
	}
	kfree(mutex);
	return RK_SUCCESS;
}

// rk_intervm_mutex_lock
int rk_intervm_mutex_lock(int type, int mid, int is_trylock)
{
	int ret;
	int try_count = 1;
	int cpunum;

	if (mid < 0 || mid >= MAX_RK_INTERVM_MUTEX_DESC) {
		printk("rk_%s_intervm_mutex_lock: invalid mutex id (%d)\n", type_names[type], mid);
		return RK_ERROR;
	}
	if (current->rk_mutex_nested_level >= MAX_RK_MUTEX_NESTED_LEVEL) {
		printk("rk_%s_intervm_mutex_lock: current task has reached the maximum nested level (%d)\n", type_names[type], MAX_RK_MUTEX_NESTED_LEVEL);
		return RK_ERROR;
	}
	if (type != RK_MUTEX_VMPCP) {
		printk("rk_%s_intervm_mutex_lock: invalid type (%d)\n", type_names[type], type);
		return RK_ERROR;
	}
	if (!is_virtualized) {
		printk("rk_%s_intervm_mutex_lock: cannot run in a non-virtualized env\n", type_names[type]);
		return RK_ERROR;
	}
	if (rk_check_task_cpursv(current) == RK_ERROR) {
		printk("rk_%s_intervm_mutex_lock: needs rk cpu reservation\n", type_names[type]);
		return RK_ERROR;
	}
	if (current->rk_resource_set->cpursv_policy != CPURSV_NO_MIGRATION) {
		printk("rk_%s_intervm_mutex_lock: resource set must be set CPURSV_NO_MIGRATION\n", type_names[type]);
		return RK_ERROR;
	}
	cpunum = raw_smp_processor_id();
	if (!per_cpu(vchannel_guest, cpunum)) {
		printk("rk_%s_intervm_mutex_lock: cannot run without RK vchannel\n", type_names[type]);
		return RK_ERROR;
	}

retry_locking:
	// Should be checked here to handle the task-exit case
	if (!current->rk_mutex_inherited_prio_list) { 
		printk("rk_%s_intervm_mutex_lock: needs to open mutex first\n", type_names[type]);
		return RK_ERROR;
	}
	// Ignore if the current task is waiting on the mutex
	if (current->rk_mutex_wait_on >= 0) {
		rkmtx_dbg("info: %s_intervm_lock: pid %d woken up, but continues to wait (mid %d)\n",
			type_names[type], current->pid, mid);
		goto wait_on_mutex;
	}
	// Request to the host machine
	if (is_trylock) 
		ret = kvm_hypercall4(HYP_rk_intervm_mutex_trylock, type, mid, current->pid, task_inherited_prio(current));
	else
		ret = kvm_hypercall4(HYP_rk_intervm_mutex_lock, type, mid, current->pid, task_inherited_prio(current));

	if (ret == RK_ERROR) {
		printk("rk_%s_intervm_mutex_lock: request rejected by host\n", type_names[type]);
		return RK_ERROR;
	}
	if (ret == RK_INTERVM_LOCK_ACQUIRED) {
		// Needs immediate priority ceiling if try_count <= 1
		// - vcpu priority has been increased by host
		// - task priority needs to be increased
		if (try_count <= 1) {
			task_inherited_prio(current) = current->rt_priority;
			current->rk_mutex_nested_level++;
			task_inherited_prio(current) = current->rt_priority;
			do_immediate_priority_ceiling(type, 0, current);

			if (current->rt_priority < task_inherited_prio(current)) {
				rkmtx_dbg("info: %s_intervm_lock: immediate prio ceiling (mid %d, pid %d prio %d -> %d)\n", 
					type_names[type], mid, current->pid, current->rt_priority, task_inherited_prio(current)); 
				rk_mutex_change_task_priority(current, FALSE);
			}
		}
		rkmtx_dbg("info: %s_intervm_lock: acquire mutex (mid %d, pid %d)\n", 
			type_names[type], mid, current->pid);
		return RK_SUCCESS;
	}
	if (ret == RK_INTERVM_LOCK_RECURSIVE) {
		rkmtx_dbg("info: %s_intervm_lock: acquire mutex (mid %d, pid %d)\n", 
			type_names[type], mid, current->pid);
		return RK_SUCCESS;
	}
	if (ret == RK_INTERVM_LOCK_WAITLISTED) {
		// Mutex cannot be locked by the current task
		// - Task is waitlisted
		current->rk_mutex_wait_on = RK_INTERVM_MUTEX_WAIT_ON_OFFSET + mid;
		rkmtx_dbg("info: %s_intervm_lock: added to wait list (mid %d, pid %d)\n", 
			type_names[type], mid, current->pid);
		goto wait_on_mutex;
	}
	if (ret == RK_INTERVM_LOCK_FAILED) {
		// Mutex cannot be locked by the current task
		// - Mutex trylock case
		return RK_ERROR;
	}
	return RK_ERROR;

wait_on_mutex:
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
	try_count++;
	goto retry_locking;
}

// __rk_intervm_mutex_lock_handler (executed by host)
int __rk_intervm_mutex_lock_handler(int type, int mid, int task_id, int task_prio, int is_trylock, int is_inv_prio)
{
	unsigned long flags;
	rk_intervm_mutex_t mutex;
	rk_reserve_t rsv;
	cpu_reserve_t cpursv;
	int i, j;

	if (!rk_intervm_mutex_desc) return RK_ERROR;
	if (mid < 0 || mid >= MAX_RK_INTERVM_MUTEX_DESC) {
		printk("rk_%s_intervm_mutex_lock_handler: invalid mutex id (%d)\n", type_names[type], mid);
		return RK_ERROR;
	}
	if (current->rk_mutex_nested_level >= MAX_RK_MUTEX_NESTED_LEVEL) {
		printk("rk_%s_intervm_mutex_lock_handler: current task has reached the maximum nested level (%d)\n", type_names[type], MAX_RK_MUTEX_NESTED_LEVEL);
		return RK_ERROR;
	}
	if (!current->rk_mutex_inherited_prio_list) { 
		printk("rk_%s_intervm_mutex_lock_handler: needs to open mutex first\n", type_names[type]);
		return RK_ERROR;
	}
	if (type != RK_MUTEX_VMPCP) {
		printk("rk_%s_intervm_mutex_lock_handler: invalid type (%d)\n", type_names[type], type);
		return RK_ERROR;
	}
	if (is_virtualized) {
		printk("rk_%s_intervm_mutex_lock_handler: cannot run in a virtual machine\n", type_names[type]);
		return RK_ERROR;
	}
	if (rk_check_task_cpursv(current) == RK_ERROR) {
		printk("rk_%s_intervm_mutex_lock_handler: needs rk cpu reservation for vcpu\n", type_names[type]);
		return RK_ERROR;
	}
	if (current->rk_resource_set->cpursv_policy != CPURSV_NO_MIGRATION) {
		printk("rk_%s_intervm_mutex_lock_handler: vcpu must be set CPURSV_NO_MIGRATION\n", type_names[type]);
		return RK_ERROR;
	}
	if (rk_intervm_vchannel_host(current) == NULL) {
		printk("rk_%s_intervm_mutex_lock_handler: vcpu must have RK vchannel\n", type_names[type]);
		return RK_ERROR;
	}

	raw_spin_lock_irqsave(&intervm_mutex_desc_lock, flags);
	mutex = rk_intervm_mutex_desc[mid];
	if (mutex == NULL) {
		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		printk("rk_%s_intervm_mutex_lock_handler: mutex %d does not exist\n", type_names[type], mid);
		return RK_ERROR;
	}
	if (mutex->type != type) {
		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		printk("rk_%s_intervm_mutex_lock_handler: mutex type mismatch\n", type_names[type]);
		return RK_ERROR;
	}
	raw_spin_lock(&mutex->lock);
	raw_spin_unlock(&intervm_mutex_desc_lock);

	rsv = rk_get_task_current_cpursv(current);
	cpursv = rsv ? rsv->reserve : NULL;

	// Mutex can be locked by the current vcpu/task
	// - if the vcpu/task is already the owner of the mutex, or
	// - if the mutex is not locked by any task/vcpu
	if ((!mutex->owner.vcpu && !mutex->owner.task_id)
	    || (mutex->owner.vcpu == current && mutex->owner.task_id == task_id)) {
		int ret;
		int overrun = FALSE;
		if (mutex->count == 0) ret = RK_INTERVM_LOCK_ACQUIRED;
		else ret = RK_INTERVM_LOCK_RECURSIVE;

		// vMPCP: Immediate priority ceiling protocols for vcpu
		//   - Increase vcpu's nested level only if mutex->count == 0 & mutex->owner == NULL 
		//   - In other cases, vcpu's nested level has been already increased by prev owner
		//   - Task priority will be increased by a guest machine
		if (mutex->count == 0 && mutex->owner.vcpu == NULL) { 
			task_inherited_prio(current) = current->rt_priority;
			current->rk_mutex_nested_level++;
			task_inherited_prio(current) = current->rt_priority;
			// Check if overrun is needed
			if (cpursv && cpursv->gcs_count++ == 0 && (mutex->mode & MTX_OVERRUN)) overrun = TRUE;
		}
		mutex->owner.vcpu = current;
		mutex->owner.task_id = task_id;
		mutex->owner.task_prio = task_prio;
		mutex->count++;
		do_immediate_priority_ceiling(type, 0, current);
		
		raw_spin_unlock_irqrestore(&mutex->lock, flags);

		rkmtx_event(RK_EVENT_TYPE_VM_TASK_ENTER_CS, raw_smp_processor_id(), current->pid, task_id, current->rt_priority);
		if (current->rt_priority < task_inherited_prio(current)) {
			rkmtx_dbg("info: %s_intervm_lock: immediate vcpu prio ceiling (mid %d, vcpuid %d prio %d -> %d)\n", 
				type_names[type], mutex->mid, current->pid, 
				current->rt_priority, task_inherited_prio(current)); 
			//rk_mutex_change_task_priority(current, TRUE);
			rk_mutex_change_task_priority(current, FALSE);
		}
		// Disable CPU enforcement if overrun is needed
		if (overrun) {
			cpursv->do_enforcement = FALSE;
			rk_enforce_timer_stop(rsv, raw_smp_processor_id());
		}

		rkmtx_dbg("info: %s_intervm_lock: acquire mutex (mid %d, key %d, vcpuid %d)\n", 
			type_names[type], mutex->mid, mutex->key, current->pid);
		return ret;
	}
	
	// Mutex cannot be locked by the current vcpu/task
	// -> trylock
	if (is_trylock) {
		raw_spin_unlock_irqrestore(&mutex->lock, flags);
		return RK_INTERVM_LOCK_FAILED;
	}

	// Mutex cannot be locked by the current vcpu/task
	// -> Add the current vcpu/task to the wait_list
	if (mutex->wait_list_size >= MAX_RK_INTERVM_MUTEX_WAIT_LIST) {
		raw_spin_unlock_irqrestore(&mutex->lock, flags);
		printk("rk_%s_intervm_mutex_lock_handler: wait list is full (mid %d, vcpuid %d)\n", type_names[type], mutex->mid, current->pid);
		return RK_ERROR;
	}
	// Simple implementation of a two-level waiting list
	for (i = 0; i < mutex->wait_list_size; i++) {
		// Level 1: VCPU priority check
		if (mutex->wait_list[i].vcpu->rt_priority > current->rt_priority) continue;

		// Level 2: Task priority check
		// - If is_inv_prio == false, higher task_prio means higher task priority (ex, guest is Linux)
		// - If is_inv_prio == true, lower task_prio means lower task priority.
		if (mutex->wait_list[i].vcpu->rt_priority == current->rt_priority
		    && ((is_inv_prio == false && mutex->wait_list[i].task_prio >= task_prio)
		        || (is_inv_prio == true && mutex->wait_list[i].task_prio <= task_prio))) continue;
		
		// Insert here
		for (j = mutex->wait_list_size; j > i; j--) 
			mutex->wait_list[j] = mutex->wait_list[j - 1];
		break;
	}
	mutex->wait_list[i].vcpu = current;
	mutex->wait_list[i].task_id = task_id;
	mutex->wait_list[i].task_prio = task_prio;
	mutex->wait_list_size++;

	raw_spin_unlock_irqrestore(&mutex->lock, flags);
	rkmtx_dbg("info: %s_intervm_lock: added to wait list (mid %d, vcpuid %d, taskid %d)\n", 
		type_names[type], mutex->mid, current->pid, task_id);
	
	return RK_INTERVM_LOCK_WAITLISTED;
}

int rk_intervm_mutex_lock_handler(int type, int mid, int task_id, int task_prio, int is_inv_prio)
{
	return __rk_intervm_mutex_lock_handler(type, mid, task_id, task_prio, FALSE, is_inv_prio);
}

int rk_intervm_mutex_trylock_handler(int type, int mid, int task_id, int task_prio, int is_inv_prio)
{
	return __rk_intervm_mutex_lock_handler(type, mid, task_id, task_prio, TRUE, is_inv_prio);
}

// rk_intervm_mutex_unlock (system call for guest)
int rk_intervm_mutex_unlock(int type, int mid) 
{ 
	int ret;

	if (mid < 0 || mid >= MAX_RK_INTERVM_MUTEX_DESC) {
		printk("rk_%s_intervm_mutex_unlock: invalid mutex id (%d)\n", type_names[type], mid);
		return RK_ERROR;
	}
	if (type != RK_MUTEX_VMPCP) {
		printk("rk_%s_intervm_mutex_unlock: invalid type (%d)\n", type_names[type], type);
		return RK_ERROR;
	}
	if (!is_virtualized) {
		printk("rk_%s_intervm_mutex_unlock: cannot run in a non-virtualized env\n", type_names[type]);
		return RK_ERROR;
	}

	ret = kvm_hypercall3(HYP_rk_intervm_mutex_unlock, type, mid, current->pid);
	if (ret == RK_ERROR) {
		printk("rk_%s_intervm_mutex_unlock: request rejected by host\n", type_names[type]);
		return RK_ERROR;
	}
	if (ret == RK_INTERVM_UNLOCK_DONE) {
		// Restore task priority (vcpu priority is restored by host)
		rk_mutex_restore_priority(current, FALSE);
		return RK_SUCCESS;
	}
	if (ret == RK_INTERVM_UNLOCK_RECURSIVE) {
		// Mutex is still held by the current task -> do nothing
		return RK_SUCCESS;
	}
	return RK_ERROR;
}

// rk_intervm_mutex_unlock_handler (executed by host)
int rk_intervm_mutex_unlock_handler(int type, int mid, int owner_task, struct task_struct *owner_vcpu) 
{
	unsigned long flags;
	rk_intervm_mutex_t mutex;
	rk_vchannel_cmd cmd;
	struct task_struct *vcpu;
	rk_reserve_t rsv;
	cpu_reserve_t cpursv;
	int i;

	if (!rk_intervm_mutex_desc) return RK_ERROR;
	if (mid < 0 || mid >= MAX_RK_INTERVM_MUTEX_DESC) {
		printk("rk_%s_intervm_mutex_unlock_handler: invalid mutex id (%d)\n", type_names[type], mid);
		return RK_ERROR;
	}
	if (type != RK_MUTEX_VMPCP) {
		printk("rk_%s_intervm_mutex_unlock_handler: invalid type (%d)\n", type_names[type], type);
		return RK_ERROR;
	}
	if (is_virtualized) {
		printk("rk_%s_intervm_mutex_unlock_handler: cannot run in a virtual machine\n", type_names[type]);
		return RK_ERROR;
	}

	raw_spin_lock_irqsave(&intervm_mutex_desc_lock, flags);
	mutex = rk_intervm_mutex_desc[mid];
	if (mutex == NULL) {
		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		printk("rk_%s_intervm_mutex_unlock_handler: mutex %d does not exist\n", type_names[type], mid);
		return RK_ERROR;
	}
	if (mutex->type != type) {
		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		printk("rk_%s_intervm_mutex_unlock_handler: mutex type mismatch\n", type_names[type]);
		return RK_ERROR;
	}
	raw_spin_lock(&mutex->lock);
	raw_spin_unlock(&intervm_mutex_desc_lock);

	// Mutex is not locked by owner vcpu/task
	if (mutex->owner.vcpu != owner_vcpu || mutex->owner.task_id != owner_task) {
		raw_spin_unlock_irqrestore(&mutex->lock, flags);
		return RK_ERROR;
	}
	// Mutex is still held by the owner task
	if (--mutex->count > 0) {
		raw_spin_unlock_irqrestore(&mutex->lock, flags);
		return RK_SUCCESS;	
	}
	// Mutex is unlocked (mutex->count == 0)
	vcpu = NULL;
	mutex->owner.vcpu = NULL;
	mutex->owner.task_id = 0;
	if (mutex->wait_list_size > 0) {
		// Wake up the highest-priority waiting task
		vcpu = mutex->wait_list[0].vcpu;
		cmd.cmd = RK_VCHANNEL_CMD_VMPCP_LOCK_ACQUIRED;
		cmd.pid = mutex->wait_list[0].task_id;

		// Set the new owner of the mutex
		mutex->owner = mutex->wait_list[0];
		for (i = 1; i < mutex->wait_list_size; i++) {
			mutex->wait_list[i - 1] = mutex->wait_list[i];
		}
		mutex->wait_list_size--;

		rkmtx_dbg("info: %s_intervm_unlock: wakeup task %d in vcpu %d (mid %d, cur_vcpu %d)\n", 
			type_names[type], mutex->owner.vcpu->pid, mutex->owner.task_id, mutex->mid, owner_vcpu->pid);
	}
	else {
		rkmtx_dbg("info: %s_intervm_unlock: no one is waiting (mid %d, cur_vcpu %d)\n", 
			type_names[type], mutex->mid, owner_vcpu->pid);
	}
	raw_spin_unlock_irqrestore(&mutex->lock, flags);
	rkmtx_event(RK_EVENT_TYPE_VM_TASK_EXIT_CS, raw_smp_processor_id(), owner_vcpu->pid, owner_task, current->rt_priority);
	
	if (vcpu) {
		int overrun = FALSE;

		rsv = rk_get_task_current_cpursv(vcpu);
		cpursv = rsv ? rsv->reserve : NULL;

		// vMPCP: Immediate priority ceiling for new owner's vcpu
		task_inherited_prio(vcpu) = vcpu->rt_priority;
		vcpu->rk_mutex_nested_level++;
		task_inherited_prio(vcpu) = vcpu->rt_priority;
		do_immediate_priority_ceiling(type, 0, vcpu);
		// Check if overrun is needed
		if (cpursv && cpursv->gcs_count++ == 0 && (mutex->mode & MTX_OVERRUN)) overrun = TRUE;

		if (vcpu->rt_priority < task_inherited_prio(vcpu)) {
			rkmtx_dbg("info: %s_intervm_unlock: immediate prio ceiling (mid %d, vcpuid %d prio %d -> %d)\n", 
				type_names[type], mutex->mid, 
				task->pid, task->rt_priority, task_inherited_prio(task)); 
			//rk_mutex_change_task_priority(vcpu, TRUE);
			rk_mutex_change_task_priority(vcpu, FALSE);
		}
		// Disable CPU enforcement if overrun is enabled
		// - don't need to stop enforcment timer as the vcpu is not running
		if (overrun) {
			cpursv->do_enforcement = FALSE;
		}
		// Wake up guest task
		rk_vchannel_send_cmd(rk_intervm_vchannel_host(vcpu), &cmd);
	}

	// Restore original vcpu priority
	//rk_mutex_restore_priority(owner_vcpu, TRUE);
	rk_mutex_restore_priority(owner_vcpu, FALSE);

	// Enable CPU enforcement if it has been disabled
	rsv = rk_get_task_current_cpursv(owner_vcpu);
	cpursv = rsv ? rsv->reserve : NULL;
	if (cpursv && --cpursv->gcs_count == 0) {
		if (owner_vcpu == current) {
			if (cpursv->do_enforcement == FALSE && cpursv->cpu_time_ticks != cpursv->cpu_period_ticks) {
				cpu_tick_data_t next;
				cpursv->do_enforcement = TRUE;
				next = cpursv->avail_ticks_in_cur_period - cpursv->used_ticks_in_cur_period;
				rk_enforce_timer_start(rsv, &next, &cpursv->start_time_of_cur_exec, raw_smp_processor_id());
			}
		}
		else if (cpursv->do_enforcement == FALSE && cpursv->cpu_res_attr.cpunum != raw_smp_processor_id()) {
			// we cannot start the enforcement timer for owner_vcpu running on different core
			// so, wake up rk-worker of the target cpu to stop owner_vcpu
			cpursv->do_enforcement = TRUE;
			wake_up_process(per_cpu(rk_worker, cpursv->cpu_res_attr.cpunum));
		}
	}
	
	return RK_SUCCESS;
}

// Called by 
// - rk_mutex_task_cleanup (guest)
int rk_intervm_mutex_unlock_all(struct task_struct *task) 
{
	if (task == NULL) return RK_ERROR;
	
	// TODO: "task != current" means that the task's rset is being detached by another task.
	//       Should the task's mutex also be unlocked in this case?
	//	 If so, the task's real vcpu id needs to be somehow provided to the host.
	if (task != current) return RK_ERROR;

	// unlock all mutexes locked by that task
	return kvm_hypercall1(HYP_rk_intervm_mutex_unlock_all, task->pid);
}

// Called by 
// - hypercall from guest (rk_intervm_mutex_unlock_all)
// - rk_mutex_task_cleanup (host) 
int rk_intervm_mutex_unlock_all_handler(int task_id, struct task_struct *vcpu)
{ 
	unsigned long flags;
	rk_intervm_mutex_t mutex;
	int i;

	if (!rk_intervm_mutex_desc) return RK_ERROR;
	if (is_virtualized) {
		printk("rk_intervm_mutex_unlock_all_handler: cannot run in a virtual machine\n");
		return RK_ERROR;
	}

retry_unlock:
	raw_spin_lock_irqsave(&intervm_mutex_desc_lock, flags);
	for (i = 0; i < MAX_RK_INTERVM_MUTEX_DESC; i++) {
		mutex = rk_intervm_mutex_desc[i];
		if (mutex == NULL) continue;
		if (vcpu && mutex->owner.vcpu != vcpu) continue;
		if (task_id && mutex->owner.task_id != task_id) continue;

		raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
		rk_intervm_mutex_unlock_handler(mutex->type, mutex->mid, mutex->owner.task_id, mutex->owner.vcpu);
		goto retry_unlock;
	}
	raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
	
	return RK_SUCCESS; 
}

// Called by 
// - rk_mutex_task_cleanup (guest)
int rk_intervm_mutex_remove_from_waitlist(int mid, struct task_struct *task) 
{
	if (task == NULL) return RK_ERROR;
	
	// TODO: "task != current" means that the task's rset is being detached by another task.
	//       Should the task also be removed from the mutex waitlist in this case?
	//	 If so, the task's real vcpu id needs to be somehow provided to the host.
	if (task != current) return RK_ERROR;

	// unlock all mutexes locked by that task
	return kvm_hypercall2(HYP_rk_intervm_mutex_remove_from_waitlist, mid, task->pid);
}

// Called by 
// - hypercall from guest (rk_intervm_mutex_remove_from_waitlist)
// - rk_mutex_task_cleanup (host) 
int rk_intervm_mutex_remove_from_waitlist_handler(int mid, int task_id, struct task_struct *vcpu)
{ 
	unsigned long flags;
	rk_intervm_mutex_t mutex;
	int i, j, k, start, end;

	if (!rk_intervm_mutex_desc) return RK_ERROR;
	if (is_virtualized) {
		printk("rk_intervm_mutex_remove_from_waitlist_handler: cannot run in a virtual machine\n");
		return RK_ERROR;
	}

	if (mid >= 0 && mid <= MAX_RK_INTERVM_MUTEX_DESC) {
		start = end = mid;
	}
	else {
		start = 0;
		end = MAX_RK_INTERVM_MUTEX_DESC - 1;
	}
	raw_spin_lock_irqsave(&intervm_mutex_desc_lock, flags);
	for (i = start; i <= end; i++) {
		mutex = rk_intervm_mutex_desc[i];
		if (mutex == NULL) continue;
		for (j = 0; j < mutex->wait_list_size; j++) {
			if (mutex->wait_list[j].vcpu != vcpu) continue;
			if (task_id && mutex->wait_list[j].task_id != task_id) continue;
			for (k = j + 1; k < mutex->wait_list_size; k++) {
				mutex->wait_list[k - 1] = mutex->wait_list[k];
			}
			mutex->wait_list_size--;
		}
	}
	raw_spin_unlock_irqrestore(&intervm_mutex_desc_lock, flags);
	return RK_SUCCESS; 
}

#else // RK_VIRT_SUPPORT

int sys_rk_vmpcp_intervm_mutex(int cmd, int key, int mode) { return RK_ERROR; }
int rk_intervm_mutex_unlock_all(struct task_struct *owner) { return RK_ERROR; }
int rk_intervm_mutex_unlock_all_handler(int task_id, struct task_struct *vcpu) { return RK_ERROR; }
int rk_intervm_mutex_remove_from_waitlist(int mid, struct task_struct *task) { return RK_ERROR; }
int rk_intervm_mutex_remove_from_waitlist_handler(int mid, int task_id, struct task_struct *vcpu) { return RK_ERROR; }

#endif // RK_VIRT_SUPPORT



