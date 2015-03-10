#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

int main(int argc, char *argv[]){
	struct sched_param par;

	if (argc < 2) {
		printf("<usage>: set_rt_prio <pid>\n");
		return -1;
	}
	par.sched_priority = MAX_LINUXRK_PRIORITY - 1;
	sched_setscheduler(atoi(argv[1]), SCHED_FIFO, &par);
	return 0;
}
