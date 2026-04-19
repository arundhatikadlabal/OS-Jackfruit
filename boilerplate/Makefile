KDIR := /lib/modules/$(shell uname -r)/build
CC   := gcc
CFLAGS := -Wall -Wextra -pthread

# Kernel module objects
obj-m += monitor.o

# Default: build everything
all: engine cpu_hog memory_hog io_pulse modules

engine: engine.c monitor_ioctl.h
	$(CC) $(CFLAGS) -o engine engine.c -lpthread

cpu_hog: cpu_hog.c
	$(CC) $(CFLAGS) -o cpu_hog cpu_hog.c

memory_hog: memory_hog.c
	$(CC) $(CFLAGS) -o memory_hog memory_hog.c

io_pulse: io_pulse.c
	$(CC) $(CFLAGS) -o io_pulse io_pulse.c

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f engine cpu_hog memory_hog io_pulse
	rm -f logs/*.log

.PHONY: all modules clean
