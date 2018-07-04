/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include "openavb_platform_pub.h"
#include "openavb_filewriter.h"

#define	AVB_LOG_COMPONENT	"filewriter"
#include "openavb_log_pub.h"

#define MAX_RING_ITEM_COUNT 1000

#define writingThread_THREAD_STK_SIZE THREAD_STACK_SIZE

THREAD_TYPE(writingThread);

typedef struct {
    uint8_t* data;
    int bufferSize;
    int dataSize;
    int readPos;
    int writePos;
} RingBuffer;

typedef struct {
    RingBuffer ringBuffer;

    FILE *pFile;

    MUTEX_HANDLE(dataMutex);
    COND_HANDLE(dataCond);
    THREAD_DEFINITON(writingThread);

    bool writingThreadRunning;
} Filewriter;

static bool ringbuffer_init(RingBuffer* rb, int itemSize) {
    rb->bufferSize = MAX_RING_ITEM_COUNT * itemSize;
    rb->data = malloc(rb->bufferSize);
    if(!rb->data) {
        return FALSE;
    }
    rb->dataSize = 0;
    rb->readPos = 0;
    rb->writePos = 0;
    return TRUE;
}

static void ringbuffer_dtor(RingBuffer* rb) {
    if(rb->data) {
        free(rb->data);
        rb->data = NULL;
    }
}

static bool ringbuffer_write(Filewriter* fw, RingBuffer* rb, uint8_t* data, int size) {
    // Check if we have enough space left in the ringbuffer
    int currentDataSize = 0;
    MUTEX_LOCK_ALT(fw->dataMutex);
    currentDataSize =  rb->dataSize;
    MUTEX_UNLOCK_ALT(fw->dataMutex);
    if (size > (rb->bufferSize - currentDataSize)) {
        AVB_LOG_ERROR("filewriter: ringbuffer full");
        return FALSE;
    }

    // Check if data can be appended in one write
    if (size <= (rb->bufferSize - rb->writePos)) {
         memcpy(rb->data + rb->writePos, data, size);
         rb->writePos += size;

         if (rb->writePos == rb->bufferSize) {
             rb->writePos = 0;
         }
    } else {
        int part1Len = rb->bufferSize - rb->writePos;
        int part2Len = size - part1Len;
        memcpy(rb->data + rb->writePos, data, part1Len);
        memcpy(rb->data, data + part1Len, part2Len);
        rb->writePos = part2Len;
    }

    return TRUE;
}

static bool ringbuffer_readcontiguous(RingBuffer* rb, int maxRead, uint8_t** data, int* size) {
    // Check how much data we can read in one shot without wrapping around ring
    // buffer
    if (maxRead <= (rb->bufferSize - rb->readPos)) {
        *size = maxRead;
    } else {
        *size = rb->bufferSize - rb->readPos;
    }
    *data = rb->data + rb->readPos;

    rb->readPos += *size;
    if (rb->readPos == rb->bufferSize) {
        rb->readPos = 0;
    }

    return TRUE;
}

static void *writingThreadFn(void *pv) {
    Filewriter* fw = (Filewriter*) pv;
    COND_CREATE_ERR();

    AVB_LOG_INFO("writingThreadFn started");

    MUTEX_LOCK_ALT(fw->dataMutex);
    do {
        if (fw->ringBuffer.dataSize == 0) {
            if (!fw->writingThreadRunning) {
                // No more data and avb stream is closing, exit writer loop
                break;
            }
            COND_WAIT(fw->dataCond, fw->dataMutex);
        }

        // Take a snapshot of how much data is currently available
        int availableData = fw->ringBuffer.dataSize;
        MUTEX_UNLOCK_ALT(fw->dataMutex);

        uint8_t* data;
        int readLen;
        ringbuffer_readcontiguous(&fw->ringBuffer, availableData, &data, &readLen);

        fwrite((void*)data, 1, (size_t)readLen, fw->pFile);
        fflush(fw->pFile);

        MUTEX_LOCK_ALT(fw->dataMutex);
        fw->ringBuffer.dataSize -= readLen;
        // mutex unlock at beginning of loop
    } while (1);
    MUTEX_UNLOCK_ALT(fw->dataMutex);

    AVB_LOG_INFO("writingThreadFn finished");

    return NULL;
}


void* filewriter_init(int itemSize, char* path){
    Filewriter* filewriter = malloc(sizeof(Filewriter));

    if (!filewriter) {
        return NULL;
    }

    if (!ringbuffer_init(&filewriter->ringBuffer, itemSize)) {
        AVB_LOG_ERROR("Failed to initialize ringbuffer");
        free(filewriter);
        return NULL;
    }

    filewriter->pFile = fopen(path, "wb");
    if (!filewriter->pFile) {
        AVB_LOGF_ERROR("Unable to open input file: %s", path);
        ringbuffer_dtor(&filewriter->ringBuffer);
        free(filewriter);
        return NULL;
    }

    MUTEX_CREATE_ALT(filewriter->dataMutex);

    {
        COND_CREATE_ERR();
        COND_CREATE(filewriter->dataCond);
        COND_LOG_ERR("failed to create dataCond");
        // cleanup
    }

    {
        bool errResult;
        filewriter->writingThreadRunning = true;
        THREAD_CREATE(writingThread, filewriter->writingThread, NULL, writingThreadFn, filewriter);
        THREAD_CHECK_ERROR(filewriter->writingThread, "Thread / task creation failed", errResult);
        // cleanup
    }

    return filewriter;
}

bool filewriter_write(void* ctx, uint8_t* data, int size) {
    Filewriter* fw = (Filewriter*) ctx;
    COND_CREATE_ERR();

    if (!ringbuffer_write(fw, &fw->ringBuffer, data, size)) {
        AVB_LOG_ERROR("ringbuffer_write failed");
        return false;
    }

    // Notify writer thread that data is available to be written to disk
    MUTEX_LOCK_ALT(fw->dataMutex);
    fw->ringBuffer.dataSize += size;
    COND_SIGNAL(fw->dataCond);
    MUTEX_UNLOCK_ALT(fw->dataMutex);
    return true;
}


bool filewriter_close(void* ctx) {
    Filewriter* fw = (Filewriter*) ctx;
    COND_CREATE_ERR();
    // Wait for thread to finish writing all the data to disk
    MUTEX_LOCK_ALT(fw->dataMutex);
    fw->writingThreadRunning = false;
    COND_SIGNAL(fw->dataCond);
    MUTEX_UNLOCK_ALT(fw->dataMutex);

    THREAD_JOIN(fw->writingThread, NULL);

    // cleanup
    ringbuffer_dtor(&fw->ringBuffer);
    free(fw);
    return true;
}