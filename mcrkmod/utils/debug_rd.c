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

	pid = atoi(argv[1]);
	
	rk_testing(0x103, pid, NULL);

	return 0;
}
