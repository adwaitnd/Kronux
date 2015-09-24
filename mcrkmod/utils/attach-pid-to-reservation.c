#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

void print_usage()
{
	printf("<usage>: attach-pid-to-reservation -P=<pid> -D=<rd>\n");
	printf("\t <pid> is the pid of the process to be attached to reservation\n");
	printf("\t <rd> is the resource set descriptor for the resource set\n");
}

int main(int argc, char *argv[]){
	int i, pid;
	int rd;

	pid = -1;
	rd = -1;

	if (argc < 3) {
		print_usage();
		return 0;
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
		printf("Invalid Arguments: Please type \'attach-pid-to-reservation -h\' for usage\n");
		return 0;
	}

	printf("Task Pid  : %d\n", pid);
	printf("Resource Descriptor: %d\n", rd);

	if (rk_resource_set_attach_process(rd, pid, NULL) < 0) {
		printf("Error: cannot attach %d to rd=%d\n", pid, rd);
	}

	return 0;
}
