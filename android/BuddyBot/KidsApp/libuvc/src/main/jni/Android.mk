PROJ_PATH := $(call my-dir)
include $(CLEAR_VARS)

# The order of inclusion matters for dependencies
include $(PROJ_PATH)/libusb/android/jni/Android.mk
include $(PROJ_PATH)/libjpeg-turbo-1.5.0/Android.mk
include $(PROJ_PATH)/libuvc/android/jni/Android.mk
include $(PROJ_PATH)/UVCCamera/Android.mk
