LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := slave
LOCAL_SRC_FILES := slave.cpp common.cpp
include $(BUILD_EXECUTABLE)
