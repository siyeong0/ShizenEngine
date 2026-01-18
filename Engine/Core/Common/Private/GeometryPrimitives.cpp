/*
 *  Copyright 2024-2025 Diligent Graphics LLC
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
#include "Engine/Core/Common/Public/GeometryPrimitives.h"

#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Memory/Public/DataBlobImpl.hpp"

namespace shz
{

uint32 GetGeometryPrimitiveVertexSize(GEOMETRY_PRIMITIVE_VERTEX_FLAGS VertexFlags)
{
    return (((VertexFlags & GEOMETRY_PRIMITIVE_VERTEX_FLAG_POSITION) ? sizeof(float3) : 0) +
            ((VertexFlags & GEOMETRY_PRIMITIVE_VERTEX_FLAG_NORMAL) ? sizeof(float3) : 0) +
            ((VertexFlags & GEOMETRY_PRIMITIVE_VERTEX_FLAG_TEXCOORD) ? sizeof(float2) : 0));
}

template <typename VertexHandlerType>
void CreateCubeGeometryInternal(uint32                          NumSubdivisions,
                                GEOMETRY_PRIMITIVE_VERTEX_FLAGS VertexFlags,
                                IDataBlob**                     ppVertices,
                                IDataBlob**                     ppIndices,
                                GeometryPrimitiveInfo*          pInfo,
                                VertexHandlerType&&             HandleVertex)
{
    if (NumSubdivisions == 0)
    {
        ASSERT(false, "NumSubdivisions must be positive");
        return;
    }
    if (NumSubdivisions > 2048)
    {
        ASSERT(false, "NumSubdivisions is too large");
        return;
    }

    //   ______ ______
    //  |    .'|    .'|
    //  |  .'  |  .'  |
    //  |.'____|.'____|  NumSubdivisions = 2
    //  |    .'|    .'|
    //  |  .'  |  .'  |
    //  |.'____|.'____|
    //
    const uint32 NumFaceVertices  = (NumSubdivisions + 1) * (NumSubdivisions + 1);
    const uint32 NumFaceTriangles = NumSubdivisions * NumSubdivisions * 2;
    const uint32 NumFaceIndices   = NumFaceTriangles * 3;
    const uint32 VertexSize       = GetGeometryPrimitiveVertexSize(VertexFlags);
    const uint32 NumFaces         = 6;
    const uint32 VertexDataSize   = NumFaceVertices * NumFaces * VertexSize;
    const uint32 IndexDataSize    = NumFaceIndices * NumFaces * sizeof(uint32);

    if (pInfo != nullptr)
    {
        pInfo->NumVertices = NumFaceVertices * NumFaces;
        pInfo->NumIndices  = NumFaceIndices * NumFaces;
        pInfo->VertexSize  = VertexSize;
    }

    RefCntAutoPtr<DataBlobImpl> pVertexData;
    uint8*                      pVert = nullptr;
    if (ppVertices != nullptr && VertexFlags != GEOMETRY_PRIMITIVE_VERTEX_FLAG_NONE)
    {
        pVertexData = DataBlobImpl::Create(VertexDataSize);
        ASSERT(*ppVertices == nullptr, "*ppVertices is not null, which may cause memory leak");
        pVertexData->QueryInterface(IID_DataBlob, ppVertices);
        pVert = pVertexData->GetDataPtr<uint8>();
    }

    RefCntAutoPtr<DataBlobImpl> pIndexData;
    uint32*                     pIdx = nullptr;
    if (ppIndices != nullptr)
    {
        pIndexData = DataBlobImpl::Create(IndexDataSize);
        ASSERT(*ppIndices == nullptr, "*ppIndices is not null, which may cause memory leak");
        pIndexData->QueryInterface(IID_DataBlob, ppIndices);
        pIdx = pIndexData->GetDataPtr<uint32>();
    }

    static constexpr std::array<float3, NumFaces> FaceNormals{
        float3{+1, 0, 0},
        float3{-1, 0, 0},
        float3{0, +1, 0},
        float3{0, -1, 0},
        float3{0, 0, +1},
        float3{0, 0, -1},
    };

    for (uint32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
    {
        if (pVert != nullptr)
        {
            // 6 ______7______ 8
            //  |    .'|    .'|
            //  |  .'  |  .'  |
            //  |.'____|.'____|
            // 3|    .'|4   .'|5
            //  |  .'  |  .'  |
            //  |.'____|.'____|
            // 0       1      2

            for (uint32 y = 0; y <= NumSubdivisions; ++y)
            {
                for (uint32 x = 0; x <= NumSubdivisions; ++x)
                {
                    float2 UV{
                        static_cast<float>(x) / NumSubdivisions,
                        static_cast<float>(y) / NumSubdivisions,
                    };

                    float2 XY{
                        UV.x - 0.5f,
                        0.5f - UV.y,
                    };

                    float3 Pos;
                    switch (FaceIndex)
                    {
                        case 0: Pos = float3{+0.5f, XY.y, +XY.x}; break;
                        case 1: Pos = float3{-0.5f, XY.y, -XY.x}; break;
                        case 2: Pos = float3{XY.x, +0.5f, +XY.y}; break;
                        case 3: Pos = float3{XY.x, -0.5f, -XY.y}; break;
                        case 4: Pos = float3{-XY.x, XY.y, +0.5f}; break;
                        case 5: Pos = float3{+XY.x, XY.y, -0.5f}; break;
                    }

                    float3 Normal = FaceNormals[FaceIndex];
                    HandleVertex(Pos, Normal, UV);

                    if (VertexFlags & GEOMETRY_PRIMITIVE_VERTEX_FLAG_POSITION)
                    {
                        memcpy(pVert, &Pos, sizeof(Pos));
                        pVert += sizeof(Pos);
                    }

                    if (VertexFlags & GEOMETRY_PRIMITIVE_VERTEX_FLAG_NORMAL)
                    {
                        memcpy(pVert, &Normal, sizeof(Normal));
                        pVert += sizeof(Normal);
                    }

                    if (VertexFlags & GEOMETRY_PRIMITIVE_VERTEX_FLAG_TEXCOORD)
                    {
                        memcpy(pVert, &UV, sizeof(UV));
                        pVert += sizeof(UV);
                    }
                }
            }
        }

        if (pIdx != nullptr)
        {
            uint32 FaceBaseVertex = FaceIndex * NumFaceVertices;
            for (uint32 y = 0; y < NumSubdivisions; ++y)
            {
                for (uint32 x = 0; x < NumSubdivisions; ++x)
                {
                    //  01     11
                    //   *-----*
                    //   |   .'|
                    //   | .'  |
                    //   *'----*
                    //  00     10
                    uint32 v00 = FaceBaseVertex + y * (NumSubdivisions + 1) + x;
                    uint32 v10 = v00 + 1;
                    uint32 v01 = v00 + NumSubdivisions + 1;
                    uint32 v11 = v01 + 1;

                    *pIdx++ = v00;
                    *pIdx++ = v10;
                    *pIdx++ = v11;

                    *pIdx++ = v00;
                    *pIdx++ = v11;
                    *pIdx++ = v01;
                }
            }
        }
    }

    ASSERT_EXPR(pVert == nullptr || pVert == pVertexData->GetConstDataPtr<uint8>() + VertexDataSize);
    ASSERT_EXPR(pIdx == nullptr || pIdx == pIndexData->GetConstDataPtr<uint32>() + IndexDataSize / sizeof(uint32));
}

void CreateCubeGeometry(const CubeGeometryPrimitiveAttributes& Attribs,
                        IDataBlob**                            ppVertices,
                        IDataBlob**                            ppIndices,
                        GeometryPrimitiveInfo*                 pInfo)
{
    const float Size = Attribs.Size;
    if (Size <= 0)
    {
        ASSERT(false, "Size must be positive");
        return;
    }

    CreateCubeGeometryInternal(Attribs.NumSubdivisions,
                               Attribs.VertexFlags,
                               ppVertices,
                               ppIndices,
                               pInfo,
                               [&](float3& Pos, float3& Normal, float2& UV) {
                                   Pos *= Size;
                               });
}

void CreateSphereGeometry(const SphereGeometryPrimitiveAttributes& Attribs,
                          IDataBlob**                              ppVertices,
                          IDataBlob**                              ppIndices,
                          GeometryPrimitiveInfo*                   pInfo)
{
    const float Radius = Attribs.Radius;
    if (Radius <= 0)
    {
        ASSERT(false, "Radius must be positive");
        return;
    }

    CreateCubeGeometryInternal(Attribs.NumSubdivisions,
                               Attribs.VertexFlags,
                               ppVertices,
                               ppIndices,
                               pInfo,
                               [&](float3& Pos, float3& Normal, float2& UV) {
                                   Normal = float3::Normalize(Pos);
                                   Pos    = Normal * Radius;

                                   UV.x = 0.5f + atan2(Normal.z, Normal.x) / (2 * PI);
                                   UV.y = 0.5f - asin(Normal.y) / PI;
                               });
}

void CreateGeometryPrimitive(const GeometryPrimitiveAttributes& Attribs,
                             IDataBlob**                        ppVertices,
                             IDataBlob**                        ppIndices,
                             GeometryPrimitiveInfo*             pInfo)
{
    ASSERT(ppVertices == nullptr || *ppVertices == nullptr, "*ppVertices is not null which may cause memory leaks");
    ASSERT(ppIndices == nullptr || *ppIndices == nullptr, "*ppIndices is not null which may cause memory leaks");

    static_assert(GEOMETRY_PRIMITIVE_TYPE_COUNT == 3, "Please update the switch below to handle the new geometry primitive type");
    switch (Attribs.Type)
    {
        case GEOMETRY_PRIMITIVE_TYPE_UNDEFINED:
            ASSERT(false, "Undefined geometry primitive type");
            break;

        case GEOMETRY_PRIMITIVE_TYPE_CUBE:
            CreateCubeGeometry(static_cast<const CubeGeometryPrimitiveAttributes&>(Attribs), ppVertices, ppIndices, pInfo);
            break;

        case GEOMETRY_PRIMITIVE_TYPE_SPHERE:
            CreateSphereGeometry(static_cast<const SphereGeometryPrimitiveAttributes&>(Attribs), ppVertices, ppIndices, pInfo);
            break;

        default:
            ASSERT(false, "Unknown geometry primitive type");
    }
}

} // namespace shz

extern "C"
{
    shz::uint32 Shizen_GetGeometryPrimitiveVertexSize(shz::GEOMETRY_PRIMITIVE_VERTEX_FLAGS VertexFlags)
    {
        return shz::GetGeometryPrimitiveVertexSize(VertexFlags);
    }

    void Shizen_CreateGeometryPrimitive(const shz::GeometryPrimitiveAttributes& Attribs,
                                          shz::IDataBlob**                        ppVertices,
                                          shz::IDataBlob**                        ppIndices,
                                          shz::GeometryPrimitiveInfo*             pInfo)
    {
        shz::CreateGeometryPrimitive(Attribs, ppVertices, ppIndices, pInfo);
    }
}
