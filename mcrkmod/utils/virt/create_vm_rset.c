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

void print_usage()
{
	printf("<usage>: create_vm_rset <n_vcpus>\n");
	printf("\t <n_vcpus>: number of vcpus (cpu reserves)\n");
}

int main(int argc, char *argv[]){
	int i;
	int rd;
	struct cpu_reserve_attr cpu_attr;
	struct timespec now;

	int cpuid;	
	long long C, T, D;
	char R[MAX_RESOURCE_SET_NAME_LEN];
	int N_VCPUS;

	if (argc < 2) {
		print_usage();
		return -1;
	}
	N_VCPUS = atoi(argv[1]);
	if (N_VCPUS <= 0 || N_VCPUS > 8) {
		printf("error: invalid n_vcpus value\n");
		return -1;
	}

	memset(&cpu_attr, 0, sizeof(cpu_attr));

	/* Default values */
	//C = 3000 * MILLISEC_TO_NANOSEC;
	//T = 10000 * MILLISEC_TO_NANOSEC;
	//D = 10000 * MILLISEC_TO_NANOSEC;
	C = 2 * MILLISEC_TO_NANOSEC;
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
	sprintf(R, "RSET-VM");
	rd = rk_resource_set_create(R, TRUE, TRUE, NO_DEFAULT_CPURSV);
	if (rd < 0) {
		printf("Failed to create a resource set\n");
		return -1;
	}

	for (i = 0; i < N_VCPUS; i++) {

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
		cpu_attr.cpunum = i;

		if (rk_cpu_reserve_create(rd, &cpu_attr) < 0) {
			printf("Failed to create CPU reserve... delete resource set\n");
			rk_resource_set_destroy(rd);
			return -1;
		}

	}
	printf("Resource Set Descriptor: %d (%d cpu reserves)\n", rd, N_VCPUS);
	return 0;
}
