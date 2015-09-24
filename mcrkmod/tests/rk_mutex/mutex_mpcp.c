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

#define NUM_TASKS 	3
#define TRACE_BUF_SIZE	100

void busyloop(long long duration_usec)
{
	int i, j;
	for (i = 0; i < duration_usec; i++) {
		for (j = 0; j < 3400; j++) // for i7-2600
			asm volatile ("nop");
	}
}

// Needs the following RK options:
// - RK_TRACE: enabled
// - RK_TRACE_SUM: disabled
void print_task_trace(int pid[NUM_TASKS]) 
{
	FILE *fp;
	struct rk_trace_data *buf;
	int i, j;

	buf = malloc(sizeof(struct rk_trace_data) * TRACE_BUF_SIZE);
	fp = fopen("mpcp_trace.txt", "w");
	//fprintf(fp, "time, sched_onoff, sched_core, task_status, llc_miss_count, instr_count\n");
	while (1) {
		sleep(1);
		for (i = 0; i < NUM_TASKS; i++) {
			int cur_size = rk_trace_get(pid[i], buf);
			for (j = 0; j < cur_size; j++) {
				int event = buf[j].onoff;
				if (buf[j].type == RK_TRACE_TYPE_CS) buf[j].onoff += 2;
				fprintf(fp, "%d, %d, %lld.%09lld, %d, %d, %d, %lld, %lld, t%d\n",
					pid[i], pid[i], 		// pid, tid
					buf[j].time / 1000000000ll,	// timestamp (sec)
					buf[j].time % 1000000000ll,	// timestamp (nsec)
					buf[j].onoff,			// event
					buf[j].core,			// core
					buf[j].task_status,		// task status
					buf[j].llc_count,		// llc
					buf[j].instr_count,		// instr
					i				// task name
					);
			}
		}
		fflush(fp);
	}
}

int main(int argc, char *argv[]){
	int i;
	int rd[NUM_TASKS];
	int ret;
	struct cpu_reserve_attr cpu_attr;
	struct timespec now;
	int pid[NUM_TASKS] = {0,};

	int cpuid;	
	long long C, T, D, start_time;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	int mid, key = 50;

	memset(&cpu_attr, 0, sizeof(cpu_attr));

	clock_gettime(CLOCK_REALTIME, &now);
	
	// create three resource sets
	// - i=0: Core 0
	// - i=1: Core 0
	// - i=2: Core 1
	for (i = 0; i < NUM_TASKS; i++) {
		C = 300 * MILLISEC_TO_NANOSEC;
		T = 1000 * MILLISEC_TO_NANOSEC;
		D = (900 + (i * 10)) * MILLISEC_TO_NANOSEC;

		start_time = (now.tv_sec + 1) * NANOSEC_LL + now.tv_nsec;
		if (i == 0 || i == 2) {
			start_time += 100 * MILLISEC_TO_NANOSEC;
		}

		sprintf(R, "RSET%d", getpid());

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

		if (i == 0 || i == 1) 
			cpu_attr.cpunum = 0;
		else
			cpu_attr.cpunum = 1;

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
		mid = rk_mpcp_mutex_open(key, TRUE);
		//mid = rk_pcp_mutex_open(key, TRUE);
		rk_trace_set(getpid(), TRACE_BUF_SIZE);

		while (1) {
			struct sched_param par;
			rt_wait_for_next_period();

			if (i == 0) {
				sched_getparam(getpid(), &par);
				busyloop(10 * 1000); // small gap before printing
				fprintf(stderr, "- %d(prio:%d)", getpid(), par.sched_priority);
				busyloop(200 * 1000);
			}
			else if (i == 1) {
				sched_getparam(getpid(), &par);
				fprintf(stderr, "\n%d(prio:%d)", getpid(), par.sched_priority);
				busyloop(50 * 1000);
				if (rk_mpcp_mutex_lock(mid) < 0) return 0;
				//if (rk_pcp_mutex_lock(mid) < 0) return 0;
				sched_getparam(getpid(), &par);
				fprintf(stderr, "- %d(CRIT/prio:%d)", getpid(), par.sched_priority);
				busyloop(200 * 1000);
				if (rk_mpcp_mutex_unlock(mid) < 0) return 0;
				//if (rk_pcp_mutex_unlock(mid) < 0) return 0;
				sched_getparam(getpid(), &par);
				fprintf(stderr, "- %d(prio:%d)", getpid(), par.sched_priority);
			}
			else if (i == 2) {
				sched_getparam(getpid(), &par);
				fprintf(stderr, "- %d(prio:%d)", getpid(), par.sched_priority);
				busyloop(50 * 1000);
				if (rk_mpcp_mutex_lock(mid) < 0) return 0;
				//if (rk_pcp_mutex_lock(mid) < 0) return 0;
				sched_getparam(getpid(), &par);
				fprintf(stderr, "- %d(CRIT/prio:%d)", getpid(), par.sched_priority);
				busyloop(50 * 1000);
				if (rk_mpcp_mutex_unlock(mid) < 0) return 0;
				//if (rk_pcp_mutex_unlock(mid) < 0) return 0;
				sched_getparam(getpid(), &par);
				fprintf(stderr, "- %d(prio:%d)", getpid(), par.sched_priority);
			}
		}
	}
	print_task_trace(pid);
	return 0;
}
