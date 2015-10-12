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
 * rk_mutex.h: RK mutex with real-time synchronization protocols
 */ 

#ifndef RK_MUTEX_H
#define RK_MUTEX_H

#include <linux/spinlock.h>

#define MAX_RK_MUTEX_DESC		128
#define MAX_RK_MUTEX_NESTED_LEVEL	16

#define MAX_RK_INTERVM_MUTEX_DESC	32
#define MAX_RK_INTERVM_MUTEX_WAIT_LIST	32

enum rk_mutex_types {
	RK_MUTEX_PIP,
	RK_MUTEX_PCP,
	RK_MUTEX_HLP,
	RK_MUTEX_MPCP,
	RK_MUTEX_VMPCP,
	__NR_RK_MUTEX_TYPES,
};

// RK mutex data structure
struct rk_mutex {
	enum rk_mutex_types type;
	int mode;
	raw_spinlock_t lock;
	int key, mid;
	int count;
	int ceiling;
	struct task_struct *owner;
	struct list_head wait_list;
	struct list_head online_mutex_link;
	struct list_head owner_link;
};
typedef struct rk_mutex* rk_mutex_t; 

typedef struct {
	struct task_struct *vcpu;
	int task_id;
	int task_prio;
} rk_intervm_node;

// RK inter-vm mutex data structure
struct rk_intervm_mutex {
	enum rk_mutex_types type;
	int mode;
	raw_spinlock_t lock;
	int key, mid;
	int count;
	//int ceiling;
	//struct task_struct *owner;
	rk_intervm_node owner;
	//struct list_head wait_list;
	int wait_list_size;
	rk_intervm_node wait_list[MAX_RK_INTERVM_MUTEX_WAIT_LIST];
	//struct list_head online_mutex_link;
	//struct list_head owner_link;
};
typedef struct rk_intervm_mutex* rk_intervm_mutex_t; 

#define RK_INTERVM_LOCK_ACQUIRED	0
#define RK_INTERVM_LOCK_RECURSIVE	1
#define RK_INTERVM_LOCK_WAITLISTED	2
#define RK_INTERVM_LOCK_FAILED		3

#define RK_INTERVM_UNLOCK_DONE		0
#define RK_INTERVM_UNLOCK_RECURSIVE	1

#define RK_INTERVM_MUTEX_WAIT_ON_OFFSET	1000

int __rk_vmpcp_start_gcs_handler(int mode);
int __rk_vmpcp_finish_gcs_handler(void);
int rk_mutex_create_inherited_prio_list(void);
int rk_intervm_mutex_open_handler(int type, int key, int mode);
int rk_intervm_mutex_destroy_handler(int type, int key);
int rk_intervm_mutex_lock_handler(int type, int mid, int task_id, int task_prio, int is_inv_prio);
int rk_intervm_mutex_trylock_handler(int type, int mid, int task_id, int task_prio, int is_inv_prio);
int rk_intervm_mutex_unlock_handler(int type, int mid, int owner_task, struct task_struct *owner_vcpu);
int rk_intervm_mutex_unlock_all_handler(int task_id, struct task_struct *vcpu);
int rk_intervm_mutex_remove_from_waitlist_handler(int mid, int task_id, struct task_struct *vcpu);
void rk_intervm_mutex_vmpcp_lock_acquired(struct task_struct *task);
void rk_mutex_restore_priority(struct task_struct *task, int need_indirect);

#define task_inherited_prio(task)	\
	(task->rk_mutex_inherited_prio_list[task->rk_mutex_nested_level])

#define task_original_prio(task) 	\
	(task->rk_mutex_inherited_prio_list[0])

#define rk_global_ceiling(task) 	\
	(BASE_LINUXRK_PRIORITY + 1 + (task_original_prio(task) - cpu_reserves_current_min_priority))

#define rk_global_ceiling_prio(curprio) \
	(BASE_LINUXRK_PRIORITY + 1 + ((curprio) - cpu_reserves_current_min_priority))
#endif

