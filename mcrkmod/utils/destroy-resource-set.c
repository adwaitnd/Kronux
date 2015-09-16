#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <rk_api.h>

void print_usage()
{
	printf("<usage>: destroy-resource-set -D=<rd>\n");
	printf("\t <rd> is the resource set descriptor for the resource set\n");
}

int main(int argc, char *argv[]){
	int i;
	int rd;

	rd = -1;

	if (argc < 2) {
		print_usage();
		return 0;
	}
	if (argc >= 1) {
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
	}

	if (rd == -1) {
		printf("Invalid Arguments: Please type \'destroy-resource-set -h\' for usage\n");
		return 0;
	}

	printf("Resource Descriptor: %d\n", rd);

	if (rk_resource_set_destroy(rd) < 0) {
		printf("Failed to destroy rd=%d\n", rd);
		return -1;
	}

	return 0;
}
