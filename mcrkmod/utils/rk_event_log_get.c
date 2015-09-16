#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>


#define FNAME_MAX 100

int main(int argc, char *argv[]) {
#ifndef RK_EVENT_LOG
        printf("rk_event_log_get: requires RK_EVENT_LOG option\n");
#else
	int i;
	int period;
	char fname[FNAME_MAX];

	if (argc < 2) {
		printf("Usage: rk_event_log_get <update_period_in_sec>\n");
		return -1;
	}
	period = atoi(argv[1]);
	if (period < 1) {
		printf("period should be greater than or equal to 1 (second)\n");
		return -1;
	}

	struct rk_event_data *buf;
	buf = malloc(sizeof(struct rk_event_data) * RK_EVENT_LOG_SIZE);
	if (!buf) {
		perror("malloc error\n");
		return -1;
	}
	FILE *fp;
	sprintf(fname, "log_rk_event.txt");
	fp = fopen(fname, "w");
	if (!fp) {
		perror("fopen error\n");
		return -1;
	}
	fprintf(fp, "time, type, cpuid, pid, arg1, arg2\n");
	while (1) {	
		//sleep(period);
		usleep(200*1000);
		int cur_size = rk_event_log_get(buf);
		for (i = 0; i < cur_size; i++) {
			fprintf(fp, "%llu.%09llu, %d, %d, %d, %lu, %lu\n", 
				buf[i].time / 1000000000ll,
				buf[i].time % 1000000000ll,
				buf[i].type, 
				buf[i].cpuid,
				buf[i].pid,
				buf[i].arg1,
				buf[i].arg2);
		}
		fflush(fp);
	}
	fclose(fp);

#endif
	return 0;
}
