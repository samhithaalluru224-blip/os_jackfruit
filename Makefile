# ========================
# Compiler settings
# ========================
CC = gcc
CFLAGS = -Wall -pthread

# ========================
# Targets
# ========================
all: engine cpu_hog memory_hog kernel

# ========================
# User-space binaries
# ========================
engine: engine.c monitor_ioctl.h
	$(CC) $(CFLAGS) engine.c -o engine

cpu_hog: cpu_hog.c
	$(CC) $(CFLAGS) cpu_hog.c -o cpu_hog

memory_hog: memory_hog.c
	$(CC) $(CFLAGS) memory_hog.c -o memory_hog

# ========================
# Kernel module
# ========================
obj-m += monitor.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

kernel:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# ========================
# Clean
# ========================
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f engine cpu_hog memory_hog

# ========================
# CI-safe build (IMPORTANT)
# ========================
ci:
	$(CC) $(CFLAGS) engine.c -o engine
	$(CC) $(CFLAGS) cpu_hog.c -o cpu_hog
	$(CC) $(CFLAGS) memory_hog.c -o memory_hog
