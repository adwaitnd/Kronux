#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define MAX_RESOURCE_SET_NAME_LEN	20	
#define FNAME_MAX 			50
#define MEGABYTE	(1024*1024UL)

struct rk_trace_data p;
int main(int argc, char *argv[]){
	int i, j;
	int pid;
	char fname[FNAME_MAX] = {0,};

	if (argc < 2) {
		printf("get_rk_trace: needs pid\n");
		return -1;
	}
	pid = atoi(argv[1]);
	printf("get_rk_trace: pid %d\n", pid);
	
	rk_trace_get(pid, &p);
	unsigned long long start_time = p.sched_time[0];
	FILE *fp;
	int last_value = 0, height = 0;
	// sched
	sprintf(fname, "plot_sched_pid%d.txt", pid);
	fp = fopen(fname, "w");
	if (fp == NULL) {
		perror("cannot open plot.txt");
		return -1;
	}
	for (i = 0; i < p.nr_sched; i++) {
		/*
		if (p.sched_onoff[i] != last_value) {
			fprintf(fp, "%llu.%09llu, %d, %d\n", 
				(p.sched_time[i] - start_time) / 1000000000ll,
				(p.sched_time[i] - start_time) % 1000000000ll,
				last_value, 
				p.sched_core[i]);
			last_value = p.sched_onoff[i];
		}
		*/
		if (i == 0 && p.sched_onoff[i] == 0) continue;

		fprintf(fp, "%llu.%09llu, %d, %d\n", 
			//(p.sched_time[i] - start_time) / 1000000000ll,
			//(p.sched_time[i] - start_time) % 1000000000ll,
			(p.sched_time[i]) / 1000000000ll,
			(p.sched_time[i]) % 1000000000ll,
			p.sched_onoff[i],
			p.sched_core[i]);
	}
	fclose(fp);
	
	return 0;
}
