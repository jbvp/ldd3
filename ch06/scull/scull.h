#ifndef _SCULL_H
#define _SCULL_H

#include <linux/ioctl.h>

#define SCULL_MAJOR 0 // dynamic major by default
#define SCULL_NR_DEVS 4
#define SCULL_QUANTUM 4000
#define SCULL_QSET    1000

#define SCULL_IOC_MAGIC 'x'
#define SCULL_IOC_RESET 	_IO(SCULL_IOC_MAGIC, 0)
#define SCULL_IOC_GET_QUANTUM	_IOR(SCULL_IOC_MAGIC, 1, int)
#define SCULL_IOC_SET_QUANTUM	_IOW(SCULL_IOC_MAGIC, 2, int)
#define SCULL_IOC_GET_QSET	_IOR(SCULL_IOC_MAGIC, 3, int)
#define SCULL_IOC_SET_QSET	_IOW(SCULL_IOC_MAGIC, 4, int)
#define SCULL_IOC_MAX_NR 4

#endif