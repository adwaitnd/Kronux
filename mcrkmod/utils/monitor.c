#include <stdio.h>
#include <string.h>
#include <rk_api.h>

void print_usage()
{
	printf("<usage>: monitor -D=<rd>\n");
	printf("\t <rd> is the resource set descriptor to be monitored\n");
}

int main(int argc, char *argv[])
{

	int i;
	int rd;
	unsigned long long prev;
	unsigned long min, max;

	if (argc < 2) {
		print_usage();
		return 0;
	}
	for(i=1; i<argc; i++) {
		if(strlen(argv[i])>3) {
			if(argv[i][1] == 'D') {
				rd = atoi(&argv[i][3]);
			}
		}
		if(strlen(argv[i])>=2) {
			if(argv[i][1] == '?' || argv[i][1] == 'h' || argv[i][1] == 'H') {
				print_usage();
				return 0;
			}
		}	
	}
	

	printf("Monitoring rd %d\n", rd);

	while(1)
	{
		rk_getcpursv_prev_used_ticks(rd, &prev);
		rk_getcpursv_min_utilization(rd, &min);
		rk_getcpursv_max_utilization(rd, &max);

		printf("Previous Period CPU Usage: %llu (nanoseconds)  Min. Utilization: %lu Max. Utilization: %lu (for Resource Descriptor %d)",
			prev, min, max, rd);
		fflush(stdout);

		sleep(1);
		
		for(i=0; i<80; i++)
			printf("\b");
	}
	return 0;
}

