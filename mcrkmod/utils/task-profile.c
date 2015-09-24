#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <rk_api.h>

// parameters
int cpuid = 0;
int iter = 1;
int prio = 80;

int main(int argc, char* argv[])
{
#if defined(RK_TRACE_SUM)
	if (argc < 2) {
		printf("Usage: task-profile <cpuid> <command>\n");
		return 0;
	}

        cpuid = atoi(argv[1]);

	cpu_set_t cpus;
	CPU_ZERO(&cpus);
	CPU_SET(cpuid, &cpus);
	sched_setaffinity(getpid(), sizeof(cpus), &cpus);

	struct sched_param par;
	par.sched_priority = 80;
	sched_setscheduler(getpid(), SCHED_FIFO, &par);

	struct rk_trace_data_sum *pmc = (struct rk_trace_data_sum*)malloc(sizeof(struct rk_trace_data_sum) * iter);
	if (!pmc) {
		perror("malloc error");
		return -1;
	}
	int i;
	for (i = 0; i < iter; i++) {
		memset(&pmc[i], 0, sizeof(struct rk_trace_data_sum));
	}

	printf("%s: run %s for %d times (cpuid=%d)\n", argv[0], argv[2], iter, cpuid);

	for (i = 0; i < iter; i++) {
		int childpid = fork();
		if (childpid) {
			int status;
			waitpid(childpid, &status, 0);
			rk_trace_sum_get(childpid, &pmc[i]);
		}
		else {
			childpid = getpid();
                        sched_setaffinity(getpid(), sizeof(cpus), &cpus);
			rk_trace_sum_set(childpid);
			if (execvp(argv[2], &argv[2]) < 0) {
				perror("execv error");
				return -1;
			}
		}
	}
	for (i = 0; i < iter; i++) {
		printf("[%s: %d / %d]\n", argv[2], i + 1, iter);
		printf("Instr,    %lld\n", pmc[i].total.inst_retired_any);
		printf("Cycles,   %lld\n", pmc[i].total.cpu_clk_unhalted);
		printf("LLC Miss, %lld\n", pmc[i].total.l3_miss);
	}
#else
        printf("task-profile: Use task-trace, which is the successor of task-profile.\n");
#endif
	return 0;
}
