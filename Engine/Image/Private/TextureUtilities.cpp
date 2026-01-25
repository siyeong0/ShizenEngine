/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "pch.h"
#include "TextureUtilities.h"

#include <algorithm>
#include <vector>
#include <limits>

#include "TextureLoader.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/GraphicsUtils/Public/ColorConversion.h"
#include "Engine/GraphicsUtils/Public/GraphicsUtils.hpp"

namespace shz
{

    template <typename SrcChannelType, typename DstChannelType>
    DstChannelType ConvertChannel(SrcChannelType Val)
    {
        return Val;
    }

    template <>
    uint16 ConvertChannel<uint8, uint16>(uint8 Val)
    {
        return static_cast<uint16>(Val) << 8u;
    }
    template <>
    uint32 ConvertChannel<uint8, uint32>(uint8 Val)
    {
        return static_cast<uint32>(Val) << 24u;
    }

    template <>
    uint8 ConvertChannel<uint16, uint8>(uint16 Val)
    {
        return static_cast<uint8>(Val >> 8u);
    }
    template <>
    uint32 ConvertChannel<uint16, uint32>(uint16 Val)
    {
        return static_cast<uint32>(Val) << 16u;
    }

    template <>
    uint8 ConvertChannel<uint32, uint8>(uint32 Val)
    {
        return static_cast<uint8>(Val >> 24u);
    }
    template <>
    uint16 ConvertChannel<uint32, uint16>(uint32 Val)
    {
        return static_cast<uint16>(Val >> 16u);
    }

