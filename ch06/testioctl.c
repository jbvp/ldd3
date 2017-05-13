#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "scull.h"

int main(int argc, char *argv[])
{
	int fd, quantum, ret = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <device> [<quantum>]\n", argv[0]);
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror(argv[1]);
		return fd;
	}

	if (argc == 3) {
		quantum = atoi(argv[2]);
		if (ioctl(fd, SCULL_IOC_SET_QUANTUM, &quantum) < 0) {
			perror("SCULL_IOC_SET_QUANTUM");
			ret = errno;
		}
	} else {
		if (ioctl(fd, SCULL_IOC_GET_QUANTUM, &quantum) < 0) {
			perror("SCULL_IOC_GET_QUANTUM");
			ret = errno;
		} else {
			printf("quantum = %d\n", quantum);
		}
	}

	close(fd);

	return ret;
}