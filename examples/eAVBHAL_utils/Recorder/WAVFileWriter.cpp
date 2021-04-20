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

#define LOG_TAG "WAVFileWriter"
#include <log/log.h>
#include "WAVFileWriter.h"

typedef struct
{
    // RIFF Chunk descriptor
    uint8_t  chunkID[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunkSize;
    uint8_t  format[4] = {'W', 'A', 'V', 'E'};

    // FMT Sub Chuck
    uint8_t  subChunk1ID[4] = {'f', 'm', 't', ' '};
    uint32_t subChunk1Size = 0x10;
    uint16_t audioFormat = 0x01;	// PCM
    uint16_t numberChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;

    // Data Sub Chunk
    uint8_t  subChunk2ID[4] = {'d', 'a', 't', 'a'};
    uint32_t subChunk2Size;
} wav_file_header_t;


WAVFileWriter::WAVFileWriter(const uint32_t &sample_rate, const audio_channel_mask_t &channel_mask, const audio_format_t &format, const std::string &path)
    : m_file(path, std::ios::out|std::ios::binary|std::ios::trunc)
{
    if (!m_file.is_open())
    {
        ALOGE("Error opening file test.wav");
        return;
    }
    const uint16_t channels = audio_channel_count_from_in_mask(channel_mask);
    const size_t bytes_per_sample = audio_bytes_per_sample(format);

    wav_file_header_t header;
    header.numberChannels = channels;
    header.sampleRate = sample_rate;
    header.bitsPerSample = bytes_per_sample * 8;
    header.blockAlign = channels * bytes_per_sample;
    header.byteRate = sample_rate * channels * bytes_per_sample;
    header.subChunk2Size = m_bytes_written;  //Updated after recording finished
    header.chunkSize = 4 + (8 + header.subChunk1Size) + (8 + header.subChunk2Size);  //Updated after recording finished
    m_chunk_size = header.chunkSize;

    m_file.write((const char*) &header.chunkID,        sizeof(header.chunkID));
    m_total_size_pos = m_file.tellp();
    m_file.write((const char*) &header.chunkSize,      sizeof(header.chunkSize));
    m_file.write((const char*) &header.format,         sizeof(header.format));
    m_file.write((const char*) &header.subChunk1ID,    sizeof(header.subChunk1ID));
    m_file.write((const char*) &header.subChunk1Size,  sizeof(header.subChunk1Size));
    m_file.write((const char*) &header.audioFormat,    sizeof(header.audioFormat));
    m_file.write((const char*) &header.numberChannels, sizeof(header.numberChannels));
    m_file.write((const char*) &header.sampleRate,     sizeof(header.sampleRate));
    m_file.write((const char*) &header.byteRate,       sizeof(header.byteRate));
    m_file.write((const char*) &header.blockAlign,     sizeof(header.blockAlign));
    m_file.write((const char*) &header.bitsPerSample,  sizeof(header.bitsPerSample));
    m_file.write((const char*) &header.subChunk2ID,    sizeof(header.subChunk2ID));
    m_data_size_pos = m_file.tellp();
    m_file.write((const char*) &header.subChunk2Size,  sizeof(header.subChunk2Size));
}

WAVFileWriter::~WAVFileWriter()
{
    m_chunk_size += m_bytes_written;
    m_file.seekp(m_data_size_pos);
    m_file.write((const char*) &m_bytes_written, sizeof(m_bytes_written));
    m_file.seekp(m_total_size_pos);
    m_file.write((const char*) &m_chunk_size, sizeof(m_chunk_size));
    m_file.close();
}

void WAVFileWriter::write(const uint8_t* const data, const size_t &num_bytes)
{
    m_file.write(reinterpret_cast<const char*>(data), num_bytes);
    m_bytes_written += num_bytes;
}