APP1=mutex_pip
APP2=mutex_hlp
APP3=mutex_pcp1
APP4=mutex_pcp2
APP5=mutex_mpcp
APP6=mutex_vmpcp
APP7=mutex_vmpcp2
APP8=mutex_vmpcp_intervm

KERNEL_DIR = ../../..

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
	$(CC) -o $(APP7)  $(APP7).c $(CCFLAGS) 
	$(CC) -o $(APP8)  $(APP8).c $(CCFLAGS) 

clean:
	rm -f $(APP1)
	rm -f $(APP2)
	rm -f $(APP3)
	rm -f $(APP4)
	rm -f $(APP5)
	rm -f $(APP6)
	rm -f $(APP7)
	rm -f $(APP8)

