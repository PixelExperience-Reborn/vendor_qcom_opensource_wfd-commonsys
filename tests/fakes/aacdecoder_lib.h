// Host TEST DOUBLE only; API names match the public Fraunhofer AAC decoder.
#pragma once
#include <stdint.h>
typedef int INT;
typedef unsigned int UINT;
typedef int16_t INT_PCM;
typedef unsigned char UCHAR;
struct AAC_DECODER;
typedef AAC_DECODER* HANDLE_AACDECODER;
enum AAC_DECODER_ERROR { AAC_DEC_OK = 0, AAC_DEC_UNKNOWN = 1,
                         AAC_DEC_OUTPUT_BUFFER_TOO_SMALL = 0x200c };
enum AACDEC_PARAM { AAC_DRC_REFERENCE_LEVEL, AAC_DRC_ATTENUATION_FACTOR,
                    AAC_PCM_MAX_OUTPUT_CHANNELS };
enum { TT_MP4_ADTS = 2 };
struct CStreamInfo {
    INT sampleRate;
    INT frameSize;
    INT numChannels;
};
HANDLE_AACDECODER aacDecoder_Open(INT transportFmt, UINT layers);
void aacDecoder_Close(HANDLE_AACDECODER handle);
CStreamInfo* aacDecoder_GetStreamInfo(HANDLE_AACDECODER handle);
AAC_DECODER_ERROR aacDecoder_SetParam(HANDLE_AACDECODER handle, AACDEC_PARAM, INT value);
AAC_DECODER_ERROR aacDecoder_Fill(HANDLE_AACDECODER handle, UCHAR** buffers,
                                   const UINT* size, UINT* bytesValid);
AAC_DECODER_ERROR aacDecoder_DecodeFrame(HANDLE_AACDECODER handle,
                                           INT_PCM* out, const INT size,
                                           const UINT flags);
