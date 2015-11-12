CROSS_COMPILE ?= ../../../dl/gcc-linaro-4.9-2015.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-

all: help

help:
	@echo "options:"
	@echo "help: display help"
	@echo "clean: clean compiled files"
	@echo "syscall_check: compile syscall_check"

syscall_check: syscall_check.o
	$(CROSS_COMPILE)gcc $^ -o $@.out

%.o: %.c
	$(CROSS_COMPILE)gcc $^ -c -o $@

clean:
	rm -f *.out *.o

.PHONY: help clean