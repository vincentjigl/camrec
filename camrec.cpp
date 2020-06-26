/*
 * Copyright (C) Texas Instruments - http://www.ti.com/
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#define LOG_NDEBUG 0
#define LOG_TAG "VTCTest"

#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#include <pthread.h>

#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>

#include <camera/Camera.h>
#include <camera/android/hardware/ICamera.h>
#include <media/stagefright/CameraSource.h>
#include <media/stagefright/MediaCodecSource.h>
#include "JglRecorder.h"

#include <gui/Surface.h>
#include <gui/IGraphicBufferProducer.h>
#include <gui/ISurfaceComposer.h>
#include <gui/ISurfaceComposerClient.h>
#include <gui/SurfaceComposerClient.h>

#include <ui/DisplayInfo.h>
#include <cutils/log.h>

#define VTC_LOGD printf
#define VTC_LOGE printf

using namespace android;

int mDuration = 300;
int camera_index = 0;
bool mCameraThrewError = false;
char mParamValue[100];
char mRecordFileName[256];
uint32_t mVideoBitRate      = 10000000;
uint32_t mPreviewWidth;
uint32_t mPreviewHeight;
uint32_t mFrameNum = 0;

// If seconds <  0, only the first frame is I frame, and rest are all P frames
// If seconds == 0, all frames are encoded as I frames. No P frames
// If seconds >  0, it is the time spacing (seconds) between 2 neighboring I frames


sp<Camera> camera;
sp<SurfaceComposerClient> client = NULL;
sp<SurfaceControl> surfaceControl;
sp<Surface> previewSurface;
CameraParameters params;
JglRecorder* recorder;
sp<MediaCodecSource> encoder;
int mOutputFd = -1;

//To perform better
static pthread_cond_t mCond;
static pthread_mutex_t mMutex;


int test_Preview();
class MyCameraListener: public CameraListener {
    public:
        virtual void notify(int32_t msgType, int32_t ext1, int32_t /* ext2 */) {
                        VTC_LOGD("\n\n\n notify reported an error!!!\n\n\n");

            if ( msgType & CAMERA_MSG_ERROR && (ext1 == 1)) {
                VTC_LOGD("\n\n\n Camera reported an error!!!\n\n\n");
                mCameraThrewError = true;
                pthread_cond_signal(&mCond);
            }
        }
        virtual void postData(int32_t /* msgType */,
                              const sp<IMemory>& /* dataPtr */,
                              camera_frame_metadata_t * /* metadata */){}

        virtual void postDataTimestamp(nsecs_t /* timestamp */, int32_t /* msgType */, const sp<IMemory>& /* dataPtr */){}
		virtual void postRecordingFrameHandleTimestamp(nsecs_t timestamp, native_handle_t* handle) {}
	virtual void postRecordingFrameHandleTimestampBatch(const std::vector<nsecs_t>& timestamps, const std::vector<native_handle_t*>& handles){}
};

sp<MyCameraListener> mCameraListener;

int createPreviewSurface() {
    if(client == NULL){
        client = new SurfaceComposerClient();
        //CHECK_EQ(client->initCheck(), (status_t)OK);
        if(client->initCheck() != (status_t)OK)
        VTC_LOGD(" initCheck error ");
    }


    sp<IBinder> dtoken(SurfaceComposerClient::getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain));
    DisplayInfo dinfo;
    status_t status = SurfaceComposerClient::getDisplayInfo(dtoken, &dinfo);

    printf("Display info %d %d\n", dinfo.w, dinfo.h);
    uint32_t cameraWinX = 0;
    uint32_t cameraWinY = 0;
    uint32_t cameraSurfaceWidth = mPreviewWidth;//dinfo.w*3/5;
    uint32_t cameraSurfaceHeight = mPreviewHeight;//dinfo.h*3/5;
    VTC_LOGD("\n\n jgl prex %d, prey %d, prew %d , preh %d, \n\n\n", cameraWinX, cameraWinY, cameraSurfaceWidth, cameraSurfaceHeight);

    surfaceControl = client->createSurface(String8("previewSurface"),
            cameraSurfaceWidth,
            cameraSurfaceHeight,
            HAL_PIXEL_FORMAT_RGB_565, 0);

    previewSurface = surfaceControl->getSurface();

    client->openGlobalTransaction();
    surfaceControl->setLayer(0x7fffffff);
    surfaceControl->setPosition(cameraWinX, cameraWinY);
    surfaceControl->setSize(cameraSurfaceWidth, cameraSurfaceHeight);
    surfaceControl->show();
    client->closeGlobalTransaction();

    return 0;
}

int destroyPreviewSurface() {

    if ( NULL != previewSurface.get() ) {
        previewSurface.clear();
    }

    if ( NULL != surfaceControl.get() ) {
        surfaceControl->clear();
        surfaceControl.clear();
    }

    if ( NULL != client.get() ) {
        client->dispose();
        client.clear();
    }

    return 0;
}

