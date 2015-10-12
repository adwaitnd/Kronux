#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <string.h>
#define NR_MB 1000
#define NR_PART 4
#define MEGABYTE (1024*1024)
int  main()
{
	size_t size = NR_MB * MEGABYTE;
	int *buf[NR_PART];
	int n, count, part;
	int i, j, k;
	int start, end;
	for (i = 0; i < NR_PART; i++) {
		buf[i] = malloc(size);
		if (buf[i] == NULL) {
			perror("malloc error\n");
			return -1;
		}
	}
	int cpuid = 3;	
	cpu_set_t cpus;
	CPU_ZERO(&cpus);
	CPU_SET(cpuid, &cpus);
	if (sched_setaffinity(getpid(), sizeof(cpus), &cpus) != 0) {
		perror("error");
		return -1;
	}

	printf("mem_bomb: pid %d\n", getpid());
	count = 0;
	srand(time(NULL));
	n = 0;
	part = 0;

	for (i = 0; i < NR_PART; i++) {
		start = 0;
		end = 1000;
		for (j = start * MEGABYTE / sizeof(int); j < end * MEGABYTE / sizeof(int); j += 512) {
			buf[i][j] = count++;
		}
	}
	printf("memory saturated\n");
	while (1) {
		/*
		n = rand() % NR_MB;
		part = rand() % NR_PART;
		for (i = 0; i < MEGABYTE / sizeof(int); i+=512) {
			//printf("%d/%lu \n", part, n*MEGABYTE/sizeof(int)+ i);
			buf[part][n * MEGABYTE / sizeof(int) + i] = count++;
		}
		*/
		for (i = 0; i < NR_PART; i++) {
			int step = 10;
			start = end = 0;
			for (k = 0; k < NR_MB; k += step) {
				start = k;
				end = start + step;
				for (j = start * MEGABYTE / sizeof(int); j < end * MEGABYTE / sizeof(int); j += 512) {
					buf[i][j] = count++;
				}
			}
			sleep(1);
		}
	}
	return 0;
}
