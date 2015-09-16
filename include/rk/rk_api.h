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

#ifndef RK_API_H
#define RK_API_H

#define __RK__

#include <rk_common.h>

#if defined(__amd64__) || defined(__amd64) || defined(amd64) || defined(__AMD64__) || defined(ARCH_x86_64)
	#include <arch/x86/include/generated/uapi/asm/unistd_64.h>
#elif defined(__i386__) || defined(__i386) || defined(i386) || defined(__I386__) || defined(ARCH_i386)
	#include <arch/x86/include/generated/uapi/asm/unistd_32.h>
#elif defined(__ia64__) || defined(__ia64) || defined(ia64) || defined(__IA64__)
	#include <arch/ia64/include/asm/unistd.h>
#elif defined(__arm__) || defined(__arm) || defined(arm) || defined(__ARM__) || defined(ARCH_arm)
	#include <arch/arm/include/asm/unistd.h>
//#elif defined(__powerpc__) || defined(__powerpc) || defined(powerpc) || defined(__POWERPC__)
//#elif defined(__sparc__) || defined(__sparc) || defined(sparc) || defined(__SPARC__)
//#elif defined(__alpha__) || defined(__alpha) || defined(alpha) || defined(__ALPHA__)
#else
	#error "Does not support this architecture"
#endif


/* RK APIs: Resource Set */
static inline int rk_resource_set_create(const char *name, int inherit_flag, int cleanup_flag, int cpursv_policy) {
	return syscall(__NR_rk_resource_set_create, name, inherit_flag, cleanup_flag, cpursv_policy);
}
static inline int rk_resource_set_destroy(int rd) {
	return syscall(__NR_rk_resource_set_destroy, rd);
}
static inline int rk_resource_set_attach_process(int rd, pid_t pid, struct rk_ordered_list *cpursv_list) {
	return syscall(__NR_rk_resource_set_attach_process, rd, pid, cpursv_list);
}
static inline int rk_resource_set_detach_process(int rd, pid_t pid) {
	return syscall(__NR_rk_resource_set_detach_process, rd, pid);
}

/* RK APIs: CPU Reserve */
static inline int rk_cpu_reserve_create(int rd, cpu_reserve_attr_t cpu_attr) {
	return syscall(__NR_rk_cpu_reserve_create, rd, cpu_attr);
}
static inline void rk_setschedulingpolicy(int policy) {
	syscall(__NR_rk_setschedulingpolicy, policy);
}
static inline int rk_is_scheduling(void) {
	return syscall(__NR_rk_is_scheduling);
}
static inline int rt_wait_for_next_period(void) {
	return syscall(__NR_rt_wait_for_next_period);
}
static inline void rk_get_start_of_current_period(unsigned long long *tm) {
	syscall(__NR_rk_get_start_of_current_period, tm);
}
static inline void rk_get_current_time(unsigned long long *tm) {
	syscall(__NR_rk_get_current_time, tm);
}

/* RK APIs: CPU Reserve - Profile */
static inline void rk_getcpursv_prev_used_ticks(int rd, unsigned long long *ret) {
	syscall(__NR_rk_getcpursv_prev_used_ticks, rd, ret);
}
static inline void rk_getcpursv_min_utilization(int rd, unsigned long *ret) {
	syscall(__NR_rk_getcpursv_min_utilization, rd, ret);
}
static inline void rk_getcpursv_max_utilization(int rd, unsigned long *ret) {
	syscall(__NR_rk_getcpursv_max_utilization, rd, ret);
}
static inline int rk_getcpursv_start_profile(int rd, int cpursv_index, int size) {
	return syscall(__NR_rk_getcpursv_profile, 0, rd, cpursv_index, size);
}
static inline int rk_getcpursv_get_profile(int rd, int cpursv_index, struct rk_cpu_profile *buf) {
	return syscall(__NR_rk_getcpursv_profile, 1, rd, cpursv_index, buf);
}
static inline int rk_getcpursv_start_task_profile(int pid, int size) {
	return syscall(__NR_rk_getcpursv_profile, 2, pid, size, NULL);
}
static inline int rk_getcpursv_get_task_profile(int pid, struct rk_cpu_profile *buf) {
	return syscall(__NR_rk_getcpursv_profile, 3, pid, buf, NULL);
}

/* RK APIs: MEM Reserve */
static inline int rk_mem_reserve_create(int rd, mem_reserve_attr_t usr_mem_attr) {
	return syscall(__NR_rk_mem_reserve_create, rd, usr_mem_attr, NULL);
}
static inline int rk_mem_reserve_create_with_auxmem(int rd, mem_reserve_attr_t usr_mem_attr, mem_reserve_attr_t usr_aux_attr) {
	return syscall(__NR_rk_mem_reserve_create, rd, usr_mem_attr, usr_aux_attr);
}
static inline int rk_mem_reserve_delete(int rd) {
	return syscall(__NR_rk_mem_reserve_delete, rd);
}
static inline int rk_mem_reserve_eviction_lock(pid_t pid, unsigned long vaddr, size_t size, char lock) {
	return syscall(__NR_rk_mem_reserve_eviction_lock, pid, vaddr, size, lock);
}