    template <typename SrcChannelType, typename DstChannelType>
    void CopyPixelsImpl(const CopyPixelsAttribs& Attribs)
    {
        ASSERT_EXPR(sizeof(SrcChannelType) == Attribs.SrcComponentSize);
        ASSERT_EXPR(sizeof(DstChannelType) == Attribs.DstComponentSize);

        auto ProcessRows = [&Attribs](auto&& Handler) {
            for (size_t row = 0; row < size_t{ Attribs.Height }; ++row)
            {
                size_t src_row = Attribs.FlipVertically ? size_t{ Attribs.Height } - row - 1 : row;
                
                const SrcChannelType* pSrcRow = reinterpret_cast<const SrcChannelType*>((static_cast<const uint8*>(Attribs.pSrcPixels) + size_t{ Attribs.SrcStride } *src_row));
                DstChannelType* pDstRow = reinterpret_cast<DstChannelType*>((static_cast<uint8*>(Attribs.pDstPixels) + size_t{ Attribs.DstStride } *row));
                
                Handler(pSrcRow, pDstRow);
            }
            };

        const bool SwizzleRequired =
            (Attribs.DstCompCount >= 1 && Attribs.Swizzle.R != TEXTURE_COMPONENT_SWIZZLE_IDENTITY && Attribs.Swizzle.R != TEXTURE_COMPONENT_SWIZZLE_R) ||
            (Attribs.DstCompCount >= 2 && Attribs.Swizzle.G != TEXTURE_COMPONENT_SWIZZLE_IDENTITY && Attribs.Swizzle.G != TEXTURE_COMPONENT_SWIZZLE_G) ||
            (Attribs.DstCompCount >= 3 && Attribs.Swizzle.B != TEXTURE_COMPONENT_SWIZZLE_IDENTITY && Attribs.Swizzle.B != TEXTURE_COMPONENT_SWIZZLE_B) ||
            (Attribs.DstCompCount >= 4 && Attribs.Swizzle.A != TEXTURE_COMPONENT_SWIZZLE_IDENTITY && Attribs.Swizzle.A != TEXTURE_COMPONENT_SWIZZLE_A);

        const uint32 SrcRowSize = Attribs.Width * Attribs.SrcComponentSize * Attribs.SrcCompCount;
        const uint32 DstRowSize = Attribs.Width * Attribs.DstComponentSize * Attribs.DstCompCount;
        if (SrcRowSize == DstRowSize && !SwizzleRequired)
        {
            if (SrcRowSize == Attribs.SrcStride &&
                DstRowSize == Attribs.DstStride &&
                !Attribs.FlipVertically)
            {
                memcpy(Attribs.pDstPixels, Attribs.pSrcPixels, size_t{ SrcRowSize } *size_t{ Attribs.Height });
            }
            else
            {
                ProcessRows([SrcRowSize](auto* pSrcRow, auto* pDstRow) {
                    memcpy(pDstRow, pSrcRow, SrcRowSize);
                    });
            }
        }
        else if (Attribs.DstCompCount < Attribs.SrcCompCount && !SwizzleRequired)
        {
            ProcessRows([&Attribs](auto* pSrcRow, auto* pDstRow) {
                for (size_t col = 0; col < size_t{ Attribs.Width }; ++col)
                {
                    auto* pDst = pDstRow + col * Attribs.DstCompCount;
                    const auto* pSrc = pSrcRow + col * Attribs.SrcCompCount;
                    for (size_t c = 0; c < Attribs.DstCompCount; ++c)
                        pDst[c] = ConvertChannel<SrcChannelType, DstChannelType>(pSrc[c]);
                }
                });
        }
        else
        {
            static constexpr int SrcCompOffset_ZERO = -1;
            static constexpr int SrcCompOffset_ONE = -2;

            auto GetSrcCompOffset = [&Attribs](TEXTURE_COMPONENT_SWIZZLE Swizzle, int IdentityOffset) {
                int SrcCompOffset = SrcCompOffset_ZERO;
                switch (Swizzle)
                {
                    
                case TEXTURE_COMPONENT_SWIZZLE_IDENTITY: SrcCompOffset = IdentityOffset;     break;
                case TEXTURE_COMPONENT_SWIZZLE_ZERO:     SrcCompOffset = SrcCompOffset_ZERO; break;
                case TEXTURE_COMPONENT_SWIZZLE_ONE:      SrcCompOffset = SrcCompOffset_ONE;  break;
                case TEXTURE_COMPONENT_SWIZZLE_R:        SrcCompOffset = 0;                  break;
                case TEXTURE_COMPONENT_SWIZZLE_G:        SrcCompOffset = 1;                  break;
                case TEXTURE_COMPONENT_SWIZZLE_B:        SrcCompOffset = 2;                  break;
                case TEXTURE_COMPONENT_SWIZZLE_A:        SrcCompOffset = 3;                  break;
                    
                default:
                    ASSERT(false, "Unexpected swizzle value");
                }
                if (SrcCompOffset >= static_cast<int>(Attribs.SrcCompCount))
                    SrcCompOffset = SrcCompOffset_ZERO;
                return SrcCompOffset;
                };

            const int SrcCompOffsets[4] = {
                GetSrcCompOffset(Attribs.Swizzle.R, 0),
                GetSrcCompOffset(Attribs.Swizzle.G, 1),
                GetSrcCompOffset(Attribs.Swizzle.B, 2),
                GetSrcCompOffset(Attribs.Swizzle.A, 3) };

            ProcessRows([&Attribs, &SrcCompOffsets](auto* pSrcRow, auto* pDstRow) {
                for (size_t col = 0; col < size_t{ Attribs.Width }; ++col)
                {
                    auto* pDst = pDstRow + col * Attribs.DstCompCount;
                    const auto* pSrc = pSrcRow + col * Attribs.SrcCompCount;

                    for (size_t c = 0; c < Attribs.DstCompCount; ++c)
                    {
                        const int SrcCompOffset = SrcCompOffsets[c];

                        pDst[c] = (SrcCompOffset >= 0) ?
                            ConvertChannel<SrcChannelType, DstChannelType>(pSrc[SrcCompOffset]) :
                            (SrcCompOffset == SrcCompOffset_ZERO ? 0 : std::numeric_limits<DstChannelType>::max());
                    }
                }
                });
        }
    }

