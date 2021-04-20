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

#define LOG_TAG "EAVBPlayer"
#include <log/log.h>
#include <signal.h>
#include <getopt.h>
#include <map>
#include "EAVBTalkerStream.h"
#include "WAVFileReader.h"

/* Supported streams */
static const std::map<std::string, stream_config_t> streams =
{
    {"MEDIA_BUS", {{.sample_rate=48000, .channel_mask=AUDIO_CHANNEL_INDEX_MASK_2, .format=AUDIO_FORMAT_PCM_16_BIT}, "BUS00_MEDIA="}},
    {"NAV_BUS", {{.sample_rate=48000, .channel_mask=AUDIO_CHANNEL_INDEX_MASK_1, .format=AUDIO_FORMAT_PCM_16_BIT}, "BUS02_NAV_GUIDANCE="}},
    {"PHONE_BUS", {{.sample_rate=24000, .channel_mask=AUDIO_CHANNEL_INDEX_MASK_1, .format=AUDIO_FORMAT_PCM_16_BIT}, "BUS03_PHONE="}}
};

/* Command line options */
static const char* name = "EAVBPlay";
static const unsigned int version = 0x00000001;
static const char *short_options = "vh";
static const struct option long_options[] =
{
    {"version", no_argument, nullptr,  'v' },
    {"help",    no_argument, nullptr,  'h' },
    {0,         0,           0,         0  }
};
void usage(void)
{
    printf("usage: %s [--version] [--help] <stream> <output path>\n", name);
}

/* Main loop */
static bool run = true;
void on_interrupt(sig_atomic_t s);
void play(stream_config_t stream, std::string path);

int main(int argc, char **argv)
{
    if (argc == 1)
    {
        usage();
        return 1;
    }

    std::string stream = "", path = "";

    while (optind < argc)
    {
        int opt = getopt_long(argc, argv, short_options, long_options, nullptr);
        if (EOF != opt)
        {  // Optional arguments
            switch(opt)
            {
            case 'v':
                printf("%s version %08x\n", name, version);
                return 0;
            case 'h':
                usage();
                return 0;
            default:
                usage();
                return 1;
            }
        }
        else
        {  // Required arguments
            switch(optind)
            {
            case 1:
                stream = argv[optind++];
                break;
            case 2:
                path = argv[optind++];
                break;
            default:
                usage();
                return 1;
            }
        }
    }

    if (stream == "" || path == "")
    {
        usage();
        return 1;
    }

    printf("Registering signal handler...\n");
    signal(SIGINT, on_interrupt);
    printf("Starting playback session...\n");
    play(streams.at(stream), path);
    return 0;
}

void on_interrupt(sig_atomic_t s)
{
    printf("Got interrupt signal, exiting playback.\n");
    run = false;
}

void play(stream_config_t stream, std::string path)
{
    printf("Creating talker object...\n");
    EAVBTalkerStream talker(stream);
    printf("Creating wav object...\n");
    WAVFileReader wav(path);

    printf("Getting output buffer...\n");
    uint8_t* buffer = talker.get_buffer();
    printf("Entering playback loop...\n");

    do
    {
        int read = wav.read(buffer, talker.buffer_size);
        if (0 < read)
        {
            ALOGI("Read %d bytes from input file  <bytes_read=%d>\n", read, wav.m_bytes_read);
            int write = talker.write(read);
            if (0 < write)
            {
                continue;
            } else {
                ALOGE("Write timed out. Is stream active?\n");
            }
        } else {
            ALOGE("Unable to read from input file <read=%d> <bytes_read=%d>\n", read, wav.m_bytes_read);
        }
    } while(run && (wav.m_bytes_read < wav.length));

    printf("Exiting playback loop...\n");
}
