ifeq ($(KERNEL_PATH), kernel4.4)
PRODUCT_COPY_FILES += vendor/sprd/modules/devdrv/input/leds/init.leddrv.old.rc:vendor/etc/init/init.leddrv.rc
else
ifneq ($(TARGET_BOARD), s9863a3c10)
PRODUCT_COPY_FILES += vendor/sprd/modules/devdrv/input/leds/init.leddrv.new.rc:vendor/etc/init/init.leddrv.rc
endif
endif
