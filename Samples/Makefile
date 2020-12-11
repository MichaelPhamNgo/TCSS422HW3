CONFIG_MODULE_SIG=n

obj-m += procReport.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
test:
	sudo dmesg -C
	sudo insmod ./procReport.ko
	sudo rmmod ./procReport.ko
	dmesg
load:
	sudo insmod ./procReport.ko
remove:
	sudo rmmod ./procReport.ko
tail:
	tail -fn 10 /var/log/syslog

 
