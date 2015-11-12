# kernel directory (absolute or relative to this directory)
KERNEL_DIR ?= ../..
ARCH ?= arm
# CROSS_COMPILE is relative to kernel directory
CROSS_COMPILE ?= ../dl/gcc-linaro-4.9-2015.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-

all: help

help:
	@echo "Options:"
	@echo "help OR all: display help"
	@echo "modules: 	compile kernel module"
	@echo "clean:       clean files"

modules: Kbuild
	make -C $(KERNEL_DIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean

.PHONY: clean help