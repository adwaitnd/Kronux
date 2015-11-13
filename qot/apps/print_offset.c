// print_offset.c
// Adwait Dongare

#include <stdio.h>
#include <time.h>
#include <sys/syscall.h>

#define __TIMELINE_NANOSLEEP    388
#define __SET_OFFSET            389
#define __PRINT_TIMELINE        390

#define TIMELINE_ID_SIZE 32

int main(int argc, char **argv) {
    long long offset_nsec;
    char tlid[TIMELINE_ID_SIZE] = "local_time";
    if(argc > 1) {
        sscanf(argv[1], "%lld", &offset_nsec);
    } else {
        offset_nsec = 1000000000;
    }
    syscall(__PRINT_TIMELINE, tlid);            // print previous timeline contents to dmesg
    syscall(__SET_OFFSET, tlid, &offset_nsec);  // change offset
    syscall(__PRINT_TIMELINE, tlid);            // print new timeline contents to dmesg

    return 0;
}