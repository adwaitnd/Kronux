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
 * rk_virt.c: RK virtualization support
 */

#include <rk/rk_mc.h>
#include <rk/rk_mutex.h>
#include <rk/rk_virt.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/termios.h>
#include <asm/ioctls.h>
#include <linux/un.h>

#ifdef RK_VCHANNEL_SOCKET
#include <linux/socket.h>
#include <linux/net.h>
#endif

int is_virtualized = false;

#ifdef RK_VIRT_SUPPORT

// for guest VMs
DEFINE_PER_CPU(struct file*, vchannel_guest);
DEFINE_PER_CPU(struct task_struct*, vchannel_manager);

#include <linux/kvm_para.h>

#define VERBOSE_RK_VIRT
#ifdef VERBOSE_RK_VIRT
	#define rkvirt_dbg(...) printk(__VA_ARGS__)
#else
	#define rkvirt_dbg(...)
#endif

void rk_virt_init(void)
{
	int cpunum;

	// for guest systems
	if (strcmp(pv_info.name, "KVM") || !pv_info.paravirt_enabled) return;
	if (rk_ping_host_machine() == RK_SUCCESS) {
		is_virtualized = true;
		printk("RK running in a virtual machine\n");
	}
	for_each_online_cpu(cpunum) {
		per_cpu(vchannel_guest, cpunum) = NULL;
		per_cpu(vchannel_manager, cpunum) = NULL;
	}
}

void rk_virt_cleanup(void)
{
	int cpunum;

	// for guest systems
	for_each_online_cpu(cpunum) {
		if (per_cpu(vchannel_manager, cpunum)) {
			kthread_stop(per_cpu(vchannel_manager, cpunum));
			send_sig(SIGKILL, per_cpu(vchannel_manager, cpunum), 1);
			per_cpu(vchannel_manager, cpunum) = NULL;
		}
		if (per_cpu(vchannel_guest, cpunum)) {
			filp_close(per_cpu(vchannel_guest, cpunum), NULL);
			per_cpu(vchannel_guest, cpunum) = NULL;
		}
	}
}


/////////////////////////////////////////////////////////////////////////////
//
// RK virtual channel between host and guest systems 
//
/////////////////////////////////////////////////////////////////////////////

int sys_rk_vchannel_register_host(int rd, int cpursv_idx, char *path)
{
	rk_resource_set_t rset;
	cpu_reserve_t cpursv;
	int ret;
#if defined(RK_VCHANNEL_SOCKET)
	struct sockaddr_un name;
	struct socket *sock;	
#elif defined(RK_VCHANNEL_PIPE)
	struct file *f;
#endif

	ret = RK_ERROR;
	if (is_virtualized) {
		printk("rk_vchannel_register_host: cannot run in a guest vm\n");
		return ret;
	}
	if (rd < 0 || rd >= MAX_RESOURCE_SETS) {
		printk("rk_vchannel_register_host: Invalid resource set id\n");
		return ret;
	}
	if (cpursv_idx < 0 || cpursv_idx >= RK_MAX_ORDERED_LIST) {
		printk("rk_vchannel_register_host: Invalid cpu reserve id\n");
		return ret;
	}
	if (path == NULL || strnlen(path, UNIX_PATH_MAX) == UNIX_PATH_MAX) {
		printk("rk_vchannel_register_host: Invalid path\n");
		return ret;
	}
	rk_sem_down();
	rset = resource_set_descriptor[rd];
	if (rset == NULL) {
		printk("rk_vchannel_register_host: rset %d not available\n", rd);
		goto error_sem_unlock;
	}
	if (rset->cpu_reserves[cpursv_idx] == NULL) {
		printk("rk_vchannel_register_host: rset %d dose not have cpu reserve %d\n", rd, cpursv_idx);
		goto error_sem_unlock;
	}
	cpursv = rset->cpu_reserves[cpursv_idx]->reserve;
	if (cpursv->vchannel_host) {
		printk("rk_vchannel_register_host: vchannel for rset %d cpursv %d already exists\n", rd, cpursv_idx);
		ret = RK_SUCCESS;
		goto error_sem_unlock;
	}
#if defined(RK_VCHANNEL_SOCKET)
	if ((ret = sock_create_kern(PF_LOCAL, SOCK_STREAM, 0, &sock))) {
		printk("rk_vchannel_register_host: cannot create socket\n");
		goto error_sem_unlock;
	}
	name.sun_family = AF_LOCAL;
	strcpy(name.sun_path, path);
	if ((ret = sock->ops->connect(sock, (struct sockaddr*)&name, sizeof(short) + strlen(path), 0))) {
		printk("rk_vchannel_register_host: cannot connect to %s (%d)\n", path, ret);
		sock_release(sock);
		goto error_sem_unlock;
	}
	cpursv->vchannel_host = sock;
#elif defined(RK_VCHANNEL_PIPE)
	f = filp_open(path, O_WRONLY | O_NOCTTY, 0);
	if (IS_ERR(f)) {
		printk("rk_vchannel_register_host: cannot open device\n");
		goto error_sem_unlock;
	}
	cpursv->vchannel_host = f;
#endif 
	rk_sem_up();

	printk("rk_vchannel_register_host: vchannel for rset %d cpursv %d created\n", rd, cpursv_idx);
	
	return RK_SUCCESS;

error_sem_unlock:
	rk_sem_up();
	return ret;
}

