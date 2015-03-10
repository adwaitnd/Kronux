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
	int rd;

	if (argc < 2) {
		printf("show_reserve_info: needs rd\n");
		return -1;
	}
	rd = atoi(argv[1]);
	printf("show_reserve_info: rd %d\n", rd);
	
	rk_mem_reserve_show_reserved_pages(rd);

	return 0;
}
