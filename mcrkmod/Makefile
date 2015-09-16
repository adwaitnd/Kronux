# Makefile for RK Kernel Module 

KDIR = ../

obj-m += rk.o
rk-objs := rt_process.o rk_sched.o cpu_reserve.o mem_reserve.o resource_set.o misc.o timer.o rk_trace.o rk_procfs.o rk_mutex.o rk_virt.o

EXTRA_CFLAGS += -DCONFIG_RK -D__RK__ -DPREEMPT_FIX

all:	
	make -C $(KDIR) SUBDIRS=$(PWD) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules 

global:
	make -C $(KDIR) SUBDIRS=$(PWD) EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DRK_GLOBAL_SCHED" modules 

clean:		
	make -C $(KDIR) SUBDIRS=$(PWD) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" clean	

