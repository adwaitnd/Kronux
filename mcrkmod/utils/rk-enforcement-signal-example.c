#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <rk_api.h>

#define MAX_RESOURCE_SET_NAME_LEN	20	
#define MILLISEC_TO_NANOSEC		1000000LL
#define NANOSEC_LL			1000000000LL


/* SIG_RK_ENFORCED signal handler */
void signal_handler_rk_enforced(int signo) 
{
	printf("pid%d: SIG_RK_ENFORCED (%d)\n", getpid(), signo);
}

int main(int argc, char *argv[]){
	int rd;
	struct cpu_reserve_attr cpu_attr;
	struct timespec now;

	int cpuid = 0;	
	long long C, T, D;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	memset(&cpu_attr, 0, sizeof(cpu_attr));

	/* Install SIG_RK_ENFORCED signal handler */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler_rk_enforced;
	sigaction(SIG_RK_ENFORCED, &sa, NULL);

	/* Default values */
	C = (100) * MILLISEC_TO_NANOSEC;
	T = (1000) * MILLISEC_TO_NANOSEC;
	D = (1000) * MILLISEC_TO_NANOSEC;

	clock_gettime(CLOCK_REALTIME, &now);
	sprintf(R, "RSET%d", getpid());

	cpu_set_t cpus;
	CPU_ZERO(&cpus);
	CPU_SET(cpuid, &cpus);
	
	cpu_attr.compute_time.tv_sec=(C/NANOSEC_LL);
	cpu_attr.period.tv_sec=(T/NANOSEC_LL);
	cpu_attr.deadline.tv_sec=(D/NANOSEC_LL);
	cpu_attr.blocking_time.tv_sec=0;
	cpu_attr.start_time.tv_sec=0;

	cpu_attr.compute_time.tv_nsec=(C%NANOSEC_LL);
	cpu_attr.period.tv_nsec=(T%NANOSEC_LL);
	cpu_attr.deadline.tv_nsec=(D%NANOSEC_LL);
	cpu_attr.blocking_time.tv_nsec=0;
	cpu_attr.start_time.tv_nsec=0;
	
	cpu_attr.reserve_mode.sch_mode = RSV_HARD;
	cpu_attr.reserve_mode.enf_mode = RSV_HARD;
	cpu_attr.reserve_mode.rep_mode = RSV_SOFT;
	cpu_attr.cpunum = cpuid;

	/*
	 * notify_when_enforce 
	 * - If set as true, SIG_RK_ENFORCED signal will be sent to tasks 
	 *   when CPU enforcement occurs 
	 */
	cpu_attr.notify_when_enforced = TRUE;

	rd = rk_resource_set_create(R, TRUE, TRUE, CPURSV_MIGRATION_DEFAULT);
	rk_cpu_reserve_create(rd, &cpu_attr);
	rk_resource_set_attach_process(rd, getpid(), NULL);

	while (1);
	
	return 0;
}