    void CopyPixels(const CopyPixelsAttribs& Attribs)
    {
        ASSERT(Attribs.Width > 0, "Width must not be zero");
        ASSERT(Attribs.Height > 0, "Height must not be zero");
        ASSERT(Attribs.SrcComponentSize > 0, "Source component size must not be zero");
        ASSERT(Attribs.pSrcPixels != nullptr, "Source pixels pointer must not be null");
        ASSERT(Attribs.SrcStride != 0 || Attribs.Height == 1, "Source stride must not be null");
        ASSERT(Attribs.SrcCompCount != 0, "Source component count must not be zero");
        ASSERT(Attribs.pDstPixels != nullptr, "Destination pixels pointer must not be null");
        ASSERT(Attribs.DstComponentSize > 0, "Destination component size must not be zero");
        ASSERT(Attribs.DstStride != 0 || Attribs.Height == 1, "Destination stride must not be null");
        ASSERT(Attribs.DstCompCount != 0, "Destination component count must not be zero");
        ASSERT(Attribs.SrcStride >= Attribs.Width * Attribs.SrcComponentSize * Attribs.SrcCompCount || Attribs.Height == 1, "Source stride is too small");
        ASSERT(Attribs.DstStride >= Attribs.Width * Attribs.DstComponentSize * Attribs.DstCompCount || Attribs.Height == 1, "Destination stride is too small");


        switch (Attribs.SrcComponentSize)
        {
#define CASE_SRC_COMPONENT_SIZE(SRC_TYPE)                                                               \
    case sizeof(SRC_TYPE):                                                                              \
        switch (Attribs.DstComponentSize)                                                               \
        {                                                                                               \
            case 1: CopyPixelsImpl<SRC_TYPE, uint8>(Attribs); break;                                    \
            case 2: CopyPixelsImpl<SRC_TYPE, uint16>(Attribs); break;                                   \
            case 4: CopyPixelsImpl<SRC_TYPE, uint32>(Attribs); break;                                   \
            default: ASSERT(false, "Unsupported destination component size: ", Attribs.DstComponentSize); \
        }                                                                                               \
        break

            CASE_SRC_COMPONENT_SIZE(uint8);
            CASE_SRC_COMPONENT_SIZE(uint16);
            CASE_SRC_COMPONENT_SIZE(uint32);
#undef CASE_SRC_COMPONENT_SIZE

        default:
            ASSERT(false, "Unsupported source component size: ", Attribs.SrcComponentSize);
        }
    }

