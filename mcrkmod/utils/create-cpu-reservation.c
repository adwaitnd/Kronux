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

int main(int argc, char *argv[]){
	int i;
	int rd;
	struct cpu_reserve_attr cpu_attr;
	struct timespec now;

	int cpuid;	
	long long C, T, D;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	memset(&cpu_attr, 0, sizeof(cpu_attr));

	/* Default values */
	cpuid = 0;
	C = 200 * MILLISEC_TO_NANOSEC;
	T = 1000 * MILLISEC_TO_NANOSEC;
	D = 1000 * MILLISEC_TO_NANOSEC;

	clock_gettime(CLOCK_REALTIME, &now);
	sprintf(R, "RSET%ld", now.tv_nsec / 1000000 + now.tv_sec * 1000);

	if (argc >= 1) {
		for(i=1; i<argc; i++) {
			if(strlen(argv[i])>3) {
				if(argv[i][1] == 'A') {
					cpuid = atoi(&argv[i][3]);
				}
			}
			if(strlen(argv[i])>3) {
				if(argv[i][1] == 'C') {
					C = atoll(&argv[i][3]);
					C = C * MILLISEC_TO_NANOSEC;
				}
			}
			if(strlen(argv[i])>3) {
				if(argv[i][1] == 'T') {
					T = atoi(&argv[i][3]);
					T = T * MILLISEC_TO_NANOSEC;
				}
			}
			if(strlen(argv[i])>3) {
				if(argv[i][1] == 'D') {
					D = atoi(&argv[i][3]);
					D = D * MILLISEC_TO_NANOSEC;
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
					printf("<usage>: create-cpu-reservation -A=<cpuid> -C=<compute_time> -T=<period> -D=<deadline> -R=<resource_set_name>\n");
					printf("\t Default <cpuid> = 0\n");
					printf("\t Default C = %lld (%lld milliseconds)\n", C / MILLISEC_TO_NANOSEC, C / MILLISEC_TO_NANOSEC);
					printf("\t Default T = %lld (%lld milliseconds)\n", T / MILLISEC_TO_NANOSEC, T / MILLISEC_TO_NANOSEC);
					printf("\t Default D = %lld (%lld milliseconds)\n", D / MILLISEC_TO_NANOSEC, D / MILLISEC_TO_NANOSEC);
					printf("\t Default R = RSET<ts> (where <ts> is the current time in milliseconds)\n");
					printf("\t Resulting Default Utilization: 20 percent\n");
					return 0;
				}
			}	
		}
	}

	printf("Affine CPU   : %d\n", cpuid);
	printf("Compute Time : %lld\n", C);
	printf("Reserve Period  : %lld\n", T);
	printf("Reserve Deadline: %lld\n", D);

	cpu_set_t cpus;
	CPU_ZERO(&cpus);
	CPU_SET(cpuid, &cpus);
	
	cpu_attr.compute_time.tv_sec=(C/NANOSEC_LL);
	cpu_attr.period.tv_sec=(T/NANOSEC_LL);
	cpu_attr.deadline.tv_sec=(D/NANOSEC_LL);
	cpu_attr.blocking_time.tv_sec=0;

	cpu_attr.compute_time.tv_nsec=(C%NANOSEC_LL);
	cpu_attr.period.tv_nsec=(T%NANOSEC_LL);
	cpu_attr.deadline.tv_nsec=(D%NANOSEC_LL);
	cpu_attr.blocking_time.tv_nsec=0;

	cpu_attr.start_time.tv_sec = now.tv_sec + 5;
	cpu_attr.start_time.tv_nsec = 0;
	
	cpu_attr.reserve_mode.sch_mode = RSV_HARD;
	cpu_attr.reserve_mode.enf_mode = RSV_HARD;
	cpu_attr.reserve_mode.rep_mode = RSV_SOFT;
	cpu_attr.cpunum = cpuid;

	sched_setaffinity(getpid(), sizeof(cpus), &cpus);
	
	// rk_resource_set_create(rset, inherit_flag, cleanup_flag, cpursv_policy)
	// - inherit flag : If it is set, child tasks of the task attached 
	//                  to the resource set are also attached to the resource set.
	// - cleanup_flag : If it is set, the resource set will be automatically 
	//                  deleted when its last task is detached.
	// - cpursv_policy: CPURSV_NO_MIGRATION
	//                  CPURSV_MIGRATION_DEFAULT
	//                  CPURSV_MIGRATION_FORKJOIN
	rd = rk_resource_set_create(R, TRUE, TRUE, CPURSV_MIGRATION_DEFAULT); // inherit: true, cleanup: true

	if (rd < 0) {
		printf("Failed to create a resource set\n");
		return -1;
	}
	if (rk_cpu_reserve_create(rd, &cpu_attr) < 0) {
		printf("Failed to create CPU reserve... delete resource set\n");
		rk_resource_set_destroy(rd);
		return -1;
	}

        printf("Resource Set Name is %s and Descriptor is %d\n",R, rd);

	return 0;
}
