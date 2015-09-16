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
#include <rk/timespec.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/termios.h>
#include <asm/ioctls.h>
#include <linux/interrupt.h>

#ifdef RK_VCHANNEL_SOCKET
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/un.h>
#endif

int is_virtualized = false;

#ifdef RK_VIRT_SUPPORT

// for guest VMs
DEFINE_PER_CPU(struct file*, vchannel_guest);
DEFINE_PER_CPU(struct task_struct*, vchannel_manager);

#include <linux/kvm_host.h>
#include <linux/kvm_para.h>

#define VERBOSE_RK_VIRT
#ifdef VERBOSE_RK_VIRT
	#define rkvirt_dbg(...) printk(__VA_ARGS__)
#else
	#define rkvirt_dbg(...)
#endif

// pseudo-VCPU list
pseudo_vcpu_list_entry pseudo_vcpu_list[RK_MAX_PSEUDO_VCPU_LIST];
raw_spinlock_t pseudo_vcpu_list_lock;
int pseudo_vcpu_list_n = 0;

void rk_virt_init(void)
{
	int cpunum;

	// for guest systems
	if (strcmp(pv_info.name, "KVM") == 0 && pv_info.paravirt_enabled) {
		if (rk_ping_host_machine() == RK_SUCCESS) {
			is_virtualized = true;
			printk("RK running in a virtual machine\n");
		}
		for_each_online_cpu(cpunum) {
			per_cpu(vchannel_guest, cpunum) = NULL;
			per_cpu(vchannel_manager, cpunum) = NULL;
		}
	}
	// pseudo-VCPU list
	raw_spin_lock_init(&pseudo_vcpu_list_lock);
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
	int ret, i;
	unsigned long flags;
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
	if (is_pseudo_vcpu(cpursv)) {
		printk("rk_vchannel_register_host: vchannel cannot be registered on a pseudo-VCPU\n");
		goto error_sem_unlock;
	}
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
	// vchannel_host for pseudo-VCPUs
	raw_spin_lock_irqsave(&pseudo_vcpu_list_lock, flags);
	for (i = 0; i < pseudo_vcpu_list_n; i++) {
		rk_reserve_t rsv;
		rsv = rk_get_task_current_cpursv(pseudo_vcpu_list[i].vcpu);
		if (rsv == NULL) continue;
		if (rsv->reserve != cpursv) continue;
		pseudo_vcpu_list[i].pseudo_vcpu_cpursv->vchannel_host = cpursv->vchannel_host;
	}
	raw_spin_unlock_irqrestore(&pseudo_vcpu_list_lock, flags);

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
// RK pseudo-VCPU registration for vINT (virtual interrupt handling)
//
/////////////////////////////////////////////////////////////////////////////

void remove_from_pseudo_vcpu_list(struct task_struct *task, cpu_reserve_t cpursv)
{
	unsigned long flags;
	int i, j;
	raw_spin_lock_irqsave(&pseudo_vcpu_list_lock, flags);

	for (i = 0; i < pseudo_vcpu_list_n;) {
		if ((task && pseudo_vcpu_list[i].vcpu == task) 
		   || (cpursv && pseudo_vcpu_list[i].pseudo_vcpu_cpursv == cpursv)) {
		   	for (j = i + 1; j < pseudo_vcpu_list_n; j++) {
				pseudo_vcpu_list[i] = pseudo_vcpu_list[j];
			}
			pseudo_vcpu_list_n--;
		}
		else {
			i++;
		}
	}
	raw_spin_unlock_irqrestore(&pseudo_vcpu_list_lock, flags);
}

int rk_kvm_assigned_dev_intr_handler(int host_irq_no, int guest_irq_no, void *dev)
{
#ifndef RK_GLOBAL_SCHED
	unsigned long flags;
	int ret = RK_SUCCESS, i;
	cpu_reserve_t pseudo_vcpu;
	struct task_struct *vcpu_task;
	struct sched_param par;
	bool need_to_wakeup = FALSE;
	int pseudo_vcpu_cpursv_idx;
	rk_resource_set_t rset;

	// Don't need to get a lock here: safe enough
	if (pseudo_vcpu_list_n == 0) return ret;

	local_irq_save(flags);
	raw_spin_lock(&pseudo_vcpu_list_lock);
	for (i = 0; i < pseudo_vcpu_list_n; i++) {
		if (pseudo_vcpu_list[i].host_irq_no == host_irq_no
		    && pseudo_vcpu_list[i].guest_irq_no == guest_irq_no) {
		    	
			break;
		}
	}
	if (i == pseudo_vcpu_list_n) {
		raw_spin_unlock(&pseudo_vcpu_list_lock);
		goto error_irq_restore;
	}

	// Found the corresponding pseudo-VCPU
	if (dev) pseudo_vcpu_list[i].dev = dev;
	vcpu_task = pseudo_vcpu_list[i].vcpu;
	pseudo_vcpu = pseudo_vcpu_list[i].pseudo_vcpu_cpursv;
	pseudo_vcpu_cpursv_idx = pseudo_vcpu_list[i].pseudo_vcpu_cpursv_idx;

	raw_spin_unlock(&pseudo_vcpu_list_lock);

	if (!vcpu_task) goto error_irq_restore;
	rset = vcpu_task->rk_resource_set;
	if (!rset) goto error_irq_restore;

	raw_spin_lock(&rset->lock);

	// Check if this handler is running on the same physical core as the VCPU
	if (raw_smp_processor_id() != rk_get_task_current_cpursv_cpunum(vcpu_task)) {
		printk("rk_kvm_assigned_dev_intr_handler: error - current core %d != vcpu's phycore %d\n",
			raw_smp_processor_id(), rk_get_task_current_cpursv_cpunum(vcpu_task));
		raw_spin_unlock(&rset->lock);
		goto error_irq_restore;
	}
	
	// Increase available ticks of pseudo-VCPU by intr_exec_time
	// - When the budget of pseudo-VCPU reaches cpu_time_ticks (maximum budget),
	//   host interrupt will be disabled and no interrupt will be raised until next period
	pseudo_vcpu->avail_ticks_in_cur_period += pseudo_vcpu->pseudo_vcpu_oneshot_exec_time;
	if (pseudo_vcpu->avail_ticks_in_cur_period > pseudo_vcpu->cpu_time_ticks) {
		pseudo_vcpu->avail_ticks_in_cur_period = pseudo_vcpu->cpu_time_ticks;
	}
	pseudo_vcpu->rsv->reservation_state &= ~RSV_IS_DEPLETED;

	// Switch VCPU to the pseudo-VCPU
	need_to_wakeup = (vcpu_task->rk_cannot_schedule & RK_TASK_UNSCHEDULABLE) ? true : false;
	vcpu_task->rk_cpursv_list->cur_idx = pseudo_vcpu_cpursv_idx;

	//printk("intx: pseudo-VCPU avail %lld\n", pseudo_vcpu->avail_ticks_in_cur_period);
	raw_spin_unlock(&rset->lock);
	local_irq_restore(flags);

	// Change pseudo-VCPU priority here
	par.sched_priority = rk_global_ceiling_prio(pseudo_vcpu->cpu_priority_index);
	if (vcpu_task->rt_priority < par.sched_priority) {
		sched_setscheduler_nocheck(vcpu_task, cpu_reserves_kernel_scheduling_policy, &par);
	}

	// Wakeup vcpu if needed
	if (need_to_wakeup) wake_up_process(vcpu_task);
	return ret;

error_irq_restore:
	local_irq_restore(flags);
	return ret;
#else
	return RK_SUCCESS;
#endif
}

int rk_kvm_assigned_dev_eoi_handler(int host_irq_no, int guest_irq_no)
{
	// TODO: support nested vINT management
	return RK_SUCCESS;
}


int rk_kvm_vm_ioctl_assign_irq_handler(int host_irq_no, int guest_irq_no, void* dev)
{
	unsigned long flags;
	int i;
	raw_spin_lock_irqsave(&pseudo_vcpu_list_lock, flags);
	for (i = 0; i < pseudo_vcpu_list_n; i++) {
		if (pseudo_vcpu_list[i].host_irq_no == host_irq_no
		    && pseudo_vcpu_list[i].guest_irq_no == guest_irq_no) {
			pseudo_vcpu_list[i].dev = dev;
			//printk("assign irq: h %d g %d\n", host_irq_no, guest_irq_no);
			break;
		}
	}
	raw_spin_unlock_irqrestore(&pseudo_vcpu_list_lock, flags);
	return RK_SUCCESS;
}

int rk_kvm_vm_ioctl_deassign_dev_irq_handler(void* dev)
{
	unsigned long flags;
	int i;
	raw_spin_lock_irqsave(&pseudo_vcpu_list_lock, flags);
	for (i = 0; i < pseudo_vcpu_list_n; i++) {
		if (pseudo_vcpu_list[i].dev == dev) {
			pseudo_vcpu_list[i].dev = NULL;
			//printk("deassign irq: h %d g %d\n", pseudo_vcpu_list[i].host_irq_no, pseudo_vcpu_list[i].guest_irq_no);
			break;
		}
	}
	raw_spin_unlock_irqrestore(&pseudo_vcpu_list_lock, flags);
	return RK_SUCCESS;
}

asmlinkage int sys_rk_vint_register_pseudo_vcpu(pid_t vcpu_pid, pseudo_vcpu_attr_t attr)
{
#ifndef RK_GLOBAL_SCHED
	struct task_struct *vcpu_task;
	rk_resource_set_t rset;
	cpu_reserve_t vcpu, pseudo_vcpu;
	cpu_tick_data_t intr_exec_time_ns;
	int ret = RK_ERROR;
	int i;
	unsigned long flags;

	if (is_virtualized) {
		printk("rk_vint_register_pseudo_vcpu: cannot run in a guest vm\n");
		return ret;
	}
	vcpu_task = find_task_by_pid_ns(vcpu_pid, &init_pid_ns);
	if (vcpu_task == NULL) {
		printk("rk_vint_register_pseudo_vcpu: cannot find VCPU pid %d\n", vcpu_pid);
		return ret;
	}
	if (attr == NULL) {
		printk("rk_vint_register_pseudo_vcpu: pseudo_vcpu_attr must be provided\n");
		return ret;
	}
	if (attr->pseudo_vcpu_cpursv < 0 || attr->pseudo_vcpu_cpursv >= RK_MAX_ORDERED_LIST) {
		printk("rk_vint_register_pseudo_vcpu: Invalid pseudo_vcpu_cpursv\n");
		return ret;
	}
	if (attr->host_irq_no < 0 || attr->host_irq_no >= NR_IRQS) {
		printk("rk_vint_register_pseudo_vcpu: Invalid host_irq_no\n");
		return ret;
	}
	if (attr->guest_irq_no < 0 || attr->guest_irq_no >= NR_IRQS) {
		printk("rk_vint_register_pseudo_vcpu: Invalid guest_irq_no\n");
		return ret;
	}
	intr_exec_time_ns = timespec2nano(attr->intr_exec_time);
	if (intr_exec_time_ns <= 0) {
		printk("rk_vint_register_pseudo_vcpu: Invalid intr_exec_time\n");
		return ret;
	}
	rk_sem_down();
	if (rk_check_task_cpursv(vcpu_task) == RK_ERROR) { 
		printk("rk_vint_register_pseudo_vcpu: VCPU pid %d does not have cpursv\n", vcpu_pid);
		goto error_sem_unlock;
	} 
	rset = vcpu_task->rk_resource_set;
	raw_spin_lock_irqsave(&rset->lock, flags);

	if (rset->cpu_reserves[attr->pseudo_vcpu_cpursv] == NULL) {
		printk("rk_vint_register_pseudo_vcpu: Resource set of VCPU %d dose not have pseudo_vcpu_cpursv %d\n", vcpu_pid, attr->pseudo_vcpu_cpursv);
		goto error_spin_unlock;
	}
	vcpu = __rk_get_task_default_cpursv(vcpu_task)->reserve;
	pseudo_vcpu = rset->cpu_reserves[attr->pseudo_vcpu_cpursv]->reserve;
	if (is_pseudo_vcpu(vcpu)) {
		printk("rk_vint_register_pseudo_vcpu: Error. VCPU pid %d is a pseudo-VCPU\n", vcpu_pid);
		goto error_spin_unlock;
	}
	if (is_pseudo_vcpu(pseudo_vcpu)) {
		printk("rk_vint_register_pseudo_vcpu: pseudo_vcpu_cpursv %d is already registered as a pseudo-VCPU\n", attr->pseudo_vcpu_cpursv);
		goto error_spin_unlock;
	}
	if (vcpu_task->rk_cpursv_list == NULL) {
		printk("rk_vint_register_pseudo_vcpu: VCPU pid %d does not have rk_cpursv_list (no cpu reserve?)\n", vcpu_pid);
		goto error_spin_unlock;
	}
	if (vcpu_task->rk_cpursv_list->n == RK_MAX_ORDERED_LIST) {
		printk("rk_vint_register_pseudo_vcpu: VCPU pid %d cannot have more cpu reserves\n", vcpu_pid);
		goto error_spin_unlock;
	}
	if (vcpu->cpu_res_attr.cpunum != pseudo_vcpu->cpu_res_attr.cpunum) {
		printk("rk_vint_register_pseudo_vcpu: VCPU (cpunum: %d) and Pseudo-VCPU (cpunum: %d) should be assigned to the same physical core\n",
			vcpu->cpu_res_attr.cpunum, pseudo_vcpu->cpu_res_attr.cpunum);
		goto error_spin_unlock;
	}

	// Add pseudo-VCPU to pseudo_vcpu_list 
	raw_spin_lock(&pseudo_vcpu_list_lock);
	if (pseudo_vcpu_list_n == RK_MAX_PSEUDO_VCPU_LIST) {
		printk("rk_vint_register_pseudo_vcpu: cannot add more pseudo-VCPUs (pseudo_vcpu_list is full)\n");
		raw_spin_unlock(&pseudo_vcpu_list_lock);
		goto error_spin_unlock;
	}
	i = pseudo_vcpu_list_n++;
	pseudo_vcpu_list[i].host_irq_no = attr->host_irq_no;
	pseudo_vcpu_list[i].guest_irq_no = attr->guest_irq_no;
	pseudo_vcpu_list[i].vcpu = vcpu_task;
	pseudo_vcpu_list[i].pseudo_vcpu_cpursv = pseudo_vcpu;
	pseudo_vcpu_list[i].pseudo_vcpu_cpursv_idx = vcpu_task->rk_cpursv_list->n; // index of vcpu's rk_cpursv_list->elem[]
	pseudo_vcpu_list[i].dev = NULL;
	pseudo_vcpu_list[i].intr_enforced= false;
	raw_spin_unlock(&pseudo_vcpu_list_lock);
	
	// Add pseudo-VCPU to the VCPU task's rk_cpursv_list
	i = vcpu_task->rk_cpursv_list->n++;
	vcpu_task->rk_cpursv_list->elem[i] = attr->pseudo_vcpu_cpursv;

	// vchannel for pseudo-VCPU
	pseudo_vcpu->vchannel_host = vcpu->vchannel_host;
	
	pseudo_vcpu->pseudo_vcpu_oneshot_exec_time = intr_exec_time_ns;

	raw_spin_unlock_irqrestore(&rset->lock, flags);
	rk_sem_up();

	printk("rk_vint_register_pseudo_vcpu: Pseudo-VCPU %d is registered to VCPU pid %d\n", attr->pseudo_vcpu_cpursv, vcpu_pid);
	
	return RK_SUCCESS;

error_spin_unlock:
	raw_spin_unlock_irqrestore(&rset->lock, flags);
	
error_sem_unlock:
	rk_sem_up();
	return ret;
#else
	printk("rk_vint_register_pseudo_vcpu: does not support global scheduling\n");
	return RK_ERROR;
#endif
}

// Should be called with rset->lock held (local irq disabled)
// - Called by cpu_reserve.c::cpu_reserve_replenish()
void pseudo_vcpu_replenish(cpu_reserve_t cpu, int *priority)
{
	rk_reserve_t rsv = cpu->rsv;
	int i;

	// Pseudo-VCPU prioritization
	*priority = rk_global_ceiling_prio(*priority);

	// Adjust available budget
	if ((rsv->reservation_state & RSV_IS_RUNNING)) {
		cpu->avail_ticks_in_cur_period = cpu->pseudo_vcpu_oneshot_exec_time;
	}
	else {
		cpu->avail_ticks_in_cur_period = 0;
	}

	// Check if interrupt has been enforced
	raw_spin_lock(&pseudo_vcpu_list_lock);
	for (i = 0; i < pseudo_vcpu_list_n; i++) {
		pseudo_vcpu_list_entry *e = &pseudo_vcpu_list[i];
		if (e->pseudo_vcpu_cpursv != cpu) continue;
		if (e->intr_enforced == true) {
			e->intr_enforced = false;
			enable_irq(e->host_irq_no);
		}
		break;
	}
	raw_spin_unlock(&pseudo_vcpu_list_lock);
}

// Should be called with rset->lock held (local irq disabled)
// - Called by cpu_reserve.c::cpu_reserve_enforce()
void pseudo_vcpu_enforce(cpu_reserve_t cpu)
{
	rk_reserve_t rsv = cpu->rsv;
	int i;

	if (cpu->avail_ticks_in_cur_period < cpu->cpu_time_ticks) {
		rsv->reservation_state &= ~RSV_IS_DEPLETED;
		return;
	}
	
	// Rsv Depleted -> Enforce interrupt
	rsv->reservation_state |= RSV_IS_DEPLETED;
	raw_spin_lock(&pseudo_vcpu_list_lock);
	for (i = 0; i < pseudo_vcpu_list_n; i++) {
		pseudo_vcpu_list_entry *e = &pseudo_vcpu_list[i];
		if (e->pseudo_vcpu_cpursv != cpu) continue;
		// Note: As we consider directly-assigned devices (host IRQ not shared), 
		// the virtual interrupt enforcement and the delayed delivery of enforced 
		// virtual interrupts can be simply implemented by disabling host IRQ.
		if (e->intr_enforced == false) {
			e->intr_enforced = true;
			disable_irq_nosync(e->host_irq_no);
		}
		break;
	}
	raw_spin_unlock(&pseudo_vcpu_list_lock);
}

///////////////////////////////////////////////////////////////////////////// 
//
// RK hypercall interface for virtual machines
//
/////////////////////////////////////////////////////////////////////////////

int rk_ping_host_machine(void)
{
	return kvm_hypercall0(HYP_rk_ping_host_machine);
}

int rk_get_remaining_time_to_next_vcpu_period(void)
{
	return kvm_hypercall0(HYP_rk_get_remaining_time_to_next_vcpu_period);
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
	return kvm_hypercall2(HYP_rk_send_vm_event, type, pid);
}

int rk_get_vcpu_priority(void)
{
	return kvm_hypercall0(HYP_rk_get_vcpu_priority);
}

int rk_create_vcpu_inherited_prio_list(void)
{
	return kvm_hypercall0(HYP_rk_create_vcpu_inherited_prio_list);
}

int rk_vmpcp_start_gcs(int mode)
{
	return kvm_hypercall1(HYP_rk_vmpcp_start_gcs, mode);
}

int rk_vmpcp_finish_gcs(void)
{
	return kvm_hypercall0(HYP_rk_vmpcp_finish_gcs);
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
int rk_hypercall_handler(void *vcpuptr, unsigned long nr, unsigned long a0, unsigned long a1, 
 			 unsigned long a2, unsigned long a3)
{
	int ret = -KVM_ENOSYS;
	struct kvm_vcpu *vcpu = vcpuptr;
	//printk("hypercall_handler: %lu %lu %lu %lu %lu\n", nr, a0, a1, a2, a3);
	switch (nr) {
	case HYP_rk_ping_host_machine:
		ret = __rk_ping_host_machine_handler();
		break;
	case HYP_rk_get_remaining_time_to_next_vcpu_period:
		ret = __rk_get_remaining_time_to_next_vcpu_period_handler();
		break;
	case HYP_rk_send_vm_event:
		ret = __rk_send_vm_event_handler(a0, a1);
		break;
	case HYP_rk_get_vcpu_priority:
		ret = __rk_get_vcpu_priority_handler();
		break;
	case HYP_rk_create_vcpu_inherited_prio_list:
		ret = rk_mutex_create_inherited_prio_list();
		break;
	case HYP_rk_vmpcp_start_gcs:
		ret = __rk_vmpcp_start_gcs_handler(a0);
		break;
	case HYP_rk_vmpcp_finish_gcs:
		ret = __rk_vmpcp_finish_gcs_handler();
		break;
	case HYP_rk_intervm_mutex_open:
		ret = rk_intervm_mutex_open_handler(a0, a1, a2);
		break;
	case HYP_rk_intervm_mutex_lock:
		ret = rk_intervm_mutex_lock_handler(a0, a1, a2, a3, false);
		break;
	case HYP_rk_intervm_mutex_unlock:
		ret = rk_intervm_mutex_unlock_handler(a0, a1, a2, current);
		break;
	case HYP_rk_intervm_mutex_unlock_all:
		ret = rk_intervm_mutex_unlock_all_handler(a0, current);
		break;
	case HYP_rk_intervm_mutex_destroy:
		ret = rk_intervm_mutex_destroy_handler(a0, a1);
		break;
	case HYP_rk_intervm_mutex_trylock:
		ret = rk_intervm_mutex_trylock_handler(a0, a1, a2, a3, false);
		break;
	case HYP_rk_intervm_mutex_remove_from_waitlist:
		ret = rk_intervm_mutex_remove_from_waitlist_handler(a0, a1, current);
		break;
	case HYP_rk_intervm_mutex_lock_inv_prio: 
		// for guest OSs with inverse task-priority schemes (ex, lower value -> higher priority)
		ret = rk_intervm_mutex_lock_handler(a0, a1, a2, a3, true);
		break;
	case HYP_rk_intervm_mutex_trylock_inv_prio:
		// for guest OSs with inverse task-priority schemes (ex, lower value -> higher priority)
		ret = rk_intervm_mutex_trylock_handler(a0, a1, a2, a3, true);
		break;
	case HYP_rk_vcoloring:
		ret = rk_mem_reserve_assign_guest_task_colors(vcpu, a0, a1);
		break;
	}
	return ret;
}

#else // RK_VIRT_SUPPORT

asmlinkage int sys_rk_get_start_of_next_vcpu_period(cpu_tick_t output) { return RK_ERROR; }
asmlinkage int sys_rk_vchannel(int type, int nr, void *data) { return RK_ERROR; }
asmlinkage int sys_rk_vint_register_pseudo_vcpu(pid_t vcpu_pid, pseudo_vcpu_attr_t usr_attr) { return RK_ERROR; }
int rk_kvm_assigned_dev_intr_handler(int host_irq_no, int guest_irq_no, void* dev) { return RK_ERROR; }
int rk_kvm_assigned_dev_eoi_handler(int host_irq_no, int guest_irq_no) { return RK_ERROR; }
int rk_kvm_vm_ioctl_assign_irq_handler(int host_irq_no, int guest_irq_no, void* dev) { return RK_ERROR; }
int rk_kvm_vm_ioctl_deassign_dev_irq_handler(void* dev) { return RK_ERROR; }

#endif // RK_VIRT_SUPPORT

