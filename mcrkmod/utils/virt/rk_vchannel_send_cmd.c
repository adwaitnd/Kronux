#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>


int main(int argc, char* argv[]){
	int rd;
	int vcpu;
	int cmd;
	int pid;
	int i;

	if (argc < 5) {
		printf("<usage>: rk_vchannel_send_cmd <rd> <vcpu_cpursv_idx> <cmd> <pid>\n");
		printf("\t rd: resource set desc\n");
		printf("\t vcpu_cpursv_idx: cpu reserve index for target vcpu\n");
		printf("\t cmd: command to be sent\n");
		printf("\t pid: target task pid\n");
		return -1;
	}

	rd = atoi(argv[1]);
	vcpu = atoi(argv[2]);
	cmd = atoi(argv[3]);
	pid = atoi(argv[4]);

	for (i = 0; i < 5; i++) {
		if (rk_vchannel_send_cmd(rd, vcpu, cmd, pid) < 0) {
			printf("send error\n");
			return -1;
		}
	}

	return 0;
}

