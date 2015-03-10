#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <rk_api.h>

/* Number of iteration per task */
#define MAX_RESOURCE_SET_NAME_LEN	20	

#define MILLISEC_TO_NANOSEC		1000000LL

#define NANOSEC_LL			1000000000LL

#define NUM_PROCESS	32

int main(int argc, char *argv[]){
	int i;
	int rd;
	int ret;
	struct cpu_reserve_attr cpu_attr;
	struct timespec now;
	int pid[NUM_PROCESS] = {0,};

	int cpuid;	
	long long C, T, D;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	memset(&cpu_attr, 0, sizeof(cpu_attr));
	
	/* Default values */
	for (cpuid = 0; cpuid < 2; cpuid++) {
	//for (cpuid = 0; cpuid < 1; cpuid++) {
		ret = fork();
		if (ret != 0) continue;

		pid[cpuid] = getpid();
		if (cpuid == 0) {
			C = 5 * MILLISEC_TO_NANOSEC;
			T = 50 * MILLISEC_TO_NANOSEC;
			D = 50 * MILLISEC_TO_NANOSEC;
		}
		else {
			C = 110 * MILLISEC_TO_NANOSEC;
			T = 1000 * MILLISEC_TO_NANOSEC;
			D = 1000 * MILLISEC_TO_NANOSEC;
		}
		//T = D = (100 - cpuid) * MILLISEC_TO_NANOSEC;

		clock_gettime(CLOCK_REALTIME, &now);
		//sprintf(R, "RSET%ld", now.tv_nsec / 1000000 + now.tv_sec * 1000);
		sprintf(R, "RSET%d", getpid());

		//printf("Affine CPU   : %d\n", cpuid);
		//printf("Compute Time : %lld\n", C);
		//printf("Reserve Period  : %lld\n", T);
		//printf("Reserve Deadline: %lld\n", D);

		cpu_set_t cpus;
		CPU_ZERO(&cpus);
		CPU_SET(0, &cpus);
		
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
		cpu_attr.cpunum = 0;

		//sched_setaffinity(getpid(), sizeof(cpus), &cpus);
		
		rd = rk_resource_set_create(R, 1, 1, CPURSV_MIGRATION_DEFAULT);
		rk_cpu_reserve_create(rd, &cpu_attr);
		rk_resource_set_attach_process(rd, getpid(), NULL);

		//while (1); 
		//return;
		execv("../busyloop", NULL);
		//rt_wait_for_next_period();

		//printf("Resource Set Name is %s and Descriptor is %d\n",R, rd);
	}
	sleep(2);
	printf("## Enter any key to kill all child processes\n");
	getchar();
	for (cpuid = 0; cpuid < NUM_PROCESS; cpuid++) {
		kill(pid[cpuid], 9);
	}

	return 0;
}
