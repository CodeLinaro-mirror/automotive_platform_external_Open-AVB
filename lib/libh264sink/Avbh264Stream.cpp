/* Copyrights (c) 2016, The Linux Foundation. All rights reserved.
 * "Not a Contribution."
 */

/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "AVBh264Stream"
#include <inttypes.h>
#include <utils/Log.h>


#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <media/ICrypto.h>
#include <media/IMediaHTTPService.h>
#include <media/IMediaPlayerService.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AString.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/MediaCodecList.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/NuMediaExtractor.h>
#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/Surface.h>
#include <ui/DisplayInfo.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <semaphore.h>
#include <poll.h>
#include <pthread.h>
#include "Avbh264Stream.h"

#define FRAME_BUFFER_SIZE  600000 //600kb
sem_t threadInitSem;
int DataSock;

#if defined( __cplusplus )
extern "C"
{
#endif /* end of macro __cplusplus */

int Avbh264DataReceiveEndpoint(void) {
    struct sockaddr_un addr;

    int sockfd = 0, connfd = 0;
    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
       ALOGE("socket error");
       return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path,H264_SOCKET_PATH, sizeof(addr.sun_path)-1);
    unlink(H264_SOCKET_PATH);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
       ALOGE("bind error");
       return -1;
    }

    if (listen(sockfd, 5) == -1) {
       ALOGE("listen error");
       return -1;
    }

    if (sem_post(&threadInitSem)!=0){
       ALOGE("failed to post the semaphore");
       return -1;
    }

    connfd = accept(sockfd, (struct sockaddr*)NULL, NULL);
    if (connfd < 0){
       ALOGE("failed to accept the connection");
       return -1;
    }
    return connfd;
}

namespace android {

struct CodecState {
    sp<MediaCodec> mCodec;
    Vector<sp<ABuffer> > mInBuffers;
    Vector<sp<ABuffer> > mOutBuffers;
    bool mSignalledInputEOS;
    bool mSawOutputEOS;
    int64_t mNumBuffersDecoded;
    int64_t mNumBytesDecoded;
    bool mIsAudio;
};

}  // namespace android

