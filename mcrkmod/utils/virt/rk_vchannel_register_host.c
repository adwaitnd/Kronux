#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define FNAME_MAX 1024

int main(int argc, char* argv[]){
	int rd;
	int n_vcpus, i;
	char path[FNAME_MAX];

	if (argc < 4) {
		printf("<usage>: rk_vchannel_register_host <rd> <n_vcpus> <path>\n");
		printf("\t <rd>: resource set index\n");
		printf("\t <n_vcpus>: number of vcpus (cpu reserves)\n");
		printf("\t <path>: path to vchannel\n");
		printf("\t         ex. if vchannels are unix domain sockets in /tmp/rk-vm1.{0,1}\n");
		printf("\t             -> /tmp/rk-vm1)\n");
		//printf("\t         ex. if vchannels are named pipes in /tmp/rk-vm1.{0,1}.{in,out}\n");
		//printf("\t             -> /tmp/rk-vm1)\n");
		return -1;
	}

	rd = atoi(argv[1]);
	n_vcpus = atoi(argv[2]);
	for (i = 0; i < n_vcpus; i++) {
		sprintf(path, "%s.%d", argv[3], i);
		//sprintf(path, "%s.%d.in", argv[3], i);
		if (rk_vchannel_register_host(rd, i, path) < 0) {
			printf("rk_vchannel_register_host: error (%s)\n", path);
			return -1;
		}
	}
	return 0;
}

