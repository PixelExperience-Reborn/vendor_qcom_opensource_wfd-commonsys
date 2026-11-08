/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
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
 * */

#include "aacDecode.h"
#include "aacdecoder_lib.h"

#include <utils/Log.h>
#include <limits.h>
#include <new>
#include <string.h>

aacDecode::aacDecode() {
    p_aacHandle = NULL;
    p_aacInfo = NULL;
    memset(&s_aacConfig, 0, sizeof(s_aacConfig));
}

aacDecode::~aacDecode() {
    if (p_aacHandle) {
        aacDecoder_Close(static_cast<HANDLE_AACDECODER>(p_aacHandle));
        p_aacHandle = NULL;
    }
    p_aacInfo = NULL; // Stream info belongs to the decoder, not this wrapper.
}

bool aacDecode::aacConfigure(aacConfigType *p_aacConfig) {
    if (!p_aacConfig || p_aacConfig->n_channels < 1 ||
        p_aacConfig->n_channels > 6 || p_aacConfig->n_sampleRate == 0 ||
        p_aacConfig->n_bitsPerSample != sizeof(INT_PCM) * 8) {
        ALOGE("Unsupported WFD AAC decoder PCM configuration");
        return false;
    }

    HANDLE_AACDECODER candidate = aacDecoder_Open(TT_MP4_ADTS, 1);
    if (!candidate) {
        ALOGE("Failed to open WFD AAC decoder");
        return false;
    }
    CStreamInfo *info = aacDecoder_GetStreamInfo(candidate);
    if (!info) {
        ALOGE("Failed to get WFD AAC stream info");
        aacDecoder_Close(candidate);
        return false;
    }

    AAC_DECODER_ERROR err = aacDecoder_SetParam(candidate,
                                                AAC_DRC_REFERENCE_LEVEL, 64);
    if (err == AAC_DEC_OK)
        err = aacDecoder_SetParam(candidate, AAC_DRC_ATTENUATION_FACTOR, 127);
    if (err == AAC_DEC_OK)
        err = aacDecoder_SetParam(candidate, AAC_PCM_MAX_OUTPUT_CHANNELS,
                                  p_aacConfig->n_channels);
    if (err != AAC_DEC_OK) {
        ALOGE("Failed to configure WFD AAC decoder: %d", err);
        aacDecoder_Close(candidate);
        return false;
    }

    if (p_aacHandle)
        aacDecoder_Close(static_cast<HANDLE_AACDECODER>(p_aacHandle));
    p_aacHandle = candidate;
    p_aacInfo = info;
    memcpy(&s_aacConfig, p_aacConfig, sizeof(s_aacConfig));
    return true;
}

bool aacDecode::aacDecodeFrame(unsigned char *p_buffer, unsigned int n_size) {
    if (!p_buffer || n_size < 7 || n_size > INT_MAX || !p_aacHandle) {
        ALOGE("Invalid WFD AAC decoder state/buffer");
        return false;
    }

    // This 2018 private API has only ONE input/output pointer and ONE byte
    // capacity. The compressed input and decoded PCM must BOTH fit n_size.
    // Use a separate aligned scratch buffer to avoid aliasing the compressed
    // bytes with the decoder output. Fail rather than overrun the caller.
    const unsigned int sample_capacity = n_size / sizeof(INT_PCM);
    if (sample_capacity == 0 || sample_capacity > INT_MAX)
        return false;
    INT_PCM *pcm = new (std::nothrow) INT_PCM[sample_capacity];
    if (!pcm) {
        ALOGE("Failed to allocate WFD AAC decode scratch");
        return false;
    }

    HANDLE_AACDECODER decoder = static_cast<HANDLE_AACDECODER>(p_aacHandle);
    UINT sizes[] = {static_cast<UINT>(n_size)};
    UINT remaining[] = {static_cast<UINT>(n_size)};
    UCHAR *input[] = {p_buffer};

    AAC_DECODER_ERROR err = aacDecoder_Fill(decoder, input, sizes, remaining);
    if (err != AAC_DEC_OK || remaining[0] != 0) {
        ALOGE("WFD AAC decoder did not consume the compressed input");
        delete[] pcm;
        return false;
    }

    err = aacDecoder_DecodeFrame(decoder, pcm,
                                 static_cast<INT>(sample_capacity), 0);
    if (err != AAC_DEC_OK) {
        ALOGE("Failed to decode WFD AAC frame: %d", err);
        delete[] pcm;
        return false;
    }

    CStreamInfo *info = aacDecoder_GetStreamInfo(decoder);
    // A malformed/changed stream must not overrun caller-owned memory.
    if (!info || info->frameSize <= 0 || info->numChannels <= 0 ||
        info->numChannels > 6 ||
        info->frameSize > static_cast<INT>(sample_capacity) / info->numChannels) {
        ALOGE("Decoded WFD AAC PCM exceeds caller buffer capacity");
        delete[] pcm;
        return false;
    }

    const size_t output_samples = static_cast<size_t>(info->frameSize) *
                                  static_cast<size_t>(info->numChannels);
    memcpy(p_buffer, pcm, output_samples * sizeof(INT_PCM));
    delete[] pcm;
    p_aacInfo = info;
    return true;
}
