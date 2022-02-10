# validate lookup result
ifeq ($(KDIR),)
$(error No KDIR found for platform $(TARGET_PLATFORM))
endif

ifeq ($(BUILD),debug)
ccflags-y += -DDEBUG
endif

ccflags-y += -I$(INC_PATH)/inc

#stmvl53l0
#Each configuration option enables a list of files.
obj-m += stmvl53l0.o
stmvl53l0-objs := stmvl53l0_module.o \
                stmvl53l0_module-i2c.o \
                stmvl53l0_module-cci.o \
                src/vl53l0_api_calibration.o \
                src/vl53l0_api_core.o \
                src/vl53l0_api_histogram.o \
                src/vl53l0_api_ranging.o \
                src/vl53l0_api_strings.o \
                src/vl53l0_api.o \
                src/vl53l0_platform.o \
                src/vl53l0_i2c_platform.o \
                src/vl53l0_port_i2c.o


modules:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR) $@

clean:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR) $@

kernelrelease:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) $@