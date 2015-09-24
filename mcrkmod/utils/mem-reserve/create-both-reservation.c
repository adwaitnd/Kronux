#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define MAX_RESOURCE_SET_NAME_LEN	20	
#define MEGABYTE	(1024*1024UL)
#define MILLISEC_TO_NANOSEC		1000000LL

#define NANOSEC_LL			1000000000LL

int main(int argc, char *argv[]){
	int i;
	int rd;
	struct cpu_reserve_attr cpu_attr;
	struct mem_reserve_attr mem_attr;
	struct timespec now;

	int cpuid;	
	long long C, T, D;
	unsigned long mem_size;
	int policy;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	memset(&cpu_attr, 0, sizeof(cpu_attr));
	memset(&mem_attr, 0, sizeof(mem_attr));

	/* Default values */
	cpuid = 0;
	C = 10 * MILLISEC_TO_NANOSEC;
	T = 33 * MILLISEC_TO_NANOSEC;
	D = 33 * MILLISEC_TO_NANOSEC;
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

	printf("Affine CPU   : %d\n", cpuid);
	printf("Compute Time : %lld\n", C);
	printf("Reserve Period  : %lld\n", T);
	printf("Reserve Deadline: %lld\n", D);
	if (policy != RSV_HARD && policy != RSV_FIRM) {
		printf("create-mem-reservation: invalid policy\n");
		return -1;
	}

	printf("Reserve Size    : %lu bytes\n", mem_size);
	printf("Reserve Policy  : %d (%s)\n", policy, policy == RSV_HARD ? "HARD" : "FIRM");

	mem_attr.mem_size = mem_size;
	mem_attr.swap_size = 0;
	mem_attr.reserve_mode = policy;

	cpu_set_t cpus;
	CPU_ZERO(&cpus);
	CPU_SET(cpuid, &cpus);
	
	cpu_attr.compute_time.tv_sec=(C/NANOSEC_LL);
	cpu_attr.period.tv_sec=(T/NANOSEC_LL);
	cpu_attr.deadline.tv_sec=(D/NANOSEC_LL);
	cpu_attr.blocking_time.tv_sec=0;
	cpu_attr.start_time.tv_sec=0;

	cpu_attr.compute_time.tv_nsec=(C%NANOSEC_LL);
	cpu_attr.period.tv_nsec=(T%NANOSEC_LL);
	cpu_attr.deadline.tv_nsec=(D%NANOSEC_LL);
	cpu_attr.blocking_time.tv_nsec=0;
	cpu_attr.start_time.tv_nsec=0;

	
	cpu_attr.reserve_mode.sch_mode = RSV_HARD;
	cpu_attr.reserve_mode.enf_mode = RSV_HARD;
	cpu_attr.reserve_mode.rep_mode = RSV_SOFT;
	cpu_attr.cpunum = cpuid;

	sched_setaffinity(getpid(), sizeof(cpus), &cpus);
	
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
	if (rk_cpu_reserve_create(rd, &cpu_attr) < 0) {
		printf("Failed to create CPU reserve... delete resource set\n");
		rk_resource_set_destroy(rd);
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
