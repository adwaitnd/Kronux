#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

void print_usage()
{
	printf("<usage>: attach_vcpus <rd> <n> <pid_1> <pid_2> ... <pid_n>\n");
	printf("\t <rd>: the resource set descriptor\n");
	printf("\t <n>: number of vcpus (cpu reserves)\n");
	printf("\t <pid_i>: vcpu pid to be attached\n");
}
int main(int argc, char *argv[]){
	int i, pid;
	int rd;
	int N_VCPUS;

	pid = -1;

	if (argc < 4) {
		print_usage();
		return -1;
	}
	rd = atoi(argv[1]);
	N_VCPUS = atoi(argv[2]);
	if (argc != N_VCPUS + 3) {
		print_usage();
		return -1;
	}

	for (i = 0; i < N_VCPUS; i++) {
		struct rk_ordered_list cpursv_list;
		pid = atoi(argv[i + 3]);
		cpursv_list.n = 1;
		cpursv_list.elem[0] = i;

		if (rk_resource_set_attach_process(rd, pid, &cpursv_list) < 0) {
			printf("Error: cannot attach %d to rd %d\n", pid, rd);
			break;
		}

		printf("VCPU pid  : %d\t -> CPURSV %d of RD %d\n", pid, i, rd);
	}
	return 0;
}
