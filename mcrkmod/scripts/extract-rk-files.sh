#!/bin/bash
# Linux/RK code backup script for x86_64

cd ../../
rm -rf RK-Backup-`date +%b-%d-%Y`
mkdir RK-Backup-`date +%b-%d-%Y`
cd RK-Backup-`date +%b-%d-%Y`

# Copy RK module 
cp ../mcrkmod . -r

# Copy Makefile and configs
cp ../Makefile .
cp ../config-* .

# Make directories
mkdir arch
cd arch
mkdir x86
cd x86
mkdir syscalls
mkdir kernel
mkdir kvm
cd ../../
mkdir kernel
cd kernel
mkdir sched
cd ..
mkdir mm
mkdir include
cd include
mkdir linux
cd ..
mkdir scripts
mkdir virt
cd virt
mkdir kvm
cd ..

# Copy kernel files
cp ../scripts/checksyscalls.sh scripts/

cp ../include/rk include/ -r
cp ../include/linux/sched.h include/linux/
cp ../include/linux/mm_types.h include/linux/
cp ../include/linux/page-flags.h include/linux/
cp ../include/linux/rmap.h include/linux/

cp ../mm/rmap.c mm/
cp ../mm/vmscan.c mm/
cp ../mm/page_alloc.c mm/
cp ../mm/memory.c mm/
cp ../mm/migrate.c mm/

cp ../kernel/Makefile kernel/
cp ../kernel/fork.c kernel/
cp ../kernel/exit.c kernel/
cp ../kernel/rk.c kernel/
cp ../kernel/sched/core.c kernel/sched/

cp ../arch/Kconfig arch/
cp ../arch/x86/syscalls/syscalltbl.sh arch/x86/syscalls/
cp ../arch/x86/syscalls/syscallhdr.sh arch/x86/syscalls/
cp ../arch/x86/syscalls/syscall_64.tbl arch/x86/syscalls/
cp ../arch/x86/syscalls/syscall_32.tbl arch/x86/syscalls/
cp ../arch/x86/kernel/x8664_ksyms_64.c arch/x86/kernel/
cp ../arch/x86/kernel/syscall_64.c arch/x86/kernel/
cp ../arch/x86/kvm/x86.c arch/x86/kvm/
#cp ../arch/x86/kernel/i386_ksyms_32.c arch/x86/kernel/
#cp ../arch/x86/kernel/syscall_32.c arch/x86/kernel/

cp ../virt/kvm/assigned-dev.c virt/kvm/
cp ../virt/kvm/kvm_main.c virt/kvm/