int startPreview() {
    mCameraThrewError = false;

    createPreviewSurface();
#if 1
    camera = Camera::connect(camera_index, String16(), -1, -1 );
#else
    status_t status = Camera::connectLegacy(camera_index, 550, String16(),
                Camera::USE_CALLING_UID, camera);
    if (status != NO_ERROR) {
        VTC_LOGE("camera connectLegacy failed");
        //return status;
    }
#endif
    if (camera.get() == NULL){
        VTC_LOGE("camera.get() =================== NULL");
        return -1;
    }

    params.unflatten(camera->getParameters());
    params.setPreviewSize(mPreviewWidth, mPreviewHeight);
    params.setPreviewFrameRate(30/*mVideoFrameRate*/);
    params.set(CameraParameters::KEY_RECORDING_HINT, CameraParameters::TRUE);// Set recording hint, otherwise it defaults to high-quality and there is not enough memory available to do playback and camera preview simultaneously!
    //sprintf(mParamValue,"%u,%u", 30/*mVideoFrameRate*/*1000, 30/*mVideoFrameRate*/*1000);
    //params.set("preview-fps-range", mParamValue);

    //if(disable_VTAB_and_VNF){
    //    VTC_LOGI("\n\n\nDisabling VSTAB & VNF (noise reduction)\n\n");
    //    params.set("vstab" , 0);
    //    params.set("vnf", 0);
    //}
    params.set("video-size", "1920x1080");
    //params.set("antibanding", "off");
    //params.setFocusMode(CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO);
    //params.set("auto-exposure", "frame-average");
    //params.set("zsl", "off");
    //params.set("video-hdr", "off");
    params.set("video-hfr", "off");
    params.set("video-hsr", "30");
    sprintf(mParamValue,"%u,%u", 30/*mVideoFrameRate*/*1000, 30/*mVideoFrameRate*/*1000);
    //sprintf(mParamValue,"%u,%u", mVideoFrameRate*1000, mVideoFrameRate*1000);
    params.set("preview-fps-range", mParamValue);
    //camera->storeMetaDataInBuffers(true);
    usleep(100*1000);
    camera->setParameters(params.flatten());
    camera->setPreviewTarget(previewSurface->getIGraphicBufferProducer());
    mCameraListener = new MyCameraListener();
    camera->setListener(mCameraListener);

    params.unflatten(camera->getParameters());
    VTC_LOGD("get(preview-fps-range) = %s\n", params.get("preview-fps-range"));
    VTC_LOGD("get(preview-fps-range-values) = %s\n", params.get("preview-fps-range-values"));
    VTC_LOGD("get(preview-size-values) = %s\n", params.get("preview-size-values"));
    VTC_LOGD("get(preview-frame-rate-values) = %s\n", params.get("preview-frame-rate-values"));
    VTC_LOGD("get(video-hfr-values) = %s\n", params.get("video-hfr-values"));
    VTC_LOGD("get(video-hsr) = %s\n", params.get("video-hsr"));
    VTC_LOGD("get(video-hfr) = %s\n", params.get("video-hfr"));

    VTC_LOGD("Starting preview\n");
    camera->startPreview();
    sleep(1);
    return 0;
}

void stopPreview() {
    if (camera.get() == NULL) return;
    camera->stopPreview();
    camera->disconnect();
    camera.clear();
    mCameraListener.clear();
    destroyPreviewSurface();
}

