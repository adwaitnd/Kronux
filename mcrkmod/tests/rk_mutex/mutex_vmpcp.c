#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <rk_api.h>

/* Number of iteration per task */
#define MAX_RESOURCE_SET_NAME_LEN	20	
#define MICROSEC_TO_NANOSEC		1000LL
#define MILLISEC_TO_NANOSEC		1000000LL
#define NANOSEC_LL			1000000000LL

#define NUM_TASKS 	3
#define TRACE_BUF_SIZE	100

#define DELTA (30 * MICROSEC_TO_NANOSEC)
void busyloop(long long duration_usec)
{
	int i, j;
	for (i = 0; i < duration_usec; i++) {
		for (j = 0; j < 3400; j++) // for i7-2600
			asm volatile ("nop");
	}
}

int main(int argc, char *argv[]){
	int i;
	int rd[NUM_TASKS];
	int ret;
	struct cpu_reserve_attr cpu_attr;
	int pid[NUM_TASKS] = {0,};

	int cpuid;	
	long long C, T, D;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	int mid, key = 60;
	unsigned long long tm, start_time;

	memset(&cpu_attr, 0, sizeof(cpu_attr));

	if (rk_get_start_of_next_vcpu_period(&tm) == RK_ERROR) {
		printf("Error: rk_get_start_of_next_vcpu_period\n");
		return -1;
	}
	// base start time: next vcpu start period + 3sec
	tm += 3 * NANOSEC_LL;
	
	// create three resource sets
	// - i=0(low) : Core 0, offset=0, C=4(1,2,1), T=1000
	// - i=1(med) : Core 0, offset=2, C=2, T=1000
	// - i=2(high): Core 1, offset=1, C=4(1,2,1), T=1000
	for (i = 0; i < NUM_TASKS; i++) {
		C = 100 * MILLISEC_TO_NANOSEC;
		T = 1000 * MILLISEC_TO_NANOSEC;
		D = T - 10 * i;

		if (i == 0) {
			start_time = tm;
			cpuid = 0;
		}
		if (i == 1) {
			start_time = tm + 2 * MILLISEC_TO_NANOSEC;
			cpuid = 0;
		}
		if (i == 2) {
			start_time = tm + 1 * MILLISEC_TO_NANOSEC;
			cpuid = 1;
		}
		sprintf(R, "RSET_%d", i);

		cpu_attr.cpunum = cpuid;
		cpu_attr.start_time.tv_sec = start_time / NANOSEC_LL;
		cpu_attr.start_time.tv_nsec = start_time % NANOSEC_LL;

		cpu_attr.compute_time.tv_sec=(C/NANOSEC_LL);
		cpu_attr.period.tv_sec=(T/NANOSEC_LL);
		cpu_attr.deadline.tv_sec=(D/NANOSEC_LL);
		cpu_attr.blocking_time.tv_sec=0;

		cpu_attr.compute_time.tv_nsec=(C%NANOSEC_LL);
		cpu_attr.period.tv_nsec=(T%NANOSEC_LL);
		cpu_attr.deadline.tv_nsec=(D%NANOSEC_LL);
		cpu_attr.blocking_time.tv_nsec=0;
		
		cpu_attr.reserve_mode.sch_mode = RSV_HARD;
		cpu_attr.reserve_mode.enf_mode = RSV_HARD;
		cpu_attr.reserve_mode.rep_mode = RSV_SOFT;

		rd[i] = rk_resource_set_create(R, 1, 1, CPURSV_MIGRATION_DEFAULT);
		rk_cpu_reserve_create(rd[i], &cpu_attr);
	}

	// create three tasks
	for (i = 0; i < NUM_TASKS; i++) {
		ret = fork();
		if (ret != 0) {
			pid[i] = ret;
			continue;
		}
		rk_resource_set_attach_process(rd[i], getpid(), NULL);

		// wait for the cpursv to be activated
		rt_wait_for_next_period();

		// now, cpursv is activated -> rt priority available
		mid = rk_vmpcp_mutex_open(key, MTX_CREATE);
		if (mid == RK_ERROR) {
			printf("Error: rk_vmpcp_mutex_open\n");
			return -1;
		}
		rk_event_log_set(getpid());
		fprintf(stderr, "%d\n", getpid());

		while (1) {
			struct sched_param par;
			rt_wait_for_next_period();

			if (i == 0) {
				busyloop(1000);
				if (rk_vmpcp_mutex_lock(mid) < 0) return -1;
				busyloop(2000);
				if (rk_vmpcp_mutex_unlock(mid) < 0) return -1;
				busyloop(1000);
			}
			if (i == 1) {
				busyloop(2000);
			}
			if (i == 2) {
				busyloop(1000);
				if (rk_vmpcp_mutex_lock(mid) < 0) return -1;
				busyloop(2000);
				if (rk_vmpcp_mutex_unlock(mid) < 0) return -1;
				busyloop(1000);
			}
		}
	}
	getchar();

	kill(0, 9);
	return 0;
}
