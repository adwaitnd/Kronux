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
		printf("traverse_page_table: needs pid\n");
		return -1;
	}
	pid = atoi(argv[1]);
	printf("traverse_page_table: pid %d\n", pid);
	
	rk_mem_reserve_traverse_page_table(pid);

	return 0;
}