int startRecording() {

    if (camera.get() == NULL) return -1;

    recorder = new JglRecorder(String16("jgl"));
    recorder->init();

    camera->unlock();

    if ( recorder->setCamera(camera->remote(), camera->getRecordingProxy()) < 0 ) {
        VTC_LOGD("error while setting the camera\n");
        return -1;
    }

    if ( recorder->setVideoSource(VIDEO_SOURCE_CAMERA) < 0 ) {
        VTC_LOGD("error while configuring camera video source\n");
        return -1;
    }
#if 0
    if ( recorder->setAudioSource(AUDIO_SOURCE_DEFAULT) < 0 ) {
        VTC_LOGD("error while configuring camera audio source\n");
        return -1;
    }
#endif
    if ( recorder->setOutputFormat(OUTPUT_FORMAT_MPEG_4) < 0 ) {
        VTC_LOGD("error while configuring output format\n");
        return -1;
    }


    mOutputFd = open(mRecordFileName, O_CREAT | O_RDWR);

    if(mOutputFd < 0){
        VTC_LOGD("Error while creating video filename\n");
        return -1;
    }

    if ( recorder->setOutputFile(mOutputFd,0, 0) < 0 ) {
        VTC_LOGD("error while configuring video filename\n");

        return -1;
    }

    if ( recorder->setVideoFrameRate(30) < 0 ) {
        VTC_LOGD("error while configuring video framerate\n");
        return -1;
    }

    if ( recorder->setVideoSize(mPreviewWidth, mPreviewHeight) < 0 ) {
        VTC_LOGD("error while configuring video size\n");
        return -1;
    }

    if ( recorder->setVideoEncoder(VIDEO_ENCODER_H264) < 0 ) {
        VTC_LOGD("error while configuring video codec\n");
        return -1;
    }
#if 0
    if ( recorder->setAudioEncoder(AUDIO_ENCODER_AMR_NB) < 0 ) {
        VTC_LOGD("error while configuring audio codec\n");
        return -1;
    }
#endif
    if ( recorder->setPreviewSurface(previewSurface->getIGraphicBufferProducer()) < 0 ) {
        VTC_LOGD("error while configuring preview surface\n");
        return -1;
    }

    sprintf(mParamValue,"video-param-encoding-bitrate=%u", mVideoBitRate);
    String8 bit_rate(mParamValue);
    if ( recorder->setParameters(bit_rate) < 0 ) {
        VTC_LOGD("error while configuring bit rate\n");
        return -1;
    }

    sprintf(mParamValue,"video-param-i-frames-interval=%u", 100);
    String8 interval(mParamValue);
    if ( recorder->setParameters(interval) < 0 ) {
        VTC_LOGD("error while configuring i-frame interval\n");
        return -1;
    }

    if ( recorder->prepare() < 0 ) {
        VTC_LOGD("recorder prepare failed\n");
        return -1;
    }

    printf("-----------line %d, start record \n", __LINE__);
    if ( recorder->start(&encoder) < 0 ) {
    //if ( recorder->start() < 0 ) {
        VTC_LOGD("recorder start failed\n");
        return -1;
    }
    //printf("-----------line %d, start record, encoder %p \n", __LINE__, encoder);

    return 0;
}

int stopRecording() {

    VTC_LOGD("stopRecording()");
    if (camera.get() == NULL) return -1;

    if ( recorder->stop() < 0 ) {
        VTC_LOGD("recorder failed to stop\n");
        return -1;
    }

    if(recorder)
        delete recorder;

    if ( 0 < mOutputFd ) {
        close(mOutputFd);
    }

    return 0;
}

int test() {
    startPreview();
    int64_t start = systemTime();
	
    startRecording();
    MediaBuffer* mbuf;
    while((encoder->read(&mbuf)==OK) && (mFrameNum <= mDuration*30)){

        printf("-----------line %d, get one frame \n", __LINE__);
        write(mOutputFd, (const uint8_t *)mbuf->data() + mbuf->range_offset(), mbuf->range_length());
        mbuf->release();
        mFrameNum++;
    }
    int64_t end = systemTime();
	stopRecording();
    stopPreview();

    return 0;
}


void printUsage() {
    printf("\n\nApplication for testing VTC requirements");
    printf("\n11 - test camera preview ");
    printf("\n-w: Preview/Record Width. Default = %d", mPreviewWidth);
    printf("\n-h: Preview/Record Height. Default = %d", mPreviewHeight);
    printf("\n-d: Recording time in secs. Default = %d seconds", mDuration);

    printf("\n\n\n");

}

int main (int argc, char* argv[]) {
    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();
    pthread_mutex_init(&mMutex, NULL);
    pthread_cond_init(&mCond, NULL);

    int opt;
    const char* const short_options = "a:n:w:l:h:d:s:i:I:b:f:B:F:p:q:M:c:t:v:";
    const struct option long_options[] = {
        {"record_filename", 1, NULL, 'n'},
        {"width", 1, NULL, 'w'},
        {"height", 1, NULL, 'h'},
        {"record_duration", 1, NULL, 'd'},
        {"slice_size_MB", 1, NULL, 'M'},
        {NULL, 0, NULL, 0}
    };

    sprintf(mRecordFileName,  "/sdcard/output.mp4");

    if (argc < 2){
        printUsage();
        return 0;
    }

    while((opt = getopt_long_only(argc, argv, short_options, long_options, NULL)) != -1) {
        switch(opt) {
            case 'w':
                mPreviewWidth = atoi(optarg);
                break;
            case 'h':
                mPreviewHeight = atoi(optarg);
                break;
			case 'n':
				strcpy(mRecordFileName, optarg);
				break;
				case 'b':
					mVideoBitRate = atoi(optarg);
					break;
            case 'd':
                mDuration = atoi(optarg);
                break;
            case '?':
                VTC_LOGE("\nError - No such option: `%c'\n\n", optopt);
                return -1;
        }
    }
    test();
    pthread_mutex_destroy(&mMutex);
    pthread_cond_destroy(&mCond);
    return 0;
}

