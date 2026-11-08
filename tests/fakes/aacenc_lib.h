// Minimal host TEST DOUBLE for documented Fraunhofer AAC encoder API.
// Not an implementation and must never be included in Android product builds.
#pragma once
#include <stdint.h>
typedef int INT;
typedef unsigned int UINT;
typedef int16_t INT_PCM;
typedef unsigned char UCHAR;
struct AACENCODER;
typedef AACENCODER* HANDLE_AACENCODER;
enum AACENC_ERROR { AACENC_OK = 0, AACENC_INVALID_CONFIG = 0x23,
                    AACENC_ENCODE_ERROR = 0x60 };
enum AACENC_PARAM { AACENC_AOT, AACENC_CHANNELMODE, AACENC_SAMPLERATE,
                    AACENC_BITRATE, AACENC_TRANSMUX };
enum { AOT_AAC_LC = 2, TT_MP4_ADTS = 2, IN_AUDIO_DATA = 0,
       OUT_BITSTREAM_DATA = 3 };
struct AACENC_BufDesc {
    INT numBufs;
    void** bufs;
    INT* bufferIdentifiers;
    INT* bufSizes;
    INT* bufElSizes;
};
struct AACENC_InArgs { INT numInSamples; INT numAncBytes; };
struct AACENC_OutArgs { INT numOutBytes; INT numInSamples;
                         INT numAncBytes; INT bitResState; };
AACENC_ERROR aacEncOpen(HANDLE_AACENCODER* handle, UINT modules, UINT channels);
AACENC_ERROR aacEncClose(HANDLE_AACENCODER* handle);
AACENC_ERROR aacEncoder_SetParam(HANDLE_AACENCODER handle, AACENC_PARAM param, UINT value);
AACENC_ERROR aacEncEncode(HANDLE_AACENCODER handle, const AACENC_BufDesc* in,
                          const AACENC_BufDesc* out, const AACENC_InArgs* args,
                          AACENC_OutArgs* result);
