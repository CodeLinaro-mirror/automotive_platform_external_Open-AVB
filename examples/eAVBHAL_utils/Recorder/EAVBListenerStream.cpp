/* Copyright (c) 2021 The Linux Foundation. All rights reserved.
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
 */

#define LOG_TAG "EAVBListenerStream"
#include <log/log.h>
#include "EAVBListenerStream.h"

#define HARDWARE_INTERFACE_ID  ("eavb")

EAVBListenerStream::EAVBListenerStream(stream_config_t &cfg)
{
    hw_get_module_by_class(AUDIO_HARDWARE_MODULE_ID, HARDWARE_INTERFACE_ID, &m_module);
    audio_hw_device_open(m_module, &m_device);
    m_device->open_input_stream(
        m_device,
        0, /* io handle unused */
        AUDIO_DEVICE_IN_DEFAULT,
        &cfg.common,
        &m_stream,
        AUDIO_INPUT_FLAG_NONE,
        nullptr, /* address unused */
        AUDIO_SOURCE_DEFAULT
    );
    m_stream->common.set_parameters(&m_stream->common, cfg.params.c_str());
    m_buffer = new uint8_t[m_stream->common.get_buffer_size(&m_stream->common)]();
}

EAVBListenerStream::~EAVBListenerStream(void)
{
    m_device->close_input_stream(m_device, m_stream);
    audio_hw_device_close(m_device);
    delete [] m_buffer;
    m_buffer = nullptr;
}

int EAVBListenerStream::read(const size_t &num_bytes)
{
    return std::move(m_stream->read(m_stream, m_buffer, num_bytes));
}

const uint8_t* EAVBListenerStream::get_buffer(void) const
{
    return m_buffer;
}