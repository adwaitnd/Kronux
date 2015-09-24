#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define N_VCPUS 4

int main(int argc, char *argv[]){
	int i, pid;
	int rd;

	pid = -1;

	if (argc < N_VCPUS + 2) {
		printf("<usage>: attach_vcpus <rd>");
		for (i = 0; i < N_VCPUS; i++) printf(" <pid_%d>", i + 1);
		printf("\n");
		printf("\t <rd> is the resource set descriptor\n");
		printf("\t <pid_n> is the pid of the VCPU to be attached to the resource set\n");
		return -1;
	}
	rd = atoi(argv[1]);
	for (i = 0; i < N_VCPUS; i++) {
		struct rk_ordered_list cpursv_list;
		pid = atoi(argv[i + 2]);
		cpursv_list.n = 1;
		// attach in reverse order
		cpursv_list.elem[0] = N_VCPUS - 1 - i;
		printf("VCPU Pid  : %d\t -> CPURSV %d of RD %d\n", pid, cpursv_list.elem[0], rd);

		if (rk_resource_set_attach_process(rd, pid, &cpursv_list) < 0) {
			printf("Error: cannot attach %d to rd %d\n", pid, rd);
			break;
		}
	}
	return 0;
}
