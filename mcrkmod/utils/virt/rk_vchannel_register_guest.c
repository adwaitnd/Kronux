#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define FNAME_MAX 1024

int main(int argc, char* argv[]){
	int n_vcpus, i;
	char path[FNAME_MAX];

	if (argc < 3) {
		printf("<usage>: rk_vchannel_register_guest <n_vcpus> <path>\n");
		printf("\t <n_vcpus>: number of vcpus\n");
		printf("\t <path>: path to RK guest vchannels\n");
		printf("\t         ex. if vchannels are /dev/virtio-ports/rk-vchannel.{0,1}\n");
		printf("\t             -> /dev/virtio-ports/rk-vchannel\n");
		return -1;
	}

	n_vcpus = atoi(argv[1]);

	for (i = 0; i < n_vcpus; i++) {
		sprintf(path, "%s.%d", argv[2], i);
		if (rk_vchannel_register_guest(i, path) < 0) {
			printf("rk_vchannel_register_guest: error (%s)\n", path);
			return -1;
		}
	}

	return 0;
}