static int decode(const android::sp<android::ALooper> &looper,const android::sp<android::Surface> &surface) {
    using namespace android;

    static int64_t kTimeout = 500ll;
    status_t err;
    struct pollfd fds;
    int nfds = 1 ,timeout ;


    KeyedVector<size_t, CodecState> stateByTrack;

    sp<AMessage> format = new AMessage;
    format->setString("mime",MEDIA_MIMETYPE_VIDEO_AVC);
    format->setInt32("height",1080);
    format->setInt32("width",1920);
    format->setInt32("arbitrary_bytes", 1);

    CodecState *state = &stateByTrack.editValueAt(stateByTrack.add(0, CodecState()));

    state->mNumBytesDecoded = 0;
    state->mNumBuffersDecoded = 0;
    state->mCodec = MediaCodec::CreateByType(looper,  MEDIA_MIMETYPE_VIDEO_AVC, false);

    CHECK(state->mCodec != NULL);

    err = state->mCodec->configure(format, surface,NULL , 0);

    CHECK_EQ(err, (status_t)OK);

    state->mSignalledInputEOS = false;
    state->mSawOutputEOS = false;

    CHECK(!stateByTrack.isEmpty());

    int64_t startTimeUs = ALooper::GetNowUs();


    sp<MediaCodec> codec = state->mCodec;

    CHECK_EQ((status_t)OK, codec->start());

    CHECK_EQ((status_t)OK, codec->getInputBuffers(&state->mInBuffers));
    CHECK_EQ((status_t)OK, codec->getOutputBuffers(&state->mOutBuffers));

    ALOGV("got %zu input and %zu output buffers",
              state->mInBuffers.size(), state->mOutBuffers.size());

    DataSock = Avbh264DataReceiveEndpoint();
    if (DataSock <= 0) {
        ALOGE("Unable to accept the connection from the client\n");
        return -1;
    }
    fds.fd = DataSock;
    fds.events = POLLIN;
    timeout = 10 * 1000;

    bool sawInputEOS = false;
    static int64_t timeUs ;

    for (;;) {
        if (!sawInputEOS) {
                size_t trackIndex = 0;

                CodecState *state = &stateByTrack.editValueFor(trackIndex);

                size_t index;
                err = state->mCodec->dequeueInputBuffer(&index, kTimeout);
                if (err == OK) {
                    ALOGV("filling input buffer %zu", index);
                    int rc = poll(&fds,nfds,timeout);
                    if(rc <= 0) {
                       printf("failed to wait on the poll or timedout\n");
                       close(DataSock);
                       ALOGE("Reached end of stream\n");
                       sawInputEOS = true;
                       continue ;
                     }

                    const sp<ABuffer> &buffer = state->mInBuffers.itemAt(index);
                    int n = read(DataSock,buffer->data(),FRAME_BUFFER_SIZE);
                    printf("data received at this time is %d\n",n);
                    if (n <= 0) {
                        close(DataSock);
                        ALOGE("Reached end of stream\n");
                        sawInputEOS = true;
                        continue ;
                    }


                    uint32_t bufferFlags = 0;

                    err = state->mCodec->queueInputBuffer(
                            index,
                            0,
                            n,
                            timeUs,
                            bufferFlags);
                    timeUs += 40000;

                    CHECK_EQ(err, (status_t)OK);

                } else {
                    CHECK_EQ(err, -EAGAIN);
                }


        } else {
            for (size_t i = 0; i < stateByTrack.size(); ++i) {
                CodecState *state = &stateByTrack.editValueAt(i);

                if (!state->mSignalledInputEOS) {
                    size_t index;
                    status_t err =
                        state->mCodec->dequeueInputBuffer(&index, kTimeout);

                    if (err == OK) {
                        ALOGV("signalling input EOS on track %zu", i);

                        err = state->mCodec->queueInputBuffer(
                                index,
                                0 /* offset */,
                                0 /* size */,
                                0ll /* timeUs */,
                                MediaCodec::BUFFER_FLAG_EOS);

                        CHECK_EQ(err, (status_t)OK);

                        state->mSignalledInputEOS = true;
                    } else {
                        CHECK_EQ(err, -EAGAIN);
                    }
                }
            }
        }

        bool sawOutputEOSOnAllTracks = true;
        for (size_t i = 0; i < stateByTrack.size(); ++i) {
            CodecState *state = &stateByTrack.editValueAt(i);
            if (!state->mSawOutputEOS) {
                sawOutputEOSOnAllTracks = false;
                break;
            }
        }

        if (sawOutputEOSOnAllTracks) {
            break;
        }

        for (size_t i = 0; i < stateByTrack.size(); ++i) {
            CodecState *state = &stateByTrack.editValueAt(i);

            if (state->mSawOutputEOS) {
                continue;
            }

            size_t index;
            size_t offset;
            size_t size;
            int64_t presentationTimeUs;
            uint32_t flags;
            status_t err = state->mCodec->dequeueOutputBuffer(
                    &index, &offset, &size, &presentationTimeUs, &flags,
                    kTimeout);

            if (err == OK) {
                ALOGV("draining output buffer %zu, time = %lld us",
                      index, (long long)presentationTimeUs);

                ++state->mNumBuffersDecoded;
                state->mNumBytesDecoded += size;

                if (surface == NULL ) {
                    err = state->mCodec->releaseOutputBuffer(index);
                }
                else {
                    err = state->mCodec->renderOutputBufferAndRelease(index);
                }

                CHECK_EQ(err, (status_t)OK);

                if (flags & MediaCodec::BUFFER_FLAG_EOS) {
                    ALOGV("reached EOS on output.");

                    state->mSawOutputEOS = true;
                }
            } else if (err == INFO_OUTPUT_BUFFERS_CHANGED) {
                ALOGV("INFO_OUTPUT_BUFFERS_CHANGED");
                CHECK_EQ((status_t)OK,
                         state->mCodec->getOutputBuffers(&state->mOutBuffers));

                ALOGV("got %zu output buffers", state->mOutBuffers.size());
            } else if (err == INFO_FORMAT_CHANGED) {
                sp<AMessage> format;
                CHECK_EQ((status_t)OK, state->mCodec->getOutputFormat(&format));

                ALOGV("INFO_FORMAT_CHANGED: %s", format->debugString().c_str());
            } else {
                CHECK_EQ(err, -EAGAIN);
            }
        }
    }

    int64_t elapsedTimeUs = ALooper::GetNowUs() - startTimeUs;

    for (size_t i = 0; i < stateByTrack.size(); ++i) {
        CodecState *state = &stateByTrack.editValueAt(i);

        CHECK_EQ((status_t)OK, state->mCodec->release());

        if (state->mIsAudio) {
            printf("track %zu: %lld bytes received. %.2f KB/sec\n",
                   i,
                   (long long)state->mNumBytesDecoded,
                   state->mNumBytesDecoded * 1E6 / 1024 / elapsedTimeUs);
        } else {
            printf("track %zu: %lld frames decoded, %.2f fps. %lld"
                    " bytes received. %.2f KB/sec\n",
                   i,
                   (long long)state->mNumBuffersDecoded,
                   state->mNumBuffersDecoded * 1E6 / elapsedTimeUs,
                   (long long)state->mNumBytesDecoded,
                   state->mNumBytesDecoded * 1E6 / 1024 / elapsedTimeUs);
        }
    }

    return 0;
}