int rk_vchannel_manager_fn(void *);
int sys_rk_vchannel_register_guest(int cpunum, char *path)
{
	struct file *f;
	struct task_struct *task;
	struct sched_param par;
	char name[20];
	cpumask_t cpumask;

	if (is_virtualized == FALSE) {
		printk("rk_vchannel_register_guest: cannot run in a host machine\n");
		return RK_ERROR;
	}
	if (path == NULL) {
		printk("rk_vchannel_register_guest: Invalid path\n");
		return RK_ERROR;
	}
	if (cpunum < 0 || cpunum >= num_cpus) {
		printk("rk_vchannel_register_guest: Invalid cpuid number\n");
		return RK_ERROR;
	}
	if (per_cpu(vchannel_guest, cpunum)) {
		printk("rk_vchannel_register_guest: vchannel for cpu %d already exists\n", cpunum);
		return RK_SUCCESS;
	}
	
	f = filp_open(path, O_RDWR | O_NOCTTY, 0);
	if (IS_ERR(f)) {
		printk("rk_vchannel_register_guest: cannot open device\n");
		return RK_ERROR;
	}
	
	sprintf(name, "rk-vchannel/%d", cpunum);
	task = kthread_create(&rk_vchannel_manager_fn, (void*)(long)cpunum, name);
	if (IS_ERR(task)) {
		printk("rk_vchannel_register_guest: cannot create vm manager thread\n");
		filp_close(f, NULL);
		return RK_ERROR;
	}
	per_cpu(vchannel_guest, cpunum) = f;
	per_cpu(vchannel_manager, cpunum) = task;

	cpus_clear(cpumask);
	cpu_set(cpunum, cpumask);
	set_cpus_allowed_ptr(task, &cpumask);

	par.sched_priority = MAX_LINUXRK_PRIORITY;
	sched_setscheduler_nocheck(task, cpu_reserves_kernel_scheduling_policy, &par);
	wake_up_process(task);

	printk("rk_vchannel_register_guest: guest vchannel for cpu %d created\n", cpunum);

	return RK_SUCCESS;
}

