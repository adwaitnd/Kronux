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
 * rk_virt.h: RK virtualization support
 */ 

#ifndef RK_VIRT_H
#define RK_VIRT_H

extern int is_virtualized;
DECLARE_PER_CPU(struct file*, vchannel_guest);

typedef struct {
	int cmd;
	int pid;
} rk_vchannel_cmd;

#ifdef RK_VIRT_SUPPORT

// RK vchannel option
#define RK_VCHANNEL_SOCKET
//#define RK_VCHANNEL_PIPE

// RK hypercalls
#define HYP_rk_ping_host_machine			100
#define HYP_rk_get_remaining_time_to_next_vcpu_period	101
#define HYP_rk_send_vm_event				102
#define HYP_rk_get_vcpu_priority			103

#define HYP_rk_create_vcpu_inherited_prio_list		200
#define HYP_rk_vmpcp_start_gcs				201
#define HYP_rk_vmpcp_finish_gcs				202
#define HYP_rk_intervm_mutex_open			203
#define HYP_rk_intervm_mutex_lock			204
#define HYP_rk_intervm_mutex_unlock			205
#define HYP_rk_intervm_mutex_destroy			206
#define HYP_rk_intervm_mutex_trylock			207
#define HYP_rk_intervm_mutex_unlock_all			208
#define HYP_rk_intervm_mutex_remove_from_waitlist	209
#define HYP_rk_intervm_mutex_lock_inv_prio		210
#define HYP_rk_intervm_mutex_trylock_inv_prio		211

#define HYP_rk_vcoloring				300

// VChannel command list
#define RK_VCHANNEL_CMD_KILL			1
#define RK_VCHANNEL_CMD_MUTEX_WAKEUP		2
#define RK_VCHANNEL_CMD_VMPCP_LOCK_ACQUIRED	3
#define RK_VCHANNEL_CMD_MUTEX_RESTORE_PRIO	4

// Pseudo-VCPU list
typedef struct {
	int host_irq_no; // -1: not in use
	int guest_irq_no;
	struct task_struct *vcpu;
	cpu_reserve_t pseudo_vcpu_cpursv;
	int pseudo_vcpu_cpursv_idx;
	void* dev;
	bool intr_enforced;
} pseudo_vcpu_list_entry;

#define RK_MAX_PSEUDO_VCPU_LIST 32

void rk_virt_init(void);
void rk_virt_cleanup(void);
int rk_hypercall_handler(void *vcpuptr, unsigned long nr, unsigned long a0, unsigned long a1, unsigned long a2, unsigned long a3);

int rk_ping_host_machine(void);
int rk_get_remaining_time_to_next_vcpu_period(void);
int rk_send_vm_event(int type, int pid);
int rk_get_vcpu_priority(void);

int rk_create_vcpu_inherited_prio_list(void);
int rk_vmpcp_start_gcs(int mode);
int rk_vmpcp_finish_gcs(void);

int sys_rk_vchannel_register_host(int rd, int cpursv_idx, char *path);
int sys_rk_vchannel_register_guest(int cpunum, char *path);
int rk_vchannel_send_cmd(void *channel, rk_vchannel_cmd *cmd);

void remove_from_pseudo_vcpu_list(struct task_struct *task, cpu_reserve_t cpursv);
int rk_kvm_assigned_dev_intr_handler(int host_irq_no, int guest_irq_no, void* dev);
int rk_kvm_assigned_dev_eoi_handler(int host_irq_no, int guest_irq_no);
int rk_kvm_vm_ioctl_assign_irq_handler(int host_irq_no, int guest_irq_no, void* dev);
int rk_kvm_vm_ioctl_deassign_dev_irq_handler(void* dev);
void pseudo_vcpu_replenish(cpu_reserve_t pseudo_vcpu, int *priority);
void pseudo_vcpu_enforce(cpu_reserve_t pseudo_vcpu);

#else

static inline void rk_virt_init(void) {}
static inline void rk_virt_cleanup(void) {}
static inline int rk_hypercall_handler(unsigned long nr, unsigned long a0, unsigned long a1, unsigned long a2, unsigned long a3) { return RK_ERROR; }

static inline int rk_ping_host_machine(void) { return RK_ERROR; }
static inline int rk_get_remaining_time_to_next_vcpu_period(void) { return RK_ERROR; }
static inline int rk_send_vm_event(int type, int pid) { return RK_ERROR; }
static inline int rk_get_vcpu_priority(void) { return RK_ERROR; }

static inline int rk_create_vcpu_inherited_prio_list(void) { return RK_ERROR; }
static inline int rk_vmpcp_start_gcs(int mode) { return RK_ERROR; }
static inline int rk_vmpcp_finish_gcs(void) { return RK_ERROR; }

static inline int sys_rk_vchannel_register_host(int rd, int cpursv_idx, char *path) { return RK_ERROR; }
static inline int sys_rk_vchannel_register_guest(int cpunum, char *path) { return RK_ERROR; }
static inline int rk_vchannel_send_cmd(void *channel, rk_vchannel_cmd *cmd) { return RK_ERROR; }

static inline void remove_from_pseudo_vcpu_list(struct task_struct *task, cpu_reserve_t cpursv) {}
static inline void pseudo_vcpu_replenish(cpu_reserve_t pseudo_vcpu, int *priority) {}
static inline void pseudo_vcpu_enforce(cpu_reserve_t pseudo_vcpu) {}
#endif /* RK_VIRT_SUPPORT */

static inline bool is_pseudo_vcpu(cpu_reserve_t cpursv)
{
	if (cpursv->pseudo_vcpu_oneshot_exec_time == 0) return false;
	return true;
}

#endif /* RK_VIRT_H */