/* RK APIs: RK Mutex - PIP */
static inline int rk_pip_mutex_open(int key, int mode) {
	return syscall(__NR_rk_pip_mutex, RK_MUTEX_OPEN, key, mode);
}
static inline int rk_pip_mutex_destroy(int key) {
	return syscall(__NR_rk_pip_mutex, RK_MUTEX_DESTROY, key, 0);
}
static inline int rk_pip_mutex_lock(int mid) {
	return syscall(__NR_rk_pip_mutex, RK_MUTEX_LOCK, mid, 0);
}
static inline int rk_pip_mutex_trylock(int mid) {
	return syscall(__NR_rk_pip_mutex, RK_MUTEX_TRYLOCK, mid, 0);
}
static inline int rk_pip_mutex_unlock(int mid) {
	return syscall(__NR_rk_pip_mutex, RK_MUTEX_UNLOCK, mid, 0);
}

/* RK APIs: RK Mutex - PCP */
static inline int rk_pcp_mutex_open(int key, int mode) {
	return syscall(__NR_rk_pcp_mutex, RK_MUTEX_OPEN, key, mode);
}
static inline int rk_pcp_mutex_destroy(int key) {
	return syscall(__NR_rk_pcp_mutex, RK_MUTEX_DESTROY, key, 0);
}
static inline int rk_pcp_mutex_lock(int mid) {
	return syscall(__NR_rk_pcp_mutex, RK_MUTEX_LOCK, mid, 0);
}
static inline int rk_pcp_mutex_trylock(int mid) {
	return syscall(__NR_rk_pcp_mutex, RK_MUTEX_TRYLOCK, mid, 0);
}
static inline int rk_pcp_mutex_unlock(int mid) {
	return syscall(__NR_rk_pcp_mutex, RK_MUTEX_UNLOCK, mid, 0);
}

/* RK APIs: RK Mutex - HLP */
static inline int rk_hlp_mutex_open(int key, int mode) {
	return syscall(__NR_rk_hlp_mutex, RK_MUTEX_OPEN, key, mode);
}
static inline int rk_hlp_mutex_destroy(int key) {
	return syscall(__NR_rk_hlp_mutex, RK_MUTEX_DESTROY, key, 0);
}
static inline int rk_hlp_mutex_lock(int mid) {
	return syscall(__NR_rk_hlp_mutex, RK_MUTEX_LOCK, mid, 0);
}
static inline int rk_hlp_mutex_trylock(int mid) {
	return syscall(__NR_rk_hlp_mutex, RK_MUTEX_TRYLOCK, mid, 0);
}
static inline int rk_hlp_mutex_unlock(int mid) {
	return syscall(__NR_rk_hlp_mutex, RK_MUTEX_UNLOCK, mid, 0);
}

/* RK APIs: RK Mutex - MPCP */
static inline int rk_mpcp_mutex_open(int key, int mode) {
	return syscall(__NR_rk_mpcp_mutex, RK_MUTEX_OPEN, key, mode);
}
static inline int rk_mpcp_mutex_destroy(int key) {
	return syscall(__NR_rk_mpcp_mutex, RK_MUTEX_DESTROY, key, 0);
}
static inline int rk_mpcp_mutex_lock(int mid) {
	return syscall(__NR_rk_mpcp_mutex, RK_MUTEX_LOCK, mid, 0);
}
static inline int rk_mpcp_mutex_trylock(int mid) {
	return syscall(__NR_rk_mpcp_mutex, RK_MUTEX_TRYLOCK, mid, 0);
}
static inline int rk_mpcp_mutex_unlock(int mid) {
	return syscall(__NR_rk_mpcp_mutex, RK_MUTEX_UNLOCK, mid, 0);
}

/* RK APIs: RK Mutex - vMPCP */
static inline int rk_vmpcp_mutex_open(int key, int mode) {
	return syscall(__NR_rk_vmpcp_mutex, RK_MUTEX_OPEN, key, mode);
}
static inline int rk_vmpcp_mutex_destroy(int key) {
	return syscall(__NR_rk_vmpcp_mutex, RK_MUTEX_DESTROY, key, 0);
}
static inline int rk_vmpcp_mutex_lock(int mid) {
	return syscall(__NR_rk_vmpcp_mutex, RK_MUTEX_LOCK, mid, 0);
}
static inline int rk_vmpcp_mutex_trylock(int mid) {
	return syscall(__NR_rk_vmpcp_mutex, RK_MUTEX_TRYLOCK, mid, 0);
}
static inline int rk_vmpcp_mutex_unlock(int mid) {
	return syscall(__NR_rk_vmpcp_mutex, RK_MUTEX_UNLOCK, mid, 0);
}

