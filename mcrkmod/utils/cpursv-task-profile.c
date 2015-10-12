#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define MAX_RESOURCE_SET_NAME_LEN	20	
#define MEGABYTE	(1024*1024UL)

#define N_PID 10
int main(int argc, char *argv[]){
	int size = 10;
	int period = 1;
	int i, j;
	int pid_list[N_PID] = {0,};
	int n_pid_list = 0;

	for (i = 1; i < argc; i++) {
		if (strlen(argv[i]) > 3) {
			if(argv[i][1] == 'N') {
				size = atoi(&argv[i][3]);
			}
		}
		if (strlen(argv[i]) > 3) {
			if(argv[i][1] == 'T') {
				period = atoi(&argv[i][3]);
			}
		}
		if (strlen(argv[i]) > 3) {
			if(argv[i][1] == 'P') {
				if (n_pid_list < N_PID) 
					pid_list[n_pid_list++] = atoi(&argv[i][3]);
			}
		}
	}
	if (argc < 2 || n_pid_list == 0) {
		printf("<usage>: cpursv-task-profile -N=<size> -T=<period> -P=<pid1> -P=<pid2> ...\n");
		printf("\t <size> is the buffer size used to collect profile data (default = 10, max = %d)\n", CPU_PROFILE_DATA_MAX);
		printf("\t <period> is the time period to retrieve profile data (in sec, default = 1sec)\n");
		printf("\t <pid> is the pid of a task (multiple tasks can be profiled, max = %d)\n", N_PID);
		return 0;
	}
	printf("cpursv-task-profile: bufsize = %d, period = %d\n", size, period);

	for (i = 0; i < n_pid_list; i++) {
		if (rk_getcpursv_start_task_profile(pid_list[i], size) < 0) {
			printf("cpursv-task-profile: error\n");
			return -1;
		}
	}

	struct rk_cpu_profile *buf;
	buf = malloc(size * sizeof(struct rk_cpu_profile));
	if (buf == NULL) {
		printf("cpursv-task-profile: malloc error\n");
		return -1;
	}
	
	while (1) {
		sleep(period);
		printf("\n");
		for (i = 0; i < n_pid_list; i++) {
			int n = rk_getcpursv_get_task_profile(pid_list[i], buf);
			printf("[pid %d - collected %d]\n", pid_list[i], n);
			for (j = 0; j < n; j++) {
				printf("  - %lu.%09lu - %lu.%09lu : %.2f\n", 
					buf[j].release.tv_sec, buf[j].release.tv_nsec,
					buf[j].completion.tv_sec, buf[j].completion.tv_nsec,
					buf[j].utilization / 100.);
			}
		}
	}
	return 0;
}
