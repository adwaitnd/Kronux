APP1  = test
APP2  = test-multitask
APP3  = test-clone
APP4  = test-clone-manual

KERNEL_DIR = ../../../

CC = $(CROSS_COMPILE)gcc
CCFLAGS := -I$(KERNEL_DIR) -I$(KERNEL_DIR)/include/rk -DCONFIG_RK -lrt -DARCH_$(ARCH) -D_GNU_SOURCE
CCFLAGS += -O3

all: 
	$(CC) -o $(APP1)  $(APP1).c $(CCFLAGS) 
	$(CC) -o $(APP2)  $(APP2).c $(CCFLAGS) 
	$(CC) -o $(APP3)  $(APP3).c $(CCFLAGS) 
	$(CC) -o $(APP4)  $(APP4).c $(CCFLAGS) 

clean:
	rm -f $(APP1)
	rm -f $(APP2)
	rm -f $(APP3)
	rm -f $(APP4)

