#pragma once
#include "Image.h"

namespace shz
{
    enum DECODE_EXR_RESULT : uint32
    {
        DECODE_EXR_RESULT_OK = 0,
        DECODE_EXR_RESULT_INVALID_ARGUMENTS,
        DECODE_EXR_RESULT_INVALID_SIGNATURE,
        DECODE_EXR_RESULT_INITIALIZATION_FAILED,
        DECODE_EXR_RESULT_UNSUPPORTED_FORMAT,
        DECODE_EXR_RESULT_DECODING_ERROR
    };

    enum ENCODE_EXR_RESULT : uint32
    {
        ENCODE_EXR_RESULT_OK = 0,
        ENCODE_EXR_RESULT_INVALID_ARGUMENTS,
        ENCODE_EXR_RESULT_INITIALIZATION_FAILED,
        ENCODE_EXR_RESULT_UNSUPPORTED_FORMAT,
        ENCODE_EXR_RESULT_ENCODING_ERROR
    };

    DECODE_EXR_RESULT DecodeEXR(
        const void* pSrcExrBits,
        size_t ExrDataSize,
        IDataBlob* pDstPixels,
        ImageDesc* pDstImgDesc);

    ENCODE_EXR_RESULT EncodeEXR(
        const void* pSrcPixels,
        const ImageDesc& SrcDesc,
        IDataBlob* pDstExrBits);
} // namespace shz
