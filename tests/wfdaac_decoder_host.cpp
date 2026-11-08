// Exercises the REAL aacDecode.cpp against a host-only FDK decoder test double.
#include "aacDecode.h"
#include "aacdecoder_lib.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

struct AAC_DECODER {
    CStreamInfo info = {48000, 4, 2};
    unsigned int params = 0;
};
static int live_decoders = 0;
static bool fail_set_parameter = false;
static bool stream_reports_too_many_samples = false;
static bool fail_decode = false;

HANDLE_AACDECODER aacDecoder_Open(INT transportFmt, UINT layers) {
    assert(transportFmt == TT_MP4_ADTS && layers == 1);
    ++live_decoders;
    return new AAC_DECODER{};
}
void aacDecoder_Close(HANDLE_AACDECODER handle) {
    if (handle) {
        delete handle;
        --live_decoders;
    }
}
CStreamInfo* aacDecoder_GetStreamInfo(HANDLE_AACDECODER handle) {
    assert(handle);
    return &handle->info;
}
AAC_DECODER_ERROR aacDecoder_SetParam(HANDLE_AACDECODER handle, AACDEC_PARAM p, INT val) {
    assert(handle);
    if (fail_set_parameter) return AAC_DEC_UNKNOWN;
    if (p == AAC_DRC_REFERENCE_LEVEL) assert(val == 64);
    if (p == AAC_DRC_ATTENUATION_FACTOR) assert(val == 127);
    if (p == AAC_PCM_MAX_OUTPUT_CHANNELS) assert(val == 2);
    handle->params |= (1U << static_cast<unsigned int>(p));
    return AAC_DEC_OK;
}
AAC_DECODER_ERROR aacDecoder_Fill(HANDLE_AACDECODER handle, UCHAR** data,
                                  const UINT* sizes, UINT* bytesValid) {
    assert(handle->params == 0x7);
    assert(data && data[0] && sizes[0] >= 7);
    assert(*bytesValid == sizes[0]);
    *bytesValid = 0;
    return AAC_DEC_OK;
}
AAC_DECODER_ERROR aacDecoder_DecodeFrame(HANDLE_AACDECODER handle,
                                          INT_PCM* pcm, INT capacity, UINT flags) {
    assert(handle && pcm && flags == 0);
    if (fail_decode || capacity < 8)
        return AAC_DEC_OUTPUT_BUFFER_TOO_SMALL;
    for (int i = 0; i < 8; ++i)
        pcm[i] = static_cast<INT_PCM>(i + 1);
    handle->info.frameSize = stream_reports_too_many_samples ? 24 : 4;
    return AAC_DEC_OK;
}

int main() {
    aacConfigType cfg = {48000, 2, 16, 128000, 1024};
    unsigned char data[32];
    std::memset(data, 0x5A, sizeof(data));
    {
        aacDecode decoder;
        assert(!decoder.aacDecodeFrame(data, 16));
        assert(!decoder.aacConfigure(nullptr));
        assert(decoder.aacConfigure(&cfg));
        assert(live_decoders == 1);
        assert(decoder.aacDecodeFrame(data, 16));
        INT_PCM output[8] = {};
        std::memcpy(output, data, sizeof(output));
        for (int i = 0; i < 8; ++i)
            assert(output[i] == i + 1);
        for (unsigned int i = 16; i < sizeof(data); ++i)
            assert(data[i] == 0x5A);

        unsigned char failed[16];
        std::memset(failed, 0x5A, sizeof(failed));
        assert(!decoder.aacDecodeFrame(failed, 7));
        assert(failed[0] == 0x5A && failed[15] == 0x5A);
        stream_reports_too_many_samples = true;
        assert(!decoder.aacDecodeFrame(failed, 16));
        for (unsigned char c : failed) assert(c == 0x5A);
        stream_reports_too_many_samples = false;
        fail_decode = true;
        assert(!decoder.aacDecodeFrame(failed, 16));
        fail_decode = false;

        fail_set_parameter = true;
        assert(!decoder.aacConfigure(&cfg));
        assert(live_decoders == 1); // previous decoder survived.
        fail_set_parameter = false;
        assert(decoder.aacConfigure(&cfg));
        assert(live_decoders == 1);
    }
    assert(live_decoders == 0);
    std::cout << "PASS: WFD AAC decoder buffer bounds, clean reconfigure and no overrun\n";
    return 0;
}
