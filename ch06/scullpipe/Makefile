ifneq ($(KERNELRELEASE),)

	obj-m := scullpipe.o
	CFLAGS_scullpipe.o := -DDEBUG

else

	KERNELDIR ?= /lib/modules/$(shell uname -r)/build

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

endif
