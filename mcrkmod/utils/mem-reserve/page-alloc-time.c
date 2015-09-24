#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define MAX_RESOURCE_SET_NAME_LEN	20	
#define MEGABYTE	(1024*1024UL)

int main(int argc, char *argv[]){
	int i;
	int pid;

	int rd;
	struct mem_reserve_attr mem_attr;
	struct timespec now;
	unsigned long mem_size;
	int policy;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	if (argc < 2) {
		return -1;
	}

	/* Default values */
	policy = RSV_HARD;
	mem_size = 1 * MEGABYTE; // 100MB

	clock_gettime(CLOCK_REALTIME, &now);
	sprintf(R, "RSET%ld", now.tv_nsec / 1000000 + now.tv_sec * 1000);

	printf("Reserve Size    : %lu bytes\n", mem_size);
	printf("Reserve Policy  : %d (%s)\n", policy, policy == RSV_HARD ? "HARD" : "FIRM");

	mem_attr.mem_size = 150 * 1024 * 1024;
	mem_attr.swap_size = 0;
	mem_attr.reserve_mode = policy;

	rd = rk_resource_set_create(R, 1, 1, CPURSV_MIGRATION_DEFAULT);
	rk_mem_reserve_create(rd, &mem_attr);
	rk_resource_set_attach_process(rd, getpid(), NULL);

	pid = atoi(argv[1]);
	printf("test: pid %d - %d pages\n", getpid(), pid);
	
	for (i = 0; i < 10; i++) {
		rk_mem_reserve_do_alloc_test(pid);
	}

	return 0;
}
