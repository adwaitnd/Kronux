#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define MAX_RESOURCE_SET_NAME_LEN	20	
#define MEGABYTE	(1024*1024UL)

void print_usage()
{
	printf("<usage>: create-mem-reservation -S=<memsize> -P=<policy> -R=<rsvname> -C=<cache> -B=<bank>\n");
	printf("\t <memsize>: Memory reserve size in MBytes (ex, '-S=100': 100 MBytes)\n");
	printf("\t <policy  : Reservation policy, 1: HARD, 2: FIRM\n");
	printf("\t <rsvname>: Resource set name\n");
	printf("\t <cache>  : Cache colors (ex, '-C=-1': all, '-C=0..3': 4 colors, indices from 0 to 3)\n");
	printf("\t <bank>   : Bank colors (ex, '-B=-1': all, '-B=4..5': 2 colors, indices from 4 to 5)\n");
	printf("\t Default P = 1 (HARD reservation)\n");
	printf("\t Default R = RSET<ts> (where <ts> is the current time in milliseconds)\n");
	printf("\t Default C and B = -1 (use all cache and bank colors)\n");
}

int main(int argc, char *argv[]){
	int i;
	int rd;
	struct mem_reserve_attr mem_attr;
	struct timespec now;

	unsigned long mem_size;
	int policy;
	char R[MAX_RESOURCE_SET_NAME_LEN];
	int min_cache_idx = 0, max_cache_idx = 0, nr_cache_colors = 0;
	int min_bank_idx = 0, max_bank_idx = 0, nr_bank_colors = 0;
	policy = RSV_HARD;

	clock_gettime(CLOCK_REALTIME, &now);
	sprintf(R, "RSET%ld", now.tv_nsec / 1000000 + now.tv_sec * 1000);

	if (argc < 2) {
		print_usage();
		return 0;
	}
	for(i=1; i<argc; i++) {
		if (strlen(argv[i]) <= 3) continue;
		if(argv[i][1] == 'S') {
			mem_size = atoi(&argv[i][3]) * MEGABYTE;
		}
		if(argv[i][1] == 'P') {
			policy = atoi(&argv[i][3]);
		}
		if(argv[i][1] == 'R') {
			strncpy(R, &argv[i][3], MAX_RESOURCE_SET_NAME_LEN);
		}
		if(argv[i][1] == 'C') {
			char *p = strtok(&argv[i][3], " .");
			if (p != NULL) min_cache_idx = atoi(p);
			p = strtok(NULL, " .");
			if (p != NULL) max_cache_idx = atoi(p);
			else max_cache_idx = min_cache_idx;
			if (min_cache_idx < 0) nr_cache_colors = 0;
			else nr_cache_colors = max_cache_idx - min_cache_idx + 1;
		}
		if(argv[i][1] == 'B') {
			char *p = strtok(&argv[i][3], " .");
			if (p != NULL) min_bank_idx = atoi(p);
			p = strtok(NULL, " .");
			if (p != NULL) max_bank_idx = atoi(p);
			else max_bank_idx = min_bank_idx;
			if (min_bank_idx < 0) nr_bank_colors = 0;
			else nr_bank_colors = max_bank_idx - min_bank_idx + 1;
		}
	}

	if (policy != RSV_HARD && policy != RSV_FIRM) {
		printf("create-mem-reservation: invalid policy\n");
		return -1;
	}

	printf("Reserve Size    : %lu MBytes (%lu bytes)\n", mem_size / MEGABYTE, mem_size);
	printf("Reserve Policy  : %d (%s)\n", policy, policy == RSV_HARD ? "HARD" : "FIRM");

	mem_attr.mem_size = mem_size;
	mem_attr.swap_size = 0;
	mem_attr.reserve_mode = policy;
	mem_attr.nr_colors = nr_cache_colors;
	for (i = 0; i < mem_attr.nr_colors; i++) 
		mem_attr.colors[i] = min_cache_idx + i;
	mem_attr.nr_bank_colors = nr_bank_colors;
	for (i = 0; i < mem_attr.nr_bank_colors; i++) 
		mem_attr.bank_colors[i] = min_bank_idx + i;

	// rk_resource_set_create(rset, inherit_flag, cleanup_flag)
	// - inherit flag : If it is set, child tasks of the task attached 
	//                  to the resource set are also attached to the resource set.
	// - cleanup_flag : If it is set, the resource set will be automatically 
	//                  deleted when its last task is detached.
	// - cpursv_policy: CPURSV_NO_MIGRATION
	//                  CPURSV_MIGRATION_DEFAULT
	//                  CPURSV_MIGRATION_FORKJOIN
	rd = rk_resource_set_create(R, 1, 1, CPURSV_MIGRATION_DEFAULT); // inherit: true, cleanup: true
	//rd = rk_resource_set_create(R, 1, 0); // inherit: true, cleanup: false

	if (rd < 0) {
		printf("Failed to create a resource set\n");
		return -1;
	}
	if (rk_mem_reserve_create(rd, &mem_attr) < 0) {
		printf("Failed to create MEM reserve... delete resource set\n");
		rk_resource_set_destroy(rd);
		return -1;
	}

	printf("Resource Set Name is %s and Descriptor is %d\n",R, rd);

	return 0;
}
