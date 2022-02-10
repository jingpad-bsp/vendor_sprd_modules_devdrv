CFLAGS=-I$(ROOTFS_INC) -I./
CC=$(CROSS_COMPILE)gcc
all: vl53l0_test vl53l0_reg vl53l0_parameter
vl53l0_test: vl53l0_test.o
	$(CC) -o vl53l0_test vl53l0_test.o $(CFLAGS) -lpthread
vl53l0_reg: vl53l0_reg.o
	$(CC) -o vl53l0_reg vl53l0_reg.o $(CFLAGS)
vl53l0_parameter: vl53l0_parameter.o
	$(CC) -o vl53l0_parameter vl53l0_parameter.o $(CFLAGS)

.PHONY: clean

clean:
	rm -f ./*.o *~ core vl53l0_test vl53l0_reg vl53l0_parameter

