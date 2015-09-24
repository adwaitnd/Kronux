#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define MAX_RESOURCE_SET_NAME_LEN	20	
#define FNAME_MAX 			50
#define MEGABYTE	(1024*1024UL)

#define uint64_t unsigned long long 

struct rk_trace_data_sum p;

int main(int argc, char *argv[]){
	int i, j;
	int pid;
	uint64_t l3_miss, l3_unshared_hit, l2_hitm;
	char fname[FNAME_MAX] = {0,};

	if (argc < 2) {
		printf("rk_trace_pmc_get: needs pid\n");
		return -1;
	}
	pid = atoi(argv[1]);
	printf("rk_trace_pmc_get: pid %d\n", pid);
	
	rk_trace_sum_get(pid, &p);

	FILE *fp;
	sprintf(fname, "plot_pmc_pid%d.txt", pid);
	fp = fopen(fname, "w");
	if (fp == NULL) {
		perror("cannot open plot.txt");
		return -1;
	}
	/*
	l3_miss = p.total.l3_miss;
	l3_unshared_hit = p.total.l3_unshared_hit;
	l2_hitm = p.total.l2_hitm;
	*/
	fprintf(fp, "Inst, %lld\n", p.total.inst_retired_any);
	fprintf(fp, "Cycle, %lld\n", p.total.cpu_clk_unhalted);
	fprintf(fp, "L1 Hit : %lld\n", p.total.l1_hit);
	fprintf(fp, "L2 Hit : %lld\n", p.total.l2_hit);
	fprintf(fp, "L3 Hit : %lld\n", p.total.l3_hit);
	fprintf(fp, "MemOps : %lld\n", p.total.l3_miss);
	/*
	fprintf(fp, "L3Hit, %lld\n", l3_unshared_hit + l2_hitm);
	fprintf(fp, "L3Miss, %lld\n", p.total.l3_miss);
	fprintf(fp, "L2Hit, %lld\n", p.total.l2_hit);
	fprintf(fp, "L2Miss, %lld\n", l2_hitm + l3_unshared_hit + l3_miss);
	fprintf(fp, "HitM, %lld\n", l2_hitm);
	fprintf(fp, "L3_UnsharedHit, %lld\n", l3_unshared_hit);
	*/

	fclose(fp);
	
	return 0;
}