    void ExpandPixels(const ExpandPixelsAttribs& Attribs)
    {
        ASSERT(Attribs.SrcWidth > 0, "Source width must not be zero");
        ASSERT(Attribs.SrcHeight > 0, "Source height must not be zero");
        ASSERT(Attribs.ComponentSize > 0, "Component size must not be zero");
        ASSERT(Attribs.ComponentCount != 0, "Component count must not be zero");
        ASSERT(Attribs.pSrcPixels != nullptr, "Source pixels pointer must not be null");
        ASSERT(Attribs.SrcStride != 0 || Attribs.SrcHeight == 1, "Source stride must not be null");

        ASSERT(Attribs.DstWidth > 0, "Destination width must not be zero");
        ASSERT(Attribs.DstHeight > 0, "Destination height must not be zero");
        ASSERT(Attribs.pDstPixels != nullptr, "Destination pixels pointer must not be null");
        ASSERT(Attribs.DstStride != 0 || Attribs.DstHeight == 1, "Destination stride must not be null");
        ASSERT(Attribs.SrcStride >= Attribs.SrcWidth * Attribs.ComponentSize * Attribs.ComponentCount || Attribs.SrcHeight == 1, "Source stride is too small");
        ASSERT(Attribs.DstStride >= Attribs.DstWidth * Attribs.ComponentSize * Attribs.ComponentCount || Attribs.DstHeight == 1, "Destination stride is too small");

        const uint32 NumRowsToCopy = std::min(Attribs.SrcHeight, Attribs.DstHeight);
        const uint32 NumColsToCopy = std::min(Attribs.SrcWidth, Attribs.DstWidth);

        auto ExpandRow = [&Attribs, NumColsToCopy](size_t row, uint8* pDstRow) {
            const uint8* pSrcRow = reinterpret_cast<const uint8*>(Attribs.pSrcPixels) + row * size_t{ Attribs.SrcStride };
            memcpy(pDstRow, pSrcRow, size_t{ NumColsToCopy } *size_t{ Attribs.ComponentSize } *size_t{ Attribs.ComponentCount });

            // Expand the row by repeating the last pixel
            const uint8* pLastPixel = pSrcRow + size_t{ NumColsToCopy - 1u } *size_t{ Attribs.ComponentSize } *size_t{ Attribs.ComponentCount };
            for (size_t col = NumColsToCopy; col < Attribs.DstWidth; ++col)
            {
                memcpy(pDstRow + col * Attribs.ComponentSize * Attribs.ComponentCount, pLastPixel, size_t{ Attribs.ComponentSize } *size_t{ Attribs.ComponentCount });
            }
            };

        for (size_t row = 0; row < NumRowsToCopy; ++row)
        {
            uint8* pDstRow = reinterpret_cast<uint8*>(Attribs.pDstPixels) + row * Attribs.DstStride;
            ExpandRow(row, pDstRow);
        }

        if (NumRowsToCopy < Attribs.DstHeight)
        {
            std::vector<uint8> LastRow(size_t{ Attribs.DstWidth } *size_t{ Attribs.ComponentSize } *size_t{ Attribs.ComponentCount });
            ExpandRow(NumRowsToCopy - 1, LastRow.data());
            for (size_t row = NumRowsToCopy - 1; row < Attribs.DstHeight; ++row)
            {
                uint8* pDstRow = reinterpret_cast<uint8*>(Attribs.pDstPixels) + row * Attribs.DstStride;
                memcpy(pDstRow, LastRow.data(), LastRow.size());
            }
        }
    }

    template <typename Type>
    struct PremultiplyAlphaImplHelper;

    template <>
    struct PremultiplyAlphaImplHelper<uint8>
    {
        using IntermediateType = uint32;
    };

    template <>
    struct PremultiplyAlphaImplHelper<int8>
    {
        using IntermediateType = int32;
    };

    template <>
    struct PremultiplyAlphaImplHelper<uint16>
    {
        using IntermediateType = uint32;
    };

    template <>
    struct PremultiplyAlphaImplHelper<int16>
    {
        using IntermediateType = int32;
    };

    template <>
    struct PremultiplyAlphaImplHelper<uint32>
    {
        using IntermediateType = uint64;
    };

    template <>
    struct PremultiplyAlphaImplHelper<int32>
    {
        using IntermediateType = int64;
    };

    template <typename Type, typename PremultiplyComponentType>
    void PremultiplyComponents(const PremultiplyAlphaAttribs& Attribs, PremultiplyComponentType&& PremultiplyComponent)
    {
        for (uint32 row = 0; row < Attribs.Height; ++row)
        {
            Type* pRow = reinterpret_cast<Type*>(reinterpret_cast<uint8*>(Attribs.pPixels) + row * Attribs.Stride);
            for (uint32 col = 0; col < Attribs.Width; ++col)
            {
                Type* pPixel = pRow + col * Attribs.ComponentCount;
                Type  A = pPixel[Attribs.ComponentCount - 1];
                for (uint32 c = 0; c < Attribs.ComponentCount - 1; ++c)
                    PremultiplyComponent(pPixel[c], A);
            }
        }
    }

