#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>


int main(int argc, char* argv[]){
	int pid;

	if (argc < 2) return -1;

	pid = atoi(argv[1]);
	rk_event_log_set(pid);

	return 0;
}
