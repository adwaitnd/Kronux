// sleeper.c
// Adwait Dongare

#include <stdio.h>
#include <time.h>

#define __TIMELINE_NANOSLEEP    388
#define __SET_OFFSET            389
#define __PRINT_TIMELINE        390
#define TIMELINE_ID_SIZE 32

#define NSEC_PER_SEC 1000000000

struct timespec ns_to_timespec(long long nsec)
{
    struct timespec ts;
    if (!nsec)
        return (struct timespec) {0, 0};
    ts.tv_nsec = (long) nsec % NSEC_PER_SEC;
    ts.tv_sec = nsec / NSEC_PER_SEC;
    return ts;
}

int main(int argc, char **argv) {
    struct timespec t_now, t_next;
    unsigned long t, count;
    char tlid[TIMELINE_ID_SIZE] = "local_time";
    if(argc > 1) {
        if(sscanf(argv[1], "%lu", &t) != 1 || t==0) t = 2;              // no non-zero values allowed
    } else t = 2;
    count = 0;
    printf("[sleeper] PID: %d sleeping for %ld secs on timeline \"%s\"\n", getpid(), t, tlid);
    while(1) {
        clock_gettime(CLOCK_REALTIME, &t_now);      // get current time
        t_next.tv_sec = t_now.tv_sec + t;           // start at next sec
        t_next.tv_nsec = 0;
        count++;
        printf("[sleeper] (%lu) CLOCK_REALTIME, current start: %ld.%09lu, next start: %ld.%09lu\n", count, t_now.tv_sec, t_now.tv_nsec, t_next.tv_sec, t_next.tv_nsec);
        if(syscall(__TIMELINE_NANOSLEEP, tlid, &t_next) == -52) {
            printf("[sleeper] missed deadline by > 1msec\n");
        }
    }
    return 0;
}