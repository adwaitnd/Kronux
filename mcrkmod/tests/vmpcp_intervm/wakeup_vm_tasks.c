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

	if (argc < 2) {
		printf("<usage>: wakeup_vm_tasks <pid1> <pid2>\n");
		return -1;
	}

	for (i = 0; i < 2; i++) {
		rd = i;
		vcpu = 0;
		cmd = 2;//RK_VCHANNEL_CMD_MUTEX_WAKEUP;
		pid = atoi(argv[1 + i]);
		if (rk_vchannel_send_cmd(rd, vcpu, cmd, pid) < 0) {
			printf("send error\n");
			return -1;
		}
	}

	return 0;
}

