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
 * rk_procfs.c: shows the information on resource sets and reserves in procfs.
 */
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <rk/rk_mc.h>

static struct proc_dir_entry *procfs_rk_dir;
static struct proc_dir_entry *procfs_rk_task_dir;
static struct proc_dir_entry *procfs_rk_rset_dir;

static int rk_procfs_read_rset_info(char *page, char **start, off_t off, int count, int *eof, void *data);
static int rk_procfs_read_rset_tasklist(char *page, char **start, off_t off, int count, int *eof, void *data);
static int rk_procfs_read_rk_sem_debug(char *page, char **start, off_t off, int count, int *eof, void *data);

void rk_procfs_init(void)
{
	struct proc_dir_entry *entry;

	procfs_rk_dir = create_proc_entry("rk", S_IFDIR, 0);
	procfs_rk_task_dir = create_proc_entry("task", S_IFDIR, procfs_rk_dir);
	procfs_rk_rset_dir = create_proc_entry("rset", S_IFDIR, procfs_rk_dir);

	entry = create_proc_entry("rksem", S_IFREG | S_IRUGO, procfs_rk_dir);
	entry->read_proc = rk_procfs_read_rk_sem_debug;
}

void rk_procfs_cleanup(void)
{
	remove_proc_entry("task", procfs_rk_dir);
	remove_proc_entry("rset", procfs_rk_dir);
	remove_proc_entry("rksem", procfs_rk_dir);
	remove_proc_entry("rk", NULL);
}

// proc/rk/rset/<rset_id>
void rk_procfs_rset_create(rk_resource_set_t rset)
{
	struct proc_dir_entry *entry;
	char buf[16];

	sprintf(buf, "%d", rset->rd_entry);
	rset->rs_proc_dir = create_proc_entry(buf, S_IFDIR, procfs_rk_rset_dir);

	// proc/rk/rset/<rset_id>/tasklist
	entry = create_proc_entry("tasklist", S_IFREG | S_IRUGO, rset->rs_proc_dir);
	entry->read_proc = rk_procfs_read_rset_tasklist;
	entry->data = rset;
	
	// proc/rk/rset/<rset_id>/info
	entry = create_proc_entry("info", S_IFREG | S_IRUGO, rset->rs_proc_dir);
	entry->read_proc = rk_procfs_read_rset_info;
	entry->data = rset;
}

void rk_procfs_rset_destroy(rk_resource_set_t rset)
{
	char buf[16];
	
	remove_proc_entry("tasklist", rset->rs_proc_dir);
	remove_proc_entry("info",  rset->rs_proc_dir);

	sprintf(buf, "%d", rset->rd_entry);
	remove_proc_entry(buf, procfs_rk_rset_dir);
}

// proc/rk/task/<task_id>
static int rk_procfs_read_task_rset(char *page, char **start, off_t off, int count, int *eof, void *data);
void rk_procfs_rset_attach_process(rk_resource_set_t rset, int pid)
{
	struct proc_dir_entry *entry;
	char buf[16];

	sprintf(buf, "%d", pid);
	entry = create_proc_entry(buf, S_IFREG | S_IRUGO, procfs_rk_task_dir);
	entry->read_proc = rk_procfs_read_task_rset;
	entry->data = (void*)(unsigned long)rset->rd_entry;
}

void rk_procfs_rset_detach_process(int pid)
{
	char buf[16];
	
	sprintf(buf, "%d", pid);
	remove_proc_entry(buf, procfs_rk_task_dir);
}

static int rk_procfs_read_reserve(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	rk_reserve_t rsv = (rk_reserve_t)data;
	if (rsv == NULL || rsv->operations->read_proc == NULL) {
		printk("rk_procfs_reserve_read: no read_proc available\n");
		return 0;
	}
	return rsv->operations->read_proc(rsv, page);
}

// proc/rk/rset/<rset_id>/<reserve_type>
void rk_procfs_reserve_create(rk_reserve_t rsv, int index)
{
	rk_resource_set_t rset = rsv->parent_resource_set;
	char buf[16];

	//printk("rk_procfs_reserve_create: rset(%d) rsv type(%d)\n", rset->rd_entry, rsv->reservation_type);
	switch (rsv->reservation_type) {
		case RSV_CPU:
			sprintf(buf, "cpu%d", index);
			break;
		case RSV_MEM:
			sprintf(buf, "mem");
			break;
		default:
			printk("  - type not supported\n");
			return;
	}

	rsv->rsv_proc_entry = create_proc_entry(buf, S_IFREG | S_IRUGO, rset->rs_proc_dir);
	rsv->rsv_proc_entry->read_proc = rk_procfs_read_reserve;
	rsv->rsv_proc_entry->data = rsv;
}

void rk_procfs_reserve_destroy(rk_reserve_t rsv, int index)
{
	rk_resource_set_t rset = rsv->parent_resource_set;
	char buf[16];

	//printk("rk_procfs_reserve_destroy: rset(%d) rsv type(%d)\n", rset->rd_entry, rsv->reservation_type);
	switch (rsv->reservation_type) {
		case RSV_CPU:
			sprintf(buf, "cpu%d", index);
			break;
		case RSV_MEM:
			sprintf(buf, "mem");
			break;
		default:
			printk("  - type not supported\n");
			return;
	}
	remove_proc_entry(buf, rset->rs_proc_dir);
}

static int rk_procfs_read_rset_tasklist(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	rk_resource_set_t rset = data;
	struct task_struct *task;
	char *p = page;

	rk_sem_down();
	if (rset == NULL || rset->task_list.next == NULL) {
		rk_sem_up();
		return 0;
	}

	list_for_each_entry(task, &rset->task_list, rk_resource_set_link) {
		p += sprintf(p, "%d\n", task->pid);
	}

	rk_sem_up();
	return (p - page);
}

static int rk_procfs_read_rset_info(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	rk_resource_set_t rset = data;
	char *p = page;
	struct task_struct *task;

	rk_sem_down();
	if (rset == NULL) {
		rk_sem_up();
		return 0;
	}
	p += sprintf(p, "rk_inherit   : %d\n", (int)rset->rk_inherit);
	p += sprintf(p, "rk_cleanup   : %d\n", (int)rset->rk_auto_cleanup);
	p += sprintf(p, "nr_cpu_rsvs  : %d\n", (int)rset->nr_cpu_reserves); 

	list_for_each_entry(task, &rset->task_list, rk_resource_set_link) {
		int cpursv_index = rk_get_task_current_cpursv_index(task);
		p += sprintf(p, "pid %d, state %ld, curcpursv %d, rk_cannot_schedule %d\n", task->pid, task->state, cpursv_index, task->rk_cannot_schedule);
	}

	rk_sem_up();
	return (p - page);
}

static int rk_procfs_read_task_rset(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int rd_entry = (unsigned long)data;
	char *p = page;

	p += sprintf(p, "%d\n", rd_entry);
	return (p - page);
}


int rk_sem_debug_pid = 0;
char rk_sem_debug_fn[100] = {0,};

static int rk_procfs_read_rk_sem_debug(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *p = page;

	p += sprintf(p, "holder pid: %d\n", rk_sem_debug_pid);
	p += sprintf(p, "holder function: %s\n", rk_sem_debug_fn);

	return (p - page);
}

