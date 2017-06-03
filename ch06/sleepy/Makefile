ifneq ($(KERNELRELEASE),)

	obj-m := sleepy.o
	CFLAGS_sleepy.o := -DDEBUG

else

	KERNELDIR ?= /lib/modules/$(shell uname -r)/build

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

endif
