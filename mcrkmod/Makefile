# Makefile for RK Kernel Module 
ARCH=arm
CROSS_COMPILE=~/Beagle-kernel-workspace/bb-kernel/dl/gcc-linaro-4.9-2015.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-

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

