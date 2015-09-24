APP1  = rk_vchannel_register_host
APP2  = rk_vchannel_register_guest
APP3  = rk_vchannel_send_cmd
APP4  = create_vm_rset
APP5  = attach_vcpus_to_rset
APP6  = rk_vint_register_pseudo_vcpu

KERNEL_DIR = ../../../

CC = $(CROSS_COMPILE)gcc
CCFLAGS := -I$(KERNEL_DIR) -I$(KERNEL_DIR)/include/rk -DCONFIG_RK -lrt -DARCH_$(ARCH) -D_GNU_SOURCE
CCFLAGS += -O3 

all: 
	$(CC) -o $(APP1)  $(APP1).c $(CCFLAGS) 
	$(CC) -o $(APP2)  $(APP2).c $(CCFLAGS) 
	$(CC) -o $(APP3)  $(APP3).c $(CCFLAGS) 
	$(CC) -o $(APP4)  $(APP4).c $(CCFLAGS) 
	$(CC) -o $(APP5)  $(APP5).c $(CCFLAGS) 
	$(CC) -o $(APP6)  $(APP6).c $(CCFLAGS) 

clean:
	rm -f $(APP1)
	rm -f $(APP2)
	rm -f $(APP3)
	rm -f $(APP4)
	rm -f $(APP5)
	rm -f $(APP6)

