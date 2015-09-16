#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>

int main(int argc, char *argv[]){
	
	int i;
	int pid;
	int cpuid;	


	/* Default values */
	cpuid = -1;

	if (argc >= 1) {
		for(i=1; i<argc; i++) {
			if(strlen(argv[i])>3) {
				if(argv[i][1] == 'A') {
					cpuid = atoi(&argv[i][3]);
				}
			}
			if(strlen(argv[i])>=2) {
				if(argv[i][1] == '?' || argv[i][1] == 'h' || argv[i][1] == 'H') {
					printf("<usage>: busyloop -A=<cpuid>\n");
					printf("\t Default <cpuid> = -1 (no affinity)\n");
					printf("\t Test forever until you send kill -9 <pid> from a Separate Shell\n");
					return 0;
				}
			}	
		}
	}

	pid = getpid();
	printf("PID: %d\n", pid);
	if (cpuid >= 0) {
		printf("Affine CPU: %d\n", cpuid);
		
		cpu_set_t cpus;
		CPU_ZERO(&cpus);
		CPU_SET(cpuid, &cpus);
	
		sched_setaffinity(pid, sizeof(cpus), &cpus);
	}

	while(1);

	return 0;
}	
