APP1=tasks_for_vm
APP2=create_cpursvs
APP3=attach_vcpus
APP4=wakeup_vm_tasks
APP5=set_rt_prio
APP6=reg_host_vchannel

KERNEL_DIR = ../../..

CC = $(CROSS_COMPILE)gcc
CCFLAGS := -I$(KERNEL_DIR) -I$(KERNEL_DIR)/include/rk -DCONFIG_RK -lrt -DARCH_$(ARCH) -D_GNU_SOURCE
CCFLAGS += -O3

all: 
	$(CC) -o $(APP1)1 $(APP1).c $(CCFLAGS) -DVM1
	$(CC) -o $(APP1)2 $(APP1).c $(CCFLAGS) -DVM2
	$(CC) -o $(APP2)  $(APP2).c $(CCFLAGS) 
	$(CC) -o $(APP3)  $(APP3).c $(CCFLAGS) 
	$(CC) -o $(APP4)  $(APP4).c $(CCFLAGS) 
	$(CC) -o $(APP5)  $(APP5).c $(CCFLAGS) 
	$(CC) -o $(APP6)  $(APP6).c $(CCFLAGS) 

clean:
	rm -f $(APP1)1
	rm -f $(APP1)2
	rm -f $(APP2)
	rm -f $(APP3)
	rm -f $(APP4)
	rm -f $(APP5)
	rm -f $(APP6)

