#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>


#define FNAME_MAX 100

int main(int argc, char *argv[]) {
#if defined(RK_TRACE_SUM)
        printf("task-trace: RK_TRACE_SUM should be turned off\n");
#else
	int i;
	int pid;
	int size;
	int period;
	char fname[FNAME_MAX];

	if (argc < 4) {
		printf("Usage: task-trace <pid> <buffer_size> <update_period_in_sec>\n");
		return -1;
	}
	pid = atoi(argv[1]);
	size = atoi(argv[2]);
	period = atoi(argv[3]);
	if (period < 1) {
		printf("period should be greater than or equal to 1 (second)\n");
		return -1;
	}

	printf("Start task tracing: pid %d, buffer %d, update period %d\n", pid, size, period);
	if (rk_trace_set(pid, size) < 0) {
		printf("rk_trace_set error\n");
		return -1;
	}

	struct rk_trace_data *buf;
	buf = malloc(sizeof(struct rk_trace_data) * size);
	if (!buf) {
		perror("malloc error\n");
		return -1;
	}
	FILE *fp;
	sprintf(fname, "trace_pid_%d.txt", pid);
	fp = fopen(fname, "w");
	if (!fp) {
		perror("fopen error\n");
		return -1;
	}
#ifdef RK_PROFILE_PMC
	fprintf(fp, "time, sched_onoff, sched_core, task_status, llc_miss_count, instr_count\n");
#else
	fprintf(fp, "time, sched_onoff, sched_core, task_status\n");
#endif
	while (1) {	
		sleep(period);
		int cur_size = rk_trace_get(pid, buf);
		for (i = 0; i < cur_size; i++) {
#ifdef RK_PROFILE_PMC
			fprintf(fp, "%llu.%09llu, %d, %d, %d, %llu, %llu\n", 
				buf[i].time / 1000000000ll,
				buf[i].time % 1000000000ll,
				buf[i].onoff, 
				buf[i].core,
				buf[i].task_status,
				buf[i].llc_count,
				buf[i].instr_count);
#else
			fprintf(fp, "%llu.%09llu, %d, %d, %d\n", 
				buf[i].time / 1000000000ll,
				buf[i].time % 1000000000ll,
				buf[i].onoff, 
				buf[i].core,
				buf[i].task_status);
#endif
		}
		fflush(fp);
	}
	fclose(fp);

#endif
	return 0;
}
