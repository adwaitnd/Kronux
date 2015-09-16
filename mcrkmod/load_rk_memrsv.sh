#!/bin/sh
# 
# RK module load with memory reservation
#

# Memory pool size for memory reservation (in MBytes)
DEFAULT_MEM_SIZE=1024
MEM_SIZE=$1
if [ -z $MEM_SIZE ]; then
	MEM_SIZE=$DEFAULT_MEM_SIZE
fi

echo "Loading RK (mem_size=$MEM_SIZE)"
sudo insmod rk.ko mem_size=$MEM_SIZE
if [ $? -eq 0 ]; then
	echo "Success"
	exit 0
else
	echo "Failed"
	exit 1
fi

