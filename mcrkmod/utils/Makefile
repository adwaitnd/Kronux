APP1  = busyloop
APP2  = monitor
APP3  = create-cpu-reservation
APP4  = attach-pid-to-reservation
APP5  = detach-pid-from-reservation
#APP6  = rk_trace_set
#APP7  = rk_trace_get
APP8  = destroy-resource-set
APP9  = cpursv-profile
APP10 = cpursv-task-profile
APP11 = cpursv-both-profile
APP12 = rk-enforcement-signal-example
APP13 = create-multi-cpu-reservation
APP14 = debug_rd
APP15 = task-profile
APP16 = task-trace
APP17 = rk_event_log_set
APP18 = rk_event_log_get
APP19 = add-cpu-reserve-to-rset

KERNEL_DIR = ../../

#ifndef ARCH
#ARCH := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ \
#                               -e s/arm.*/arm/ -e s/sa110/arm/ \
#			       -e s/s390x/s390/ -e s/parisc64/parisc/ \
#			       -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
#			       -e s/sh[234].*/sh/)
#endif

CC = $(CROSS_COMPILE)gcc
CCFLAGS := -I$(KERNEL_DIR) -I$(KERNEL_DIR)/include/rk -DCONFIG_RK -lrt -DARCH_$(ARCH) -D_GNU_SOURCE
CCFLAGS += -O3 

all: 
	$(CC) -o  $(APP1)  $(APP1).c $(CCFLAGS) 
	$(CC) -o  $(APP2)  $(APP2).c $(CCFLAGS) 
	$(CC) -o  $(APP3)  $(APP3).c $(CCFLAGS) 
	$(CC) -o  $(APP4)  $(APP4).c $(CCFLAGS) 
	$(CC) -o  $(APP5)  $(APP5).c $(CCFLAGS) 
	#$(CC) -o  $(APP6)  $(APP6).c $(CCFLAGS) 
	#$(CC) -o  $(APP7)  $(APP7).c $(CCFLAGS) 
	$(CC) -o  $(APP8)  $(APP8).c $(CCFLAGS) 
	$(CC) -o  $(APP9)  $(APP9).c $(CCFLAGS) 
	$(CC) -o $(APP10) $(APP10).c $(CCFLAGS) 
	$(CC) -o $(APP11) $(APP11).c $(CCFLAGS) 
	$(CC) -o $(APP12) $(APP12).c $(CCFLAGS) 
	$(CC) -o $(APP13) $(APP13).c $(CCFLAGS) 
	$(CC) -o $(APP14) $(APP14).c $(CCFLAGS) 
	$(CC) -o $(APP15) $(APP15).c $(CCFLAGS) 
	$(CC) -o $(APP16) $(APP16).c $(CCFLAGS) 
	$(CC) -o $(APP17) $(APP17).c $(CCFLAGS) 
	$(CC) -o $(APP18) $(APP18).c $(CCFLAGS) 
	$(CC) -o $(APP19) $(APP19).c $(CCFLAGS) 

clean:
	rm -f  $(APP1)
	rm -f  $(APP2)
	rm -f  $(APP3)
	rm -f  $(APP4)
	rm -f  $(APP5)
	#rm -f  $(APP6)
	#rm -f  $(APP7)
	rm -f  $(APP8)
	rm -f  $(APP9)
	rm -f $(APP10)
	rm -f $(APP11)
	rm -f $(APP12)
	rm -f $(APP13)
	rm -f $(APP14)
	rm -f $(APP15)
	rm -f $(APP16)
	rm -f $(APP17)
	rm -f $(APP18)
	rm -f $(APP19)

