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

	int cpuid = 1;	
	long long C, T, D;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	memset(&cpu_attr, 0, sizeof(cpu_attr));
	
	/* Default values */
	C = 50 * MILLISEC_TO_NANOSEC;
	//C = 5 * MILLISEC_TO_NANOSEC;
	T = 100 * MILLISEC_TO_NANOSEC;
	D = 100 * MILLISEC_TO_NANOSEC;
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
	cpu_attr.cpunum = 0;

	//sched_setaffinity(getpid(), sizeof(cpus), &cpus);
	
	sleep(1);
	rd = rk_resource_set_create(R, 1, 1, CPURSV_MIGRATION_FORKJOIN);

	rk_cpu_reserve_create(rd, &cpu_attr);
	////////////
	C = 10 * MILLISEC_TO_NANOSEC;
	cpu_attr.compute_time.tv_sec=(C/NANOSEC_LL);
	cpu_attr.compute_time.tv_nsec=(C%NANOSEC_LL);
	//D = 90 * MILLISEC_TO_NANOSEC;
	cpu_attr.deadline.tv_nsec=(D%NANOSEC_LL);

	cpu_attr.cpunum = 1;
	rk_cpu_reserve_create(rd, &cpu_attr);
	////////////

	//D = 80 * MILLISEC_TO_NANOSEC;
	cpu_attr.deadline.tv_nsec=(D%NANOSEC_LL);
	cpu_attr.cpunum = 2;
	rk_cpu_reserve_create(rd, &cpu_attr);

	////////////
	//D = 70 * MILLISEC_TO_NANOSEC;
	cpu_attr.deadline.tv_nsec=(D%NANOSEC_LL);
	cpu_attr.cpunum = 3;
	rk_cpu_reserve_create(rd, &cpu_attr);
	/*
	rd = rk_resource_set_create(R, 1, 1, CPURSV_MIGRATION_DEFAULT);
	rk_cpu_reserve_create(rd, &cpu_attr);
	*/

	int parent_pid = getpid();

	rk_resource_set_attach_process(rd, getpid(), NULL);

	fork();
	fork();

	int count = 0;
	int curpid = getpid();
	printf("pid : %d\n", getpid());
	while (1) {
		if (curpid == parent_pid) {
			//for (i = 0; i < 20; i++) {
			for (i = 0; i < 20; i++) {
				for (j = 0; j < 100000; j++) {
					asm volatile ("nop");
				}
			}
		}
		else {
			//for (i = 0; i < 100; i++) {
			for (i = 0; i < 80; i++) {
				for (j = 0; j < 100000; j++) {
					asm volatile ("nop");
				}
			}
		}
		rt_wait_for_next_period();
		continue;
		/*
		if (modpid == 0) {
			usleep(30000);
			for (i = 0; i < 1000; i++) {
				for (j = 0; j < 100000; j++) {
					asm volatile ("nop");
				}
			}
		}
		else {
			for (i = 0; i < 1000; i++) {
				for (j = 0; j < 100000; j++) {
					asm volatile ("nop");
				}
			}
		}*/
		while (1);
		for (i = 0; i < 1000; i++) {
			for (j = 0; j < 100000; j++) {
				asm volatile ("nop");
			}
		}

		rt_wait_for_next_period();
		/*	
		unsigned long long ret1;
		unsigned long ret2, ret3;
		rk_getcpursv_prev_used_ticks(rd, &ret1);
		rk_getcpursv_min_utilization(rd, &ret2);
		rk_getcpursv_max_utilization(rd, &ret3);
		unsigned long long tm1, tm2;
		rk_get_current_time(&tm1);
		rk_get_start_of_current_period(&tm2);
		//printf("%d - used:%llu, min:%lu, max:%lu, cur:%llu, start:%llu\n", getpid(), ret1, ret2, ret3, tm1, tm2);
		rt_wait_for_next_period();
		*/
	}
	
	return 0;
}
