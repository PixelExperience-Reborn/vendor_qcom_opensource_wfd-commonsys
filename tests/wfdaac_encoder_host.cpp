// Runs the REAL aacEncode.cpp against a host-only Fraunhofer API test double.
// The output bytes in this file are markers, not a valid AAC bitstream.
#include "aacEncode.h"
#include "aacenc_lib.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

struct AACENCODER {
    UINT modules;
    UINT channels;
    bool initialized = false;
    unsigned int parameter_mask = 0;
};

static int live_encoders = 0;
static int total_encode_calls = 0;
static bool fail_configuration = false;
static bool fail_encoding = false;
static bool claim_oversize = false;
static bool partially_consume = false;
static bool buffering = false;

AACENC_ERROR aacEncOpen(HANDLE_AACENCODER* handle, UINT modules, UINT channels) {
    assert(handle != nullptr);
    assert(modules == 0x01);
    *handle = new AACENCODER{modules, channels};
    ++live_encoders;
    return AACENC_OK;
}

AACENC_ERROR aacEncClose(HANDLE_AACENCODER* handle) {
    assert(handle != nullptr);
    if (*handle) {
        delete *handle;
        *handle = nullptr;
        --live_encoders;
    }
    return AACENC_OK;
}

AACENC_ERROR aacEncoder_SetParam(HANDLE_AACENCODER handle, AACENC_PARAM param,
                                  UINT value) {
    assert(handle);
    if (fail_configuration)
        return AACENC_INVALID_CONFIG;
    switch (param) {
    case AACENC_AOT: assert(value == AOT_AAC_LC); break;
    case AACENC_CHANNELMODE: assert(value == handle->channels); break;
    case AACENC_SAMPLERATE: assert(value == 48000); break;
    case AACENC_BITRATE: assert(value == 128000); break;
    case AACENC_TRANSMUX: assert(value == TT_MP4_ADTS); break;
    default: assert(false);
    }
    handle->parameter_mask |= (1U << static_cast<unsigned int>(param));
    return AACENC_OK;
}

AACENC_ERROR aacEncEncode(HANDLE_AACENCODER handle, const AACENC_BufDesc* in,
                           const AACENC_BufDesc* out, const AACENC_InArgs* args,
                           AACENC_OutArgs* result) {
    assert(handle != nullptr);
    if (!in && !out && !args && !result) {
        assert(handle->parameter_mask == 0x1f);
        handle->initialized = true;
        return AACENC_OK;
    }
    ++total_encode_calls;
    assert(handle->initialized);
    assert(in && out && args && result);
    assert(in->numBufs == 1 && out->numBufs == 1);
    assert(in->bufs && out->bufs && in->bufs[0] && out->bufs[0]);
    assert(in->bufferIdentifiers && out->bufferIdentifiers);
    assert(in->bufSizes && out->bufSizes && in->bufElSizes && out->bufElSizes);
    assert(in->bufferIdentifiers[0] == IN_AUDIO_DATA);
    assert(out->bufferIdentifiers[0] == OUT_BITSTREAM_DATA);
    assert(in->bufElSizes[0] == static_cast<INT>(sizeof(INT_PCM)));
    assert(out->bufElSizes[0] == static_cast<INT>(sizeof(UCHAR)));
    assert(in->bufSizes[0] == 8 && out->bufSizes[0] >= 4);
    assert(args->numInSamples == 4 && args->numAncBytes == 0);
    if (fail_encoding)
        return AACENC_ENCODE_ERROR;
    result->numOutBytes = buffering ? 0 : 4;
    result->numInSamples = args->numInSamples;
    if (claim_oversize)
        result->numOutBytes = out->bufSizes[0] + 1;
    if (partially_consume)
        --result->numInSamples;
    if (!buffering && !claim_oversize) {
        const unsigned char marker[] = {0xAA, 0xBB, 0xCC, 0xDD};
        std::memcpy(out->bufs[0], marker, sizeof(marker));
    }
    return AACENC_OK;
}

int main() {
    const aacConfigType valid = {48000, 2, 16, 128000, 1024};
    int16_t input[4] = {4, 5, 6, 7};
    unsigned char output[32] = {};
    unsigned int written = 99;
    int baseline = total_encode_calls;
    {
        aacEncode encoder;
        assert(!encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                       sizeof(input), output, sizeof(output), &written));
        assert(written == 0 && total_encode_calls == baseline);
        assert(!encoder.aacConfigure(nullptr));
        aacConfigType bad = valid;
        bad.n_channels = 0;
        assert(!encoder.aacConfigure(&bad));
        bad = valid;
        bad.n_bitsPerSample = 24;
        assert(!encoder.aacConfigure(&bad));
        assert(live_encoders == 0);

        assert(encoder.aacConfigure(const_cast<aacConfigType*>(&valid)));
        assert(live_encoders == 1);
        assert(encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                      sizeof(input), output, sizeof(output), &written));
        assert(written == 4 && output[0] == 0xAA && output[3] == 0xDD);

        // The wrapper must not report success for malformed input/output.
        const int calls = total_encode_calls;
        assert(!encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                        7, output, sizeof(output), &written));
        assert(written == 0 && total_encode_calls == calls);
        assert(!encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                        0, output, sizeof(output), &written));
        assert(!encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                        sizeof(input), output, 0, &written));
        assert(!encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                        sizeof(input), output, sizeof(output), nullptr));

        // A failed reconfiguration must not invalidate a working instance.
        fail_configuration = true;
        assert(!encoder.aacConfigure(const_cast<aacConfigType*>(&valid)));
        fail_configuration = false;
        assert(live_encoders == 1);
        assert(encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                      sizeof(input), output, sizeof(output), &written));
        assert(written == 4);

        fail_encoding = true;
        assert(!encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                        sizeof(input), output, sizeof(output), &written));
        assert(written == 0);
        fail_encoding = false;
        claim_oversize = true;
        assert(!encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                        sizeof(input), output, sizeof(output), &written));
        assert(written == 0);
        claim_oversize = false;
        partially_consume = true;
        assert(!encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                        sizeof(input), output, sizeof(output), &written));
        assert(written == 0);
        partially_consume = false;

        buffering = true;
        assert(encoder.aacEncodeFrame(reinterpret_cast<unsigned char*>(input),
                                      sizeof(input), output, sizeof(output), &written));
        assert(written == 0); // FDK might need further PCM before output.
        buffering = false;
        assert(encoder.aacConfigure(const_cast<aacConfigType*>(&valid)));
        assert(live_encoders == 1);
    }
    assert(live_encoders == 0); // ASan/LSan also verify all aacInfo allocations.
    std::cout << "PASS: WFD AAC configure, descriptor lengths, failures and lifecycle\n";
    return 0;
}
