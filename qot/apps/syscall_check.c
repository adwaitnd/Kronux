// syscall_check.c
// Adwait Dongare

// test & check our custom syscalls
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>

#define __TIMELINE_NANOSLEEP 388

int main(void) {
    struct timespec t, t_wake;
    char id[] = "Hello";
    clock_gettime(CLOCK_REALTIME, &t);      // get current time
    t_wake.tv_sec = t.tv_sec + 2;
    t_wake.tv_nsec = 0;    
    printf("Hello World from process %d\n", syscall(__NR_getpid));
    printf("Timeline nanosleep returned %d\n", syscall(__TIMELINE_NANOSLEEP, id, &t_wake));
    return 0;
}