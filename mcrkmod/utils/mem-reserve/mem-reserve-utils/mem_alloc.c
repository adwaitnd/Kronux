#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096 
#define MAX_CHUNK 100
int count = -1;
int *p[MAX_CHUNK];
int p_size[MAX_CHUNK];
int value = 1;
void alloc_mem(unsigned long size)
{
	int i, j;
	if (++count == MAX_CHUNK) {
		printf("max chunk reached. stop allocating\n");
		return;
	}
	p[count] = malloc(size);
	p_size[count] = size / sizeof(int);

	if (p[count] == NULL) {
		printf("cannot allocate memory \n");
		return;
	}
	for (i = 0; i <= count; i++) {
		printf("access memory chunk %d: size %lu\n", i, p_size[i] * sizeof(int));
		for (j = 0; j < p_size[i]; j += 100) {
			if (i < count && p[i][j] != i + 1) {
				printf("something wrong!!!\n");
				break;
			}
			p[i][j] = i + 1;
		}
	}
}
int stack_check(int depth)
{
	int buf[10];
	int i;
	if (depth == 100) return 1;
	for (i = 0; i < 10; i++) {
		buf[i] += stack_check(depth + 1);
	}
	return buf[0];

}

int main(int argc, char* argv[])
{
	/*
	if (argc < 2) {
		printf("arg: pid\n");
		return -1;
	}
	*/

	int pid;
	unsigned long size;

	/*
	int i;
	for (i = 0; i < 100000; i++) {
		buffer[i] = 123;
	}
	printf("%x\n", buffer);
	buffer[0] = 1;
	buffer[1000] = 1;
	buffer[50000] = 1;
	buffer[80000] = 1;
	buffer[99999] = 1;
*/
	printf("pid : %d\n", getpid());
	//mlockall(MCL_CURRENT);
	while (1) {
		printf("alloc size(# of pages): ");
		scanf("%lu", &size);
		//if (size == 0) { munlockall(); }
		if (size > 0) {
			size *= PAGE_SIZE;
			printf(" -> alloc %lu bytes\n", size);
			alloc_mem(size);
		}
		if (size == 0) stack_check(0);
	}
	return 0;
}

