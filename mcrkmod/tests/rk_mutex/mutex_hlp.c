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

#define NUM_TASKS 4
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
	int rd;
	int ret;
	struct cpu_reserve_attr cpu_attr;
	struct timespec now;
	int pid[NUM_TASKS] = {0,};

	int cpuid;	
	long long C, T, D, offset;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	int mid, key = 40;

	memset(&cpu_attr, 0, sizeof(cpu_attr));

	// create four tasks
	clock_gettime(CLOCK_REALTIME, &now);
	for (i = 0; i < NUM_TASKS; i++) {
		ret = fork();
		if (ret != 0) {
			pid[i] = ret;
			continue;
		}

		C = 100 * MILLISEC_TO_NANOSEC;
		T = 1000 * MILLISEC_TO_NANOSEC;
		if (i == 0) {
			D = 1000 * MILLISEC_TO_NANOSEC;
		}
		else if (i == 1) {
			D = 920 * MILLISEC_TO_NANOSEC;
		}
		else if (i == 2) {
			D = 910 * MILLISEC_TO_NANOSEC;
		}
		else if (i == 3) {
			D = 900 * MILLISEC_TO_NANOSEC;
		}

		if (i == 0) {
			offset = 1000 * MILLISEC_TO_NANOSEC;
		}
		else {
			offset = (1100 + i * 10) * MILLISEC_TO_NANOSEC;
		}
		sprintf(R, "RSET%d", getpid());

		now.tv_sec += offset / NANOSEC_LL;
		now.tv_nsec += offset % NANOSEC_LL;
		cpu_attr.start_time = now;

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
		//cpu_attr.cpunum = i % 4;
		cpu_attr.cpunum = 0;

		rd = rk_resource_set_create(R, 1, 1, CPURSV_MIGRATION_DEFAULT);
		rk_cpu_reserve_create(rd, &cpu_attr);
		rk_resource_set_attach_process(rd, getpid(), NULL);

		// wait for the cpursv to be activated
		rt_wait_for_next_period();

		// now, cpursv is activated -> rt priority available
		mid = rk_hlp_mutex_open(key, TRUE);

		while (1) {
			struct sched_param par;
			rt_wait_for_next_period();

			if (rk_hlp_mutex_lock(mid) < 0) return 0;
			sched_getparam(getpid(), &par);
			if (i == 0) {
				fprintf(stderr, "\n%d(prio:%d/", getpid(), par.sched_priority);
				usleep(500000);
				//busyloop(500 * 1000);
				sched_getparam(getpid(), &par);
				fprintf(stderr, "%d)", par.sched_priority);
			}
			else fprintf(stderr, " - %d(prio:%d)", getpid(), par.sched_priority);
			if (rk_hlp_mutex_unlock(mid) < 0) return 0;
		}
	}
	sleep(2);
	//printf("## Enter any key to kill all child processes\n");
	getchar();

	kill(0, 9);

	return 0;
}
