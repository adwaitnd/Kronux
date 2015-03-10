#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

void print_usage()
{
	printf("<usage>: detach-pid-from-reservation -P=<pid> -D=<rd>\n");
	printf("\t <pid> is the pid of the process to be detached from the reservation\n");
	printf("\t <rd> is the resource set descriptor for the resource set\n");
}

int main(int argc, char *argv[]){
	int i, pid;
	int rd;

	pid = -1;
	rd = -1;


	if (argc < 3) {
	}
	if (argc >= 1) {
		for(i=1; i<argc; i++) {
			if(strlen(argv[i])>3) {
				if(argv[i][1] == 'P') {
					pid = atoi(&argv[i][3]);
				}
			}
			if(strlen(argv[i])>3) {
				if(argv[i][1] == 'D') {
					rd = atoi(&argv[i][3]);
				}
			}
			if(strlen(argv[i])>=2) {
				if(argv[i][1] == '?' || argv[i][1] == 'h' || argv[i][1] == 'H') {
					print_usage();
					return 0;
				}
			}	
		}
	}

	if (pid == -1 || rd == -1) {
		printf("Invalid Arguments: Please type \'detach-pid-from-reservation -h\' for usage\n");
		return 0;
	}


	printf("Task Pid  : %d\n", pid);
	printf("Resource Descriptor: %d\n", rd);

	if (rk_resource_set_detach_process(rd, pid) < 0) {
		printf("Failed to detach pid %d from rd %d\n", pid, rd);
		return -1;
	}

	return 0;
}
