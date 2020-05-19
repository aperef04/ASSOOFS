obj-m := assoofs.o
KERNEL = $(shell uname -r)

all: ko mkassoofs

ko:
	make -C /lib/modules/$(KERNEL)/build M=$(PWD) modules

mkassoofs_SOURCES:
	mkassoofs.c assoofs.h

clean:
	make -C /lib/modules/$(KERNEL)/build M=$(PWD) clean
	rm mkassoofs