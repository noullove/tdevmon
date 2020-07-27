obj-m += tdevmon.o

tdevmon-objs :=  src/module.o src/Device.o src/Hook.o src/Connection.o src/HashTable.o src/ScatterGather.o src/FileNameFilter.o src/lkmUtils.o src/stringUtils.o

ifndef LINUX_BUILD_DIR
	LINUX_BUILD_DIR := /lib/modules/$(shell uname -r)/build/
endif

ifndef TDEVMON_LKM_DIR
	export TDEVMON_LKM_DIR := $(shell pwd)
endif

EXTRA_CFLAGS := -fvisibility=hidden

default:
	make -C $(LINUX_BUILD_DIR) M=$(PWD) modules

modules_install:
	make -C $(LINUX_BUILD_DIR) M=$(PWD) modules_install

clean:
	make -C $(LINUX_BUILD_DIR) M=$(PWD) clean

install:
	install -D -m 755 tdevmon.ko $(LINUX_BUILD_DIR)
	install -D -m 755 bin/tdevmon /usr/local/bin