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

#define NUM_PROCESS	64
//#define NUM_PROCESS	32

int main(int argc, char *argv[]){
	int i, j;
	int rd;
	int ret;
	struct cpu_reserve_attr cpu_attr;
	struct timespec now;
	int pid[NUM_PROCESS] = {0,};

	int cpuid = 0;	
	long long C, T, D;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	memset(&cpu_attr, 0, sizeof(cpu_attr));

	/* Default values */
	C = (500 + cpuid) * MILLISEC_TO_NANOSEC;
	//C = 5 * MILLISEC_TO_NANOSEC;
	T = (1000 + cpuid * 20) * MILLISEC_TO_NANOSEC;
	D = (1000 + cpuid * 20) * MILLISEC_TO_NANOSEC;
	//T = D = (100 - cpuid) * MILLISEC_TO_NANOSEC;

	clock_gettime(CLOCK_REALTIME, &now);
	//sprintf(R, "RSET%ld", now.tv_nsec / 1000000 + now.tv_sec * 1000);
	sprintf(R, "RSET%d", getpid());

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
	//cpu_attr.reserve_mode.rep_mode = RSV_SOFT;
	cpu_attr.reserve_mode.rep_mode = RSV_HARD;
	cpu_attr.cpunum = cpuid;

	//sched_setaffinity(getpid(), sizeof(cpus), &cpus);
	
	rd = rk_resource_set_create(R, 1, 1, CPURSV_MIGRATION_DEFAULT);
	rk_resource_set_attach_process(rd, getpid(), NULL);

	rk_cpu_reserve_create(rd, &cpu_attr);
	printf("pid %d\n", getpid());
	while (1);

	rt_wait_for_next_period();
	int count = 0;
	while (1) {
		for (i = 0; i < 100; i++) {
		//for (i = 0; i < 5000000; i++) {
			for (j = 0; j < 100000; j++) {
				asm volatile ("nop");
			}
		}
		count = (count + 1) % 2;
		if (count == 1) 
			usleep(100 * 1000);
		for (i = 0; i < 100; i++) {
			for (j = 0; j < 100000; j++) {
				asm volatile ("nop");
			}
		}
		/*rt_wait_for_next_period();
		*/
		unsigned long long ret1;
		unsigned long ret2, ret3;
		rk_getcpursv_prev_used_ticks(rd, &ret1);
		rk_getcpursv_min_utilization(rd, &ret2);
		rk_getcpursv_max_utilization(rd, &ret3);
		printf("%d - used:%llu, min:%lu, max:%lu, ", getpid(), ret1, ret2, ret3);
		unsigned long long tm1, tm2;
		rk_get_current_time(&tm1);
		rk_get_start_of_current_period(&tm2);
		printf("cur:%llu, start:%llu\n", tm1, tm2);
		rt_wait_for_next_period();
	}
	
	return 0;
}
