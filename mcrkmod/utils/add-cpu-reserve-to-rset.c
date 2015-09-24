#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

/* Number of iteration per task */
#define MAX_RESOURCE_SET_NAME_LEN	20	
#define MILLISEC_TO_NANOSEC		1000000LL
#define NANOSEC_LL			1000000000LL

int main(int argc, char *argv[]){
	int i;
	int rd;
	struct cpu_reserve_attr cpu_attr;
	struct timespec now;

	int cpuid;	
	int cpursv;
	long long C, T, D;

	memset(&cpu_attr, 0, sizeof(cpu_attr));

	/* Default values */
	rd = -1;
	cpuid = 0;
	C = 200 * MILLISEC_TO_NANOSEC;
	T = 1000 * MILLISEC_TO_NANOSEC;
	D = 1000 * MILLISEC_TO_NANOSEC;

	for (i = 1; i < argc; i++) {
		if (strlen(argv[i]) > 3) {
			if (argv[i][1] == 'A') {
				cpuid = atoi(&argv[i][3]);
			}
		}
		if (strlen(argv[i]) > 3) {
			if (argv[i][1] == 'C') {
				C = atoll(&argv[i][3]);
				C = C * MILLISEC_TO_NANOSEC;
			}
		}
		if (strlen(argv[i]) > 3) {
			if (argv[i][1] == 'T') {
				T = atoi(&argv[i][3]);
				T = T * MILLISEC_TO_NANOSEC;
			}
		}
		if (strlen(argv[i]) > 3) {
			if (argv[i][1] == 'D') {
				D = atoi(&argv[i][3]);
				D = D * MILLISEC_TO_NANOSEC;
			}
		}
		if (strlen(argv[i]) > 3) {
			if (argv[i][1] == 'R') {
				rd = atoi(&argv[i][3]);
			}
		}	
	}
	if (argc < 2 || rd == -1) {
		printf("<usage>: add-cpu-reserve-to-rset -R=<resource_set_descriptor> -A=<cpuid> -C=<compute_time> -T=<period> -D=<deadline>\n");
		printf("\t <resource_set_descriptor>: resource set descriptor\n");
		printf("\t Default <cpuid> = %d\n", cpuid);
		printf("\t Default C = %lld (%lld milliseconds)\n", C / MILLISEC_TO_NANOSEC, C / MILLISEC_TO_NANOSEC);
		printf("\t Default T = %lld (%lld milliseconds)\n", T / MILLISEC_TO_NANOSEC, T / MILLISEC_TO_NANOSEC);
		printf("\t Default D = %lld (%lld milliseconds)\n", D / MILLISEC_TO_NANOSEC, D / MILLISEC_TO_NANOSEC);
		printf("\t Resulting Default Utilization: %.1lf percent\n", ((double)C / (double)D) * 100);
		return 0;
	}

	printf("RSET         : %d\n", rd);
	printf("Affine CPU   : %d\n", cpuid);
	printf("Compute Time : %lld\n", C);
	printf("Reserve Period  : %lld\n", T);
	printf("Reserve Deadline: %lld\n", D);

	cpu_set_t cpus;
	CPU_ZERO(&cpus);
	CPU_SET(cpuid, &cpus);
	
	cpu_attr.compute_time.tv_sec=(C/NANOSEC_LL);
	cpu_attr.period.tv_sec=(T/NANOSEC_LL);
	cpu_attr.deadline.tv_sec=(D/NANOSEC_LL);
	cpu_attr.blocking_time.tv_sec=0;

	cpu_attr.compute_time.tv_nsec=(C%NANOSEC_LL);
	cpu_attr.period.tv_nsec=(T%NANOSEC_LL);
	cpu_attr.deadline.tv_nsec=(D%NANOSEC_LL);
	cpu_attr.blocking_time.tv_nsec=0;

	cpu_attr.reserve_mode.sch_mode = RSV_HARD;
	cpu_attr.reserve_mode.enf_mode = RSV_HARD;
	cpu_attr.reserve_mode.rep_mode = RSV_SOFT;
	cpu_attr.cpunum = cpuid;

	sched_setaffinity(getpid(), sizeof(cpus), &cpus);
	
	if ((cpursv = rk_cpu_reserve_create(rd, &cpu_attr)) < 0) {
		printf("Failed to create CPU reserve...\n");
		return -1;
	}

	printf("CPU Reserve %d is added to Resource Set Descriptor %d\n", cpursv, rd);

	return 0;
}
