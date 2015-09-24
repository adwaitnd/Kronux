#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define MAX_RESOURCE_SET_NAME_LEN	20	
#define MEGABYTE	(1024*1024UL)

int main(int argc, char *argv[]){
	int rd = -1;
	int size = 10;
	int period = 1;
	int i;
	int cpursv_index = -1;

	for (i = 1; i < argc; i++) {
		if (strlen(argv[i]) > 3) {
			if(argv[i][1] == 'D') {
				rd = atoi(&argv[i][3]);
			}
		}
		if (strlen(argv[i]) > 3) {
			if(argv[i][1] == 'I') {
				cpursv_index = atoi(&argv[i][3]);
			}
		}
		if (strlen(argv[i]) > 3) {
			if(argv[i][1] == 'N') {
				size = atoi(&argv[i][3]);
			}
		}
		if (strlen(argv[i]) > 3) {
			if(argv[i][1] == 'T') {
				period = atoi(&argv[i][3]);
			}
		}
	}
	if (argc < 2 || rd == -1 || cpursv_index == -1) {
		printf("<usage>: cpursv-profile -D=<rd> -I=<cpursv_index> -N=<size> -T=<period>\n");
		printf("\t <rd> is the resource set descriptor for the resource set\n");
		printf("\t <cpursv_index> is the index of cpu reserve in the resource set\n");
		printf("\t <size> is the buffer size used to collect profile data (default = 10, max = %d)\n", CPU_PROFILE_DATA_MAX);
		printf("\t <period> is the time period to retrieve profile data (in sec, default = 1sec)\n");
		return 0;
	}
	printf("cpursv-profile: rd = %d, cpursv_index = %d, size = %d, period = %d\n", rd, cpursv_index, size, period);

	if (rk_getcpursv_start_profile(rd, cpursv_index, size) < 0) {
		printf("cpursv-profile: error\n");
		return -1;
	}

	struct rk_cpu_profile *buf;
	buf = malloc(size * sizeof(struct rk_cpu_profile));
	if (buf == NULL) {
		printf("cpursv-profile: malloc error\n");
		return -1;
	}
	
	while (1) {
		sleep(period);
		int n = rk_getcpursv_get_profile(rd, cpursv_index, buf);
		printf("[Collected %d data (rset %d, cpursv %d)]\n", n, rd, cpursv_index);
		for (i = 0; i < n; i++) {
			printf("  - %lu.%09lu - %lu.%09lu : %.2f\n", 
				buf[i].release.tv_sec, buf[i].release.tv_nsec,
				buf[i].completion.tv_sec, buf[i].completion.tv_nsec,
				buf[i].utilization / 100.);
		}
	}
	return 0;
}
