KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean

all:
	$(MAKE) -C src KDIR=$(KDIR)

clean:
	$(MAKE) -C src KDIR=$(KDIR) clean