/* RK APIs: RK Mutex - vMPCP (Inter-VM) */
static inline int rk_vmpcp_intervm_mutex_open(int key, int mode) {
	return syscall(__NR_rk_vmpcp_intervm_mutex, RK_MUTEX_OPEN, key, mode);
}
static inline int rk_vmpcp_intervm_mutex_destroy(int key) {
	return syscall(__NR_rk_vmpcp_intervm_mutex, RK_MUTEX_DESTROY, key, 0);
}
static inline int rk_vmpcp_intervm_mutex_lock(int mid) {
	return syscall(__NR_rk_vmpcp_intervm_mutex, RK_MUTEX_LOCK, mid, 0);
}
static inline int rk_vmpcp_intervm_mutex_trylock(int mid) {
	return syscall(__NR_rk_vmpcp_intervm_mutex, RK_MUTEX_TRYLOCK, mid, 0);
}
static inline int rk_vmpcp_intervm_mutex_unlock(int mid) {
	return syscall(__NR_rk_vmpcp_intervm_mutex, RK_MUTEX_UNLOCK, mid, 0);
}

/* RK APIs: Trace and Event Log */
static inline int rk_trace(int type, int nr, void *data) {
	return syscall(__NR_rk_trace, type, nr, data);
}
static inline int rk_trace_set(int pid, int size) {
	return rk_trace(RK_TRACE_SYSCALL_SET, pid, (void*)(long)size);
}
static inline int rk_trace_get(int pid, struct rk_trace_data *p) {
	return rk_trace(RK_TRACE_SYSCALL_GET, pid, p);
}
static inline int rk_trace_sum_set(int pid) {
	return rk_trace(RK_TRACE_SYSCALL_SUM_SET, pid, NULL);
}
static inline int rk_trace_sum_get(int pid, struct rk_trace_data_sum *p) {
	return rk_trace(RK_TRACE_SYSCALL_SUM_GET, pid, p);
}
static inline int rk_event_log_set(int pid) {
	return rk_trace(RK_TRACE_SYSCALL_EVENT_LOG_SET, pid, NULL);
}
static inline int rk_event_log_get(struct rk_event_data *p) {
	return rk_trace(RK_TRACE_SYSCALL_EVENT_LOG_GET, 0, p);
}

/* RK APIs: VCPU info */
static inline int rk_get_start_of_next_vcpu_period(unsigned long long *tm) {
	return syscall(__NR_rk_get_start_of_next_vcpu_period, tm);
}

/* RK APIs: VChannel */
static inline int rk_vchannel(int type, int nr, const void *data) {
	return syscall(__NR_rk_vchannel, type, nr, data);
}
static inline int rk_vchannel_register_host(int rd, int cpursv_idx, const char *path) {
	long msg[2];
	msg[0] = cpursv_idx,
	msg[1] = (long)path;
	return rk_vchannel(RK_VCHANNEL_SYSCALL_REGISTER_HOST, rd, msg);
}
static inline int rk_vchannel_register_guest(int cpunum, const char *path) {
	return rk_vchannel(RK_VCHANNEL_SYSCALL_REGISTER_GUEST, cpunum, path);
}
static inline int rk_vchannel_send_cmd(int rd, int cpursv_idx, int cmd, int pid) {
	int msg[3];
	msg[0] = cpursv_idx;
	msg[1] = cmd;
	msg[2] = pid;
	return rk_vchannel(RK_VCHANNEL_SYSCALL_SEND_CMD, rd, msg);
}

/* RK APIs: vINT - virtual interrupt handling */
static inline int rk_vint_register_pseudo_vcpu(int vcpu_pid, pseudo_vcpu_attr_t attr) {
	return syscall(__NR_rk_vint_register_pseudo_vcpu, vcpu_pid, attr);
}

/* RK APIs: Testing */
static inline int rk_testing(int index, int nr, void *data) {
	return syscall(__NR_rk_testing, index, nr, data);
}
static inline void rt_suspend_until_rsv_start(void) {
	rk_testing(0x102, 0, NULL);
}
static inline int rk_mem_reserve_show_reserved_pages(int rd) {
	return rk_testing(0x200, rd, NULL);
}
static inline int rk_mem_reserve_show_task_vminfo(int pid) {
	return rk_testing(0x201, pid, NULL);
}
static inline int rk_mem_reserve_do_alloc_test(int pid) {
	return rk_testing(0x202, pid, NULL);
}
static inline int rk_mem_reserve_swap_out_page(int pid, void* vaddr) {
	return rk_testing(0x203, pid, vaddr);
}
static inline int rk_mem_reserve_show_color_info(int color_idx) {
	return rk_testing(0x204, color_idx, NULL);
}
static inline int rk_mem_reserve_traverse_page_table(int pid) {
	return rk_testing(0x205, pid, NULL);
}


#endif