    template <typename Type>
    void PremultiplyAlphaImpl(const PremultiplyAlphaAttribs& Attribs)
    {
        if (Attribs.IsSRGB)
        {
            PremultiplyComponents<Type>(
                Attribs,
                [](auto& C, auto A) {
                    constexpr float MaxValue = static_cast<float>(std::numeric_limits<Type>::max());

                    float Linear = FastGammaToLinear(static_cast<float>(C) / MaxValue);
                    Linear *= static_cast<float>(A) / MaxValue;
                    float Gamma = FastLinearToGamma(Linear);

                    C = static_cast<Type>(Gamma * MaxValue + 0.5f);
                });
        }
        else
        {
            PremultiplyComponents<Type>(
                Attribs,
                [](auto& C, auto A) {
                    using IntermediateType = typename PremultiplyAlphaImplHelper<Type>::IntermediateType;

                    constexpr IntermediateType MaxValue = static_cast<IntermediateType>(std::numeric_limits<Type>::max());

                    C = static_cast<Type>((static_cast<IntermediateType>(C) * A + MaxValue / 2) / MaxValue);
                });
        }
    }

    template <>
    void PremultiplyAlphaImpl<float>(const PremultiplyAlphaAttribs& Attribs)
    {
        using Type = float;
        if (Attribs.IsSRGB)
        {
            PremultiplyComponents<Type>(
                Attribs,
                [](auto& C, auto A) {
                    float Linear = FastGammaToLinear(C);
                    Linear *= A;
                    C = FastLinearToGamma(Linear);
                });
        }
        else
        {
            PremultiplyComponents<Type>(
                Attribs,
                [](auto& C, auto A) {
                    C *= A;
                });
        }
    }

    void PremultiplyAlpha(const PremultiplyAlphaAttribs& Attribs)
    {
        const uint32 ValueSize = GetValueSize(Attribs.ComponentType);

        ASSERT(Attribs.Width > 0, "Eidth must not be zero");
        ASSERT(Attribs.Height > 0, "Height must not be zero");
        ASSERT(Attribs.ComponentCount >= 2, "The number of components must be at least two");
        ASSERT(Attribs.pPixels != nullptr, "Pixels pointer must not be null");
        ASSERT(Attribs.Stride != 0 || Attribs.Height == 1, "Source stride must not be null");
        ASSERT(Attribs.Stride >= Attribs.Width * ValueSize * Attribs.ComponentCount || Attribs.Height == 1, "Source stride is too small");

        switch (Attribs.ComponentType)
        {
        case VT_UINT8: PremultiplyAlphaImpl<uint8>(Attribs); break;
        case VT_UINT16: PremultiplyAlphaImpl<uint16>(Attribs); break;
        case VT_UINT32: PremultiplyAlphaImpl<uint32>(Attribs); break;

        case VT_INT8: PremultiplyAlphaImpl<int8>(Attribs); break;
        case VT_INT16: PremultiplyAlphaImpl<int16>(Attribs); break;
        case VT_INT32: PremultiplyAlphaImpl<int32>(Attribs); break;

        case VT_FLOAT32: PremultiplyAlphaImpl<float>(Attribs); break;

        default:
            ASSERT(false, "Unsupported component type ", GetValueTypeString(Attribs.ComponentType));
        }
    }

    void CreateTextureFromFile(
        const Char* FilePath,
        const TextureLoadInfo& TexLoadInfo,
        IRenderDevice* pDevice,
        ITexture** ppTexture)
    {
        RefCntAutoPtr<ITextureLoader> pTexLoader;
        CreateTextureLoaderFromFile(FilePath, IMAGE_FILE_FORMAT_UNKNOWN, TexLoadInfo, &pTexLoader);
        if (!pTexLoader)
            return;

        pTexLoader->CreateTexture(pDevice, ppTexture);
    }

} // namespace shz
