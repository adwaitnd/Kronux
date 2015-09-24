#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

#define MICROSEC_TO_NANOSEC	1000LL
#define NANOSEC_LL		1000000000LL

int main(int argc, char* argv[]){
	pseudo_vcpu_attr_data_t attr;
	int vcpu_pid, i;
	long long intr_exec_time_ns;
	int rd;

	if (argc < 6) {
		printf("<usage>: rk_vint_register_pseudo_vcpu <vcpu_pid> <pseudo_vcpu_cpursv> <host_irq_no> <guest_irq_no> <intr_exec_time>\n");
		printf("\t <vcpu_pid>: VCPU PID\n");
		printf("\t <pseudo_vcpu_cpursv>: CPU reserve index of pseudo-VCPU to be registered\n");
		printf("\t <host_irq_no>: Host IRQ number (physical interrupt)\n");
		printf("\t <guest_irq_no>: Guest IRQ number (virtual interrupt)\n");
		printf("\t <intr_exec_time>: Execution time (microseconds) for handling a single virtual interrupt instance\n");
		return -1;
	}

	vcpu_pid = atoi(argv[1]);
	attr.pseudo_vcpu_cpursv = atoi(argv[2]);
	attr.host_irq_no = atoi(argv[3]);
	attr.guest_irq_no = atoi(argv[4]);
	intr_exec_time_ns = atoi(argv[5]) * MICROSEC_TO_NANOSEC;
	attr.intr_exec_time.tv_sec = intr_exec_time_ns / NANOSEC_LL;
	attr.intr_exec_time.tv_nsec = intr_exec_time_ns % NANOSEC_LL;

	if (rk_vint_register_pseudo_vcpu(vcpu_pid, &attr) == RK_SUCCESS) {
		printf("rk_vint_register_pseudo_vcpu: Success\n");
	}
	else {
		printf("rk_vint_register_pseudo_vcpu: Error (check kernel log)\n");
		return -1;
	}
	return 0;
}

