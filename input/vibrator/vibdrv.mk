ifeq ($(KERNEL_PATH), kernel4.4)
PRODUCT_COPY_FILES += vendor/sprd/modules/devdrv/input/vibrator/init.vibdrv.old.rc:vendor/etc/init/init.vibdrv.rc
else
PRODUCT_PACKAGES += vibrator.$(TARGET_BOARD_PLATFORM)
PRODUCT_COPY_FILES += vendor/sprd/modules/devdrv/input/vibrator/init.vibdrv.new.rc:vendor/etc/init/init.vibdrv.rc
endif
