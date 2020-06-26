LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := camrec.cpp JglRecorder.cpp

LOCAL_C_INCLUDES += \
    $(TOP)/frameworks/native/include/media/openmax \
    $(TOP)/frameworks/native/include/media/hardware\
    $(TOP)/frameworks/native/include\
    $(TOP)/frameworks/av/include\
    $(TOP)/frameworks/av/media/libmediaplayerservice\
    $(TOP)/frameworks/av/include/media\
    $(TOP)/frameworks/base/include

LOCAL_SHARED_LIBRARIES := \
    libcamera_client \
    libmedia \
    libbinder \
    libcutils \
    libutils \
    liblog \
    libgui

LOCAL_SHARED_LIBRARIES += \
    libstagefright \
    libstagefright_foundation

LOCAL_CFLAGS +=-Wall -fno-short-enums -O0 -g -D___ANDROID___ $(ANDROID_API_CFLAGS)
LOCAL_CFLAGS += -Wno-unused-parameter 
LOCAL_CFLAGS += -Wno-unused-variable
LOCAL_CFLAGS += -Wno-format

LOCAL_32_BIT_ONLY := true

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS:= optional
LOCAL_MODULE := camrec
include $(BUILD_EXECUTABLE)
