#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <rk_api.h>
#include <sys/types.h>

#ifdef __NR_gettid
#define gettid() syscall(__NR_gettid)
#else
#error SYS_gettid unavailable
#endif

/* Number of iteration per task */
#define MAX_RESOURCE_SET_NAME_LEN	20	

#define MILLISEC_TO_NANOSEC		1000000LL

#define NANOSEC_LL			1000000000LL

#define NUM_PROCESS	64
//#define NUM_PROCESS	32

int tmp_thread(void * arg)
{
	printf("Child thread: pid %d, tid %d\n", getpid(), gettid()); 
	while (1) { 
		//printf("%d\n", getpid());
		sleep(1);
	}
	return 0;
}

#define MAX_THREADS	10
#define STACK_SIZE	10000
void *stack[MAX_THREADS][STACK_SIZE];

int main(int argc, char *argv[]){
	int i, j;
	int rd;
	int ret;
	struct cpu_reserve_attr cpu_attr;
	struct timespec now;
	int pid[NUM_PROCESS] = {0,};
	int thread_count = 0;

	int cpuid = 0;	
	long long C, T, D;
	char R[MAX_RESOURCE_SET_NAME_LEN];

	memset(&cpu_attr, 0, sizeof(cpu_attr));
	
	/* Default values */
	C = (500 + cpuid) * MILLISEC_TO_NANOSEC;
	//C = 5 * MILLISEC_TO_NANOSEC;
	T = (1000 + cpuid * 20) * MILLISEC_TO_NANOSEC;
	D = (1000 + cpuid * 20) * MILLISEC_TO_NANOSEC;
	//T = D = (100 - cpuid) * MILLISEC_TO_NANOSEC;

	clock_gettime(CLOCK_REALTIME, &now);
	//sprintf(R, "RSET%ld", now.tv_nsec / 1000000 + now.tv_sec * 1000);
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
	//cpu_attr.reserve_mode.rep_mode = RSV_SOFT;
	cpu_attr.reserve_mode.rep_mode = RSV_HARD;
	cpu_attr.cpunum = cpuid;

	//sched_setaffinity(getpid(), sizeof(cpus), &cpus);
	
	//rd = rk_resource_set_create(R, 1, 1, CPURSV_MIGRATION_DEFAULT);
	//rk_cpu_reserve_create(rd, &cpu_attr);

	printf("Main thread - pid %d, tid %d\n", getpid(), gettid());
	while (1) {
		if (thread_count < MAX_THREADS) {
			printf("Press ENTER to create a child thread (current: %d, max: %d)\n", thread_count, MAX_THREADS);
			getchar();
			clone(tmp_thread, &stack[thread_count][STACK_SIZE], CLONE_SIGHAND | CLONE_VM | CLONE_THREAD, NULL);
			thread_count++;
		}
		else {
			printf("Cannot create more threads (current: %d, max: %d)\n", thread_count, MAX_THREADS);
			getchar();
		}
	}

	//rk_resource_set_attach_process(rd, getpid(), NULL);
	//while (1) {
	//	sleep (1);
	//	rt_wait_for_next_period();
	//}
	
	return 0;
}
