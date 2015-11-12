# kernel directory (absolute or relative to this directory)
KERNEL_DIR ?= ../..
ARCH ?= arm
# CROSS_COMPILE is relative to kernel directory
CROSS_COMPILE ?= ../dl/gcc-linaro-4.9-2015.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-

all: help

help:
	@echo "Options:"
	@echo "help OR all:    	display help"
	@echo "timeline_module: compile timeline kernel module"
	@echo "clean:          	clean files"

timeline_module: timeline.c Kbuild
	make -C $(KERNEL_DIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)

clean:
	rm -rf *.ko *.mod.c *.order .*.cmd *.o *.symvers

.PHONY: clean help