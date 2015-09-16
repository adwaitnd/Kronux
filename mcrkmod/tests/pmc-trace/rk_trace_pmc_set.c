#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define MAX_RESOURCE_SET_NAME_LEN	20	
#define MEGABYTE	(1024*1024UL)

int main(int argc, char *argv[]){
	int i;
	int pid;

	if (argc < 2) {
		printf("start_rk_trace_pmc: needs pid\n");
		return -1;
	}
	pid = atoi(argv[1]);
	printf("start_rk_trace_pmc: pid %d\n", pid);
	
	rk_trace_sum_set(pid);

	return 0;
}