int rk_vchannel_manager_fn(void *__cpunum)
{
	rk_vchannel_cmd data;
	struct task_struct *task;
	struct termios options;
	struct file *f;
	int cpunum = (long)__cpunum;
	
	f = per_cpu(vchannel_guest, cpunum);
	if (f == NULL) {
		printk("rk_vchannel_manager(%s): ERROR: vchannel_guest %d\n", current->comm, cpunum);
		per_cpu(vchannel_manager, cpunum) = NULL;
		return 0;
	}
	set_fs(KERNEL_DS);

	if (f->f_op->unlocked_ioctl) {
		f->f_op->unlocked_ioctl(f, TCGETS, (unsigned long)&options);
		options.c_cflag &= ~(CBAUD | PARENB | CSTOPB | CSIZE);
		options.c_cflag |= (B4000000 | CLOCAL | CREAD | CS8);
		options.c_iflag = IGNPAR | IGNBRK;
		options.c_oflag = 0;
		options.c_lflag &= ~(ICANON | ECHO | ISIG);
		options.c_cc[VTIME] = 10; // 10 * 0.1 sec
		options.c_cc[VMIN] = 0;
		f->f_op->unlocked_ioctl(f, TCSETS, (unsigned long)&options);
		printk("rk_vchannel_manager(%s): isa-serial (pid %d)\n", current->comm, current->pid);
	}
	else {
		printk("rk_vchannel_manager(%s): virtio-serial (pid %d)\n", current->comm, current->pid);
	}

	while (!kthread_should_stop()) {
#if defined(RK_VCHANNEL_SOCKET)
		int ret = f->f_op->read(f, (char*)&data, sizeof(data), &f->f_pos);
		if (ret < sizeof(data)) {
#elif defined(RK_VCHANNEL_PIPE)
		char buf[20];
		int ret = f->f_op->read(f, buf, 20, &f->f_pos);
		if (ret > 0) {
			sscanf(buf, "%d,%d", &data.cmd, &data.pid);
		}
		else {
#endif
			// host vchannel hasn't been set yet
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ);
			continue;
		}
		//printk("rk_vchannel_manager(%s): cmd %d, pid %d\n", current->comm, data.cmd, data.pid);
		task = find_task_by_pid_ns(data.pid, &init_pid_ns);
		if (task == NULL) continue;

		switch (data.cmd) {
		case RK_VCHANNEL_CMD_KILL:
			break;
		case RK_VCHANNEL_CMD_MUTEX_WAKEUP:
			task->rk_mutex_wait_on = -1;
			wake_up_process(task);
			break;	
		case RK_VCHANNEL_CMD_VMPCP_LOCK_ACQUIRED:			
			rk_intervm_mutex_vmpcp_lock_acquired(task);
			break;
		case RK_VCHANNEL_CMD_MUTEX_RESTORE_PRIO:
			rk_mutex_restore_priority(task, FALSE);
			break;
		}
	}
	return 0;
}

int rk_vchannel_send_cmd(void *channel, rk_vchannel_cmd *cmd)
{
	mm_segment_t oldfs;
	int ret = RK_SUCCESS;
#if defined(RK_VCHANNEL_SOCKET)
	struct msghdr msg;
	struct iovec iov;
	struct socket *sock;
#elif defined(RK_VCHANNEL_PIPE)
	struct file *f;
	char buf[20];
#endif

	if (channel == NULL || cmd == NULL) return RK_ERROR;

	//printk("rk_vchannel_send_cmd: cmd %d, pid %d\n", cmd->cmd, cmd->pid);
	oldfs = get_fs();
	set_fs(KERNEL_DS);

#if defined(RK_VCHANNEL_SOCKET)
	sock = channel;
	memset(&msg, 0, sizeof(msg));
	memset(&iov, 0, sizeof(iov));

	iov.iov_base = cmd;
	iov.iov_len = sizeof(rk_vchannel_cmd);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (sock_sendmsg(sock, &msg, sizeof(rk_vchannel_cmd)) < 0) {
		printk("rk_vchannel_send_cmd: sendmsg error\n");
		ret = RK_ERROR;
	}
#elif defined(RK_VCHANNEL_PIPE)
	f = channel;
	snprintf(buf, 20, "%d,%d\n", cmd->cmd, cmd->pid);
	if (f->f_op->write(f, buf, 20, &f->f_pos) < 0) {
		printk("rk_vchannel_send_cmd: write error\n");
		ret = RK_ERROR;
	}
#endif
	set_fs(oldfs);
	return ret;
}

