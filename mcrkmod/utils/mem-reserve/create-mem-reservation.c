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
	int rd;
	struct mem_reserve_attr mem_attr;
	struct timespec now;

	unsigned long mem_size;
	int policy;
	char R[MAX_RESOURCE_SET_NAME_LEN];


	/* Default values */
	policy = RSV_HARD;
	mem_size = 1 * MEGABYTE; // 100MB

	clock_gettime(CLOCK_REALTIME, &now);
	sprintf(R, "RSET%ld", now.tv_nsec / 1000000 + now.tv_sec * 1000);

	if (argc >= 1) {
		for(i=1; i<argc; i++) {
			if(strlen(argv[i])>3) {
				if(argv[i][1] == 'S') {
					mem_size = atoi(&argv[i][3]) * MEGABYTE;
				}
			}
			if(strlen(argv[i])>3) {
				if(argv[i][1] == 'P') {
					policy = atoi(&argv[i][3]);
				}
			}
			if(strlen(argv[i])>3) {
				if(argv[i][1] == 'R') {
					strncpy(R, &argv[i][3], MAX_RESOURCE_SET_NAME_LEN);
				}
			}	
			if(strlen(argv[i])>=2) {
				if(argv[i][1] == '?' 
                    || argv[i][1] == 'h' 
                    || argv[i][1] == 'H') {
					printf("<usage>: create-mem-reservation -A=<cpuid> \
                        -S=<memory_size_megabytes> \
                        -P=<reservation_policy, 1:HARD, 2:FIRM> \
                        -R=<resource_set_name>\n");
					printf("\t Default S = 100 (100 megabytes)\n");
					printf("\t Default P = 1   (HARD reservation)\n");
					printf("\t Default R = RSET<ts> (where <ts> is \
                        the current time in milliseconds)\n");
					return 0;
				}
			}	
		}
	}

	if (policy != RSV_HARD && policy != RSV_FIRM) {
		printf("create-mem-reservation: invalid policy\n");
		return -1;
	}

	printf("Reserve Size    : %lu bytes\n", mem_size);
	printf("Reserve Policy  : %d (%s)\n", policy, policy == RSV_HARD ? "HARD" : "FIRM");

	mem_attr.mem_size = mem_size;
	mem_attr.swap_size = 0;
	mem_attr.reserve_mode = policy;
	mem_attr.nr_colors = 1;
	mem_attr.colors[0] = 0;

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
