#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

/* Number of iteration per task */
#define MAX_RESOURCE_SET_NAME_LEN	20	
#define MILLISEC_TO_NANOSEC		1000000LL
#define NANOSEC_LL			1000000000LL

#define N_VMS 2
#define N_VCPUS 4

int main(){
	int i, j, k;
	int rd[N_VMS];
	struct cpu_reserve_attr cpu_attr;
	struct timespec now;

	int cpuid;	
	long long C, T, D;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	memset(&cpu_attr, 0, sizeof(cpu_attr));

	/* Default values */
	C = 3 * MILLISEC_TO_NANOSEC;
	T = 10 * MILLISEC_TO_NANOSEC;
	D = 10 * MILLISEC_TO_NANOSEC;

	clock_gettime(CLOCK_REALTIME, &now);
	now.tv_sec += 2;
	now.tv_nsec = 0;

	// rk_resource_set_create(rset, inherit_flag, cleanup_flag, cpursv_policy)
	// - inherit flag : If it is set, child tasks of the task attached 
	//                  to the resource set are also attached to the resource set.
	// - cleanup_flag : If it is set, the resource set will be automatically 
	//                  deleted when its last task is detached.
	// - cpursv_policy: CPURSV_NO_MIGRATION
	//                  CPURSV_MIGRATION_DEFAULT
	//                  CPURSV_MIGRATION_FORKJOIN
	for (i = 0; i < N_VMS; i++) {
		sprintf(R, "RSET-VM%d", i + 1);
		rd[i] = rk_resource_set_create(R, FALSE, TRUE, CPURSV_NO_MIGRATION);
		if (rd[i] < 0) {
			printf("Failed to create a resource set\n");
			return -1;
		}
		printf("%s: Resource Set Descriptor is %d\n", R, rd[i]);
	}

	// CPU reserve create
	// VM1: {v1, v3, v5, v7}
	// VM2: {v2, v4, v6, v8}
	// - v8 is the highest priority VCPU, v1 is the lowest one.
	// - Under Deadline Monotinic policy with RK_UNIQUE_PRIORITY_ACROSS_PROCESSORS,
	//   when the deadlines of CPU reserves are the same,
	//   RK assigns higher priorities to CPU reserves that have been created earlier
	for (i = N_VCPUS * N_VMS - 1; i >= 0; i--) {
		cpu_attr.compute_time.tv_sec=(C/NANOSEC_LL);
		cpu_attr.period.tv_sec=(T/NANOSEC_LL);
		cpu_attr.deadline.tv_sec=(D/NANOSEC_LL);
		cpu_attr.blocking_time.tv_sec=0;

		cpu_attr.compute_time.tv_nsec=(C%NANOSEC_LL);
		cpu_attr.period.tv_nsec=(T%NANOSEC_LL);
		cpu_attr.deadline.tv_nsec=(D%NANOSEC_LL);
		cpu_attr.blocking_time.tv_nsec=0;

		cpu_attr.start_time = now;
	
		cpu_attr.reserve_mode.sch_mode = RSV_HARD;
		cpu_attr.reserve_mode.enf_mode = RSV_HARD;
		cpu_attr.reserve_mode.rep_mode = RSV_SOFT;
		cpu_attr.cpunum = i / N_VMS;

		if (rk_cpu_reserve_create(rd[i % N_VMS], &cpu_attr) < 0) {
			printf("Failed to create CPU reserve... delete resource set\n");
			for (j = 0; j < N_VMS; j++) 
				rk_resource_set_destroy(rd[j]);
			return -1;
		}

	}
	return 0;
}