// syscall interface for rk_vchannel_send_cmd
int sys_rk_vchannel_send_cmd(int rd, int cpursv_idx, int cmd, int pid)
{
	rk_vchannel_cmd msg;
	rk_resource_set_t rset;
	cpu_reserve_t cpursv;

	if (rd < 0 || rd >= MAX_RESOURCE_SETS) {
		printk("sys_rk_vchannel_send_cmd: invalid rset (%d)\n", rd);
		return -1;
	}
	if (cpursv_idx < 0 || cpursv_idx >= RK_MAX_ORDERED_LIST) {
		printk("sys_rk_vchannel_send_cmd: invalid cpu reserve id\n");
		return -1;
	}

	rset = resource_set_descriptor[rd];
	if (rset == NULL) {
		printk("sys_rk_vchannel_send_cmd: rset %d not available\n", rd);
		return -1; 
	}
	if (rset->cpu_reserves[cpursv_idx] == NULL) {
		printk("sys_rk_vchannel_send_cmd: rset %d dose not have cpu reserve %d\n", rd, cpursv_idx);
		return -1;
	}

	cpursv = rset->cpu_reserves[cpursv_idx]->reserve;
	msg.cmd = cmd;
	msg.pid = pid;

	return rk_vchannel_send_cmd(cpursv->vchannel_host, &msg);
}


////////////////////////////////////////////////////////////////////////////udo 
//
// RK hypercall interface for virtual machines
//
/////////////////////////////////////////////////////////////////////////////

int rk_ping_host_machine(void)
{
	return kvm_hypercall0(NR_rk_ping_host_machine);
}

int rk_get_remaining_time_to_next_vcpu_period(void)
{
	return kvm_hypercall0(NR_rk_get_remaining_time_to_next_vcpu_period);
}

asmlinkage int sys_rk_get_start_of_next_vcpu_period(cpu_tick_t output)
{
	int remaining;

	if (!output || !is_virtualized) return RK_ERROR;

	rk_rdtsc(output);
	remaining = rk_get_remaining_time_to_next_vcpu_period();
	if (remaining < 0) return RK_ERROR;

	*output += remaining;
	return RK_SUCCESS;
}

int rk_send_vm_event(int type, int pid)
{
	return kvm_hypercall2(NR_rk_send_vm_event, type, pid);
}

int rk_get_vcpu_priority(void)
{
	return kvm_hypercall0(NR_rk_get_vcpu_priority);
}

int rk_create_vcpu_inherited_prio_list(void)
{
	return kvm_hypercall0(NR_rk_create_vcpu_inherited_prio_list);
}

int rk_vmpcp_start_gcs(int mode)
{
	return kvm_hypercall1(NR_rk_vmpcp_start_gcs, mode);
}

int rk_vmpcp_finish_gcs(void)
{
	return kvm_hypercall0(NR_rk_vmpcp_finish_gcs);
}

asmlinkage int sys_rk_vchannel(int type, int nr, void *data)
{	
	int ret = RK_ERROR;
	switch (type) {
	case RK_VCHANNEL_SYSCALL_REGISTER_HOST:
		if (data != NULL) {
			long *msg = data;
			ret = sys_rk_vchannel_register_host(nr, msg[0], (char*)msg[1]);
		}
		else {
			ret = RK_ERROR;
		}
		break;
	case RK_VCHANNEL_SYSCALL_REGISTER_GUEST:
		ret = sys_rk_vchannel_register_guest(nr, data);
		break;
	case RK_VCHANNEL_SYSCALL_SEND_CMD:
		if (data != NULL) {
			int *msg = data;
			ret = sys_rk_vchannel_send_cmd(nr, msg[0], msg[1], msg[2]);
		}
		else {
			ret = RK_ERROR;
		}
		break;
	}
	return ret;
}

/////////////////////////////////////////////////////////////////////////////
//
// RK hypercall handlers for host machine
//
/////////////////////////////////////////////////////////////////////////////

