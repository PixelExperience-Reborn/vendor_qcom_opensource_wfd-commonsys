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

#include "aacEncode.h"
#include "aacenc_lib.h"

#include <utils/Log.h>
#include <limits.h>
#include <new>
#include <string.h>

/*
 * This wrapper is an old private WFD ABI. Do not change class layout: older
 * vendor consumers allocate aacEncode using the size from aacEncode.h.
 */
struct aacInfo {
    AACENC_BufDesc inBuff = {};
    AACENC_BufDesc outBuff = {};
    AACENC_InArgs inArg = {};
    AACENC_OutArgs outArg = {};
};

aacEncode::aacEncode() {
    p_aacHandle = NULL;
    p_aacInfo = NULL;
    memset(&s_aacConfig, 0, sizeof(s_aacConfig));
}

aacEncode::~aacEncode() {
    if (p_aacHandle) {
        HANDLE_AACENCODER encoder = static_cast<HANDLE_AACENCODER>(p_aacHandle);
        if (aacEncClose(&encoder) != AACENC_OK) {
            ALOGE("Failed to close WFD AAC encoder");
        }
        p_aacHandle = NULL;
    }
    delete static_cast<aacInfo*>(p_aacInfo);
    p_aacInfo = NULL;
}

bool aacEncode::aacConfigure(aacConfigType *p_aacConfig) {
    if (!p_aacConfig ||
        (p_aacConfig->n_channels != 1 && p_aacConfig->n_channels != 2) ||
        p_aacConfig->n_sampleRate == 0 || p_aacConfig->n_bitrate == 0 ||
        p_aacConfig->n_bitsPerSample != 16 ||
        sizeof(INT_PCM) != sizeof(int16_t)) {
        ALOGE("Unsupported WFD AAC configuration (requires mono/stereo PCM16)");
        return false;
    }

    // Prepare the candidate before replacing any working instance.
    aacInfo *info = new (std::nothrow) aacInfo{};
    if (!info) {
        ALOGE("Failed to allocate WFD AAC encoder state");
        return false;
    }

    HANDLE_AACENCODER encoder = NULL;
    AACENC_ERROR err = aacEncOpen(&encoder, 0x01 /* AAC encoder */, p_aacConfig->n_channels);
    if (err != AACENC_OK || !encoder) {
        ALOGE("Failed to open WFD AAC encoder: %d", err);
        if (encoder)
            aacEncClose(&encoder);
        delete info;
        return false;
    }

    // The decoder consumes ADTS and the WFD path uses AAC-LC PCM16.
    // The FDK encoder requires AOT, channel mode and transport to be set,
    // then a NULL-buffer initialization call before accepting audio.
    err = aacEncoder_SetParam(encoder, AACENC_AOT, AOT_AAC_LC);
    if (err == AACENC_OK)
        err = aacEncoder_SetParam(encoder, AACENC_CHANNELMODE, p_aacConfig->n_channels);
    if (err == AACENC_OK)
        err = aacEncoder_SetParam(encoder, AACENC_SAMPLERATE, p_aacConfig->n_sampleRate);
    if (err == AACENC_OK)
        err = aacEncoder_SetParam(encoder, AACENC_BITRATE, p_aacConfig->n_bitrate);
    if (err == AACENC_OK)
        err = aacEncoder_SetParam(encoder, AACENC_TRANSMUX, TT_MP4_ADTS);
    if (err == AACENC_OK)
        err = aacEncEncode(encoder, NULL, NULL, NULL, NULL);
    if (err != AACENC_OK) {
        ALOGE("Failed to initialize WFD AAC encoder: %d", err);
        aacEncClose(&encoder);
        delete info;
        return false;
    }

    if (p_aacHandle) {
        HANDLE_AACENCODER previous = static_cast<HANDLE_AACENCODER>(p_aacHandle);
        aacEncClose(&previous);
    }
    delete static_cast<aacInfo*>(p_aacInfo);

    p_aacHandle = encoder;
    p_aacInfo = info;
    memcpy(&s_aacConfig, p_aacConfig, sizeof(s_aacConfig));
    return true;
}

bool aacEncode::aacEncodeFrame(unsigned char *p_inBuffer,
                               unsigned int n_inSize,
                               unsigned char *p_outBuffer,
                               unsigned int n_outSize,
                               unsigned int *p_length) {
    if (!p_length)
        return false;
    *p_length = 0;

    if (!p_aacHandle || !p_aacInfo || !p_inBuffer || !p_outBuffer ||
        n_inSize == 0 || n_outSize == 0 ||
        n_inSize > INT_MAX || n_outSize > INT_MAX ||
        n_inSize % sizeof(INT_PCM) != 0) {
        ALOGE("Invalid WFD AAC encoder state or input/output capacity");
        return false;
    }

    const unsigned int samples = n_inSize / sizeof(INT_PCM);
    if (samples == 0 || samples > INT_MAX ||
        samples % s_aacConfig.n_channels != 0) {
        ALOGE("Invalid WFD PCM sample count");
        return false;
    }

    aacInfo *state = static_cast<aacInfo*>(p_aacInfo);
    // FDK's descriptors require identifiers, byte sizes, element sizes and
    // counts; setting only .bufs used to pass uninitialized descriptor data.
    void *inputBuffers[] = {p_inBuffer};
    void *outputBuffers[] = {p_outBuffer};
    INT inputIds[] = {IN_AUDIO_DATA};
    INT outputIds[] = {OUT_BITSTREAM_DATA};
    INT inputSizes[] = {static_cast<INT>(n_inSize)};
    INT outputSizes[] = {static_cast<INT>(n_outSize)};
    INT inputElementSizes[] = {sizeof(INT_PCM)};
    INT outputElementSizes[] = {sizeof(UCHAR)};

    state->inBuff = {};
    state->inBuff.numBufs = 1;
    state->inBuff.bufs = inputBuffers;
    state->inBuff.bufferIdentifiers = inputIds;
    state->inBuff.bufSizes = inputSizes;
    state->inBuff.bufElSizes = inputElementSizes;

    state->outBuff = {};
    state->outBuff.numBufs = 1;
    state->outBuff.bufs = outputBuffers;
    state->outBuff.bufferIdentifiers = outputIds;
    state->outBuff.bufSizes = outputSizes;
    state->outBuff.bufElSizes = outputElementSizes;

    state->inArg = {};
    state->outArg = {};
    state->inArg.numInSamples = static_cast<INT>(samples);

    const AACENC_ERROR err = aacEncEncode(
            static_cast<HANDLE_AACENCODER>(p_aacHandle),
            &state->inBuff, &state->outBuff, &state->inArg, &state->outArg);
    if (err != AACENC_OK || state->outArg.numOutBytes < 0 ||
        static_cast<unsigned int>(state->outArg.numOutBytes) > n_outSize ||
        state->outArg.numInSamples != state->inArg.numInSamples) {
        // The legacy wrapper cannot report a partially consumed PCM buffer.
        // Fail closed instead of claiming a successful encode or overrun.
        ALOGE("WFD AAC encode failed or returned inconsistent buffer lengths");
        return false;
    }

    // Zero bytes is valid while the FDK encoder is still buffering audio.
    *p_length = static_cast<unsigned int>(state->outArg.numOutBytes);
    return true;
}
