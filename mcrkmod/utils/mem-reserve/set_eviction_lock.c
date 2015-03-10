#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

int main(int argc, char *argv[]){
	int pid;
	unsigned long vaddr;
	unsigned long size;

	if (argc < 2) {
		printf("eviction_lock: needs pid, start_addr, size\n");
		return -1;
	}
	pid = atoi(argv[1]);
	vaddr = strtoul(argv[2], NULL, 16);
	size = strtoul(argv[3], NULL, 10);

	printf("eviction_lock: pid %d - vaddr:%lx, size:%lu\n", pid, vaddr, size);
	
	rk_mem_reserve_eviction_lock(pid, vaddr, size, TRUE);

	return 0;
}