int __rk_ping_host_machine_handler(void)
{
	return RK_SUCCESS;
}

int __rk_get_remaining_time_to_next_vcpu_period_handler(void)
{
	rk_resource_set_t rset;
	cpu_reserve_t cpu;
	unsigned long flags;
	cpu_tick_data_t tm_now, tm_next;
	int ret = RK_ERROR;

	rset = current->rk_resource_set;
	if (rset == NULL) return ret;

	raw_spin_lock_irqsave(&rset->lock, flags);
	if (rk_check_task_cpursv(current) == RK_SUCCESS) {
		cpu = __rk_get_task_default_cpursv(current)->reserve;
		rk_rdtsc(&tm_now);
		tm_next = cpu->release_time_of_cur_period + cpu->cpu_period_ticks;
		if (tm_next > tm_now) ret = tm_next - tm_now;
	}
	raw_spin_unlock_irqrestore(&rset->lock, flags);

	return ret;
}

int __rk_send_vm_event_handler(int type, int pid)
{
	rk_event_log_save(type, raw_smp_processor_id(), current->pid, pid, current->rt_priority);
	//printk("vm_event_handler: %d %d %d %d\n", type, raw_smp_processor_id(), current->pid, pid);
	return RK_SUCCESS;
}

int __rk_get_vcpu_priority_handler(void)
{
	return current->rt_priority;
}

// non-interrupt context
int rk_hypercall_handler(unsigned long nr, unsigned long a0, unsigned long a1, 
 			 unsigned long a2, unsigned long a3)
{
	int ret = -KVM_ENOSYS;
	//printk("hypercall_handler: %lu %lu %lu %lu %lu\n", nr, a0, a1, a2, a3);
	switch (nr) {
	case NR_rk_ping_host_machine:
		ret = __rk_ping_host_machine_handler();
		break;
	case NR_rk_get_remaining_time_to_next_vcpu_period:
		ret = __rk_get_remaining_time_to_next_vcpu_period_handler();
		break;
	case NR_rk_send_vm_event:
		ret = __rk_send_vm_event_handler(a0, a1);
		break;
	case NR_rk_get_vcpu_priority:
		ret = __rk_get_vcpu_priority_handler();
		break;
	case NR_rk_create_vcpu_inherited_prio_list:
		ret = rk_mutex_create_inherited_prio_list();
		break;
	case NR_rk_vmpcp_start_gcs:
		ret = __rk_vmpcp_start_gcs_handler(a0);
		break;
	case NR_rk_vmpcp_finish_gcs:
		ret = __rk_vmpcp_finish_gcs_handler();
		break;
	case NR_rk_intervm_mutex_open:
		ret = rk_intervm_mutex_open_handler(a0, a1, a2);
		break;
	case NR_rk_intervm_mutex_lock:
		ret = rk_intervm_mutex_lock_handler(a0, a1, a2, a3);
		break;
	case NR_rk_intervm_mutex_unlock:
		ret = rk_intervm_mutex_unlock_handler(a0, a1, a2, current);
		break;
	case NR_rk_intervm_mutex_unlock_all:
		ret = rk_intervm_mutex_unlock_all_handler(a0, current);
		break;
	case NR_rk_intervm_mutex_destroy:
		ret = rk_intervm_mutex_destroy_handler(a0, a1);
		break;
	case NR_rk_intervm_mutex_trylock:
		ret = rk_intervm_mutex_trylock_handler(a0, a1, a2, a3);
		break;
	case NR_rk_intervm_mutex_remove_from_waitlist:
		ret = rk_intervm_mutex_remove_from_waitlist_handler(a0, a1, current);
		break;
	}
	return ret;
}

#else // RK_VIRT_SUPPORT

asmlinkage int sys_rk_get_start_of_next_vcpu_period(cpu_tick_t output) { return RK_ERROR; }
asmlinkage int sys_rk_vchannel(int type, int nr, void *data) { return RK_ERROR; }

#endif // RK_VIRT_SUPPORT

