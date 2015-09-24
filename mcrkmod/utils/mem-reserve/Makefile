APP1  = create-mem-reservation
APP2  = show_vminfo
APP3  = show_mem_reserve_info
APP4  = set_eviction_lock
APP5  = create-both-reservation
APP6  = page-alloc-time
APP7  = traverse_page_table

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
	$(CC) -o $(APP7)  $(APP7).c $(CCFLAGS) 

clean:
	rm -f $(APP1)
	rm -f $(APP2)
	rm -f $(APP3)
	rm -f $(APP4)
	rm -f $(APP5)
	rm -f $(APP6)
	rm -f $(APP7)