void* Avbh264sink(void *arg)
{
    arg = NULL;
    using namespace android;

    ProcessState::self()->startThreadPool();

    DataSource::RegisterDefaultSniffers();

    sp<ALooper> looper = new ALooper;
    looper->start();

    sp<SurfaceComposerClient> composerClient;
    sp<SurfaceControl> control;
    sp<Surface> surface;

    composerClient = new SurfaceComposerClient;
    CHECK_EQ(composerClient->initCheck(), (status_t)OK);

    sp<IBinder> display(SurfaceComposerClient::getBuiltInDisplay(
                         ISurfaceComposer::eDisplayIdMain));
    DisplayInfo info;
    SurfaceComposerClient::getDisplayInfo(display, &info);
    ssize_t displayWidth = info.w;
    ssize_t displayHeight = info.h;

    ALOGV("display is %zd x %zd\n", displayWidth, displayHeight);

    control = composerClient->createSurface(
                String8("A Surface"),
                displayWidth,
                displayHeight,
                PIXEL_FORMAT_RGB_565,
                0);

    CHECK(control != NULL);
    CHECK(control->isValid());

    SurfaceComposerClient::openGlobalTransaction();
    CHECK_EQ(control->setLayer(INT_MAX), (status_t)OK);
    CHECK_EQ(control->show(), (status_t)OK);
    SurfaceComposerClient::closeGlobalTransaction();

    surface = control->getSurface();
    CHECK(surface != NULL);

    decode(looper, surface);

    composerClient->dispose();

    looper->stop();
    pthread_exit((void*)0);

    return NULL;
}

int Avbh264streamInitialize()
{
        pthread_t tid;
        int rc;

        if (sem_init(&threadInitSem,0,0) < 0){
            ALOGE("semaphore initialization failed");
            return -1;
        }

        rc = pthread_create(&tid, NULL, Avbh264sink, NULL);
        if (rc!=0){
            ALOGE(" Avbh264sinkthread creation failed...\n ");
            return -1;
        }

        if (sem_wait(&threadInitSem)!= 0){
            ALOGE("waiting on semaphore failed");
            return -1;
        }

        return 0;
}
int Avbh264ConnectToStreamSource(void)
{
        struct sockaddr_un addr;
        int sockfd;
        if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
                ALOGE("socket error");
                return -1;
        }
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, H264_SOCKET_PATH, sizeof(addr.sun_path)-1);
        if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
                ALOGE("connect error");
                return -1;
        }

       return sockfd;
}


#ifdef __cplusplus
}
#endif /* __cplusplus*/
