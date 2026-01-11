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


#pragma once

// \file
// Declaration of DynamicAtlasManager class

#include <map>
#include <unordered_map>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/HashUtils.hpp"

namespace shz
{

// Dynamic 2D atlas manager

// This class manages a 2D atlas of regions. It allows allocating and freeing
// rectangular regions of the atlas. The regions are represented by the
// Region structure, which contains the x and y coordinates of the top-left
// corner, as well as the width and height of the region.
//
// \warning The class is not thread-safe. All operations on the atlas must be
//          must be protected by a mutex or other synchronization mechanism.
class DynamicAtlasManager
{
public:
    // Structure representing a rectangular region in the atlas.
    struct Region
    {
        // x coordinate of the top-left corner of the region
        uint32 x = 0;

        // y coordinate of the top-left corner of the region
        uint32 y = 0;

        // width of the region
        uint32 width = 0;

        // height of the region
        uint32 height = 0;

        Region() = default;

        
        Region           (const Region&)  = default;
        Region           (      Region&&) = default;
        Region& operator=(const Region&)  = default;
        Region& operator=(      Region&&) = default;
        

        Region(uint32 _x, uint32 _y, uint32 _width, uint32 _height)
            : x {_x}
            , y {_y}
            , width {_width}
            , height{_height}
        
        {}

        // Checks if the region is empty (width or height is zero).
        bool IsEmpty() const
        {
            return width == 0 || height == 0;
        }

        constexpr bool operator==(const Region& rhs) const
        {
            
            return x      == rhs.x     &&
                   y      == rhs.y     &&
                   width  == rhs.width &&
                   height == rhs.height;
            
        }
        constexpr bool operator!=(const Region& rhs) const
        {
            return !(*this == rhs);
        }

        struct Hasher
        {
            size_t operator()(const Region& R) const
            {
                return ComputeHash(R.width, R.height, R.x, R.y);
            }
        };
    };

    DynamicAtlasManager(uint32 Width, uint32 Height);
    ~DynamicAtlasManager();

    
    DynamicAtlasManager             (const DynamicAtlasManager&)  = delete;
    DynamicAtlasManager& operator = (const DynamicAtlasManager&)  = delete;
    DynamicAtlasManager             (      DynamicAtlasManager&&) = default;
    DynamicAtlasManager& operator = (      DynamicAtlasManager&&) = delete;
    


    // Allocates a rectangular region in the atlas.

    // \param Width  - Width of the region to allocate.
    // \param Height - Height of the region to allocate.
    // \return         The allocated region.
    //
    // If the requested region cannot be allocated, an empty region is returned.
    Region Allocate(uint32 Width, uint32 Height);


    // Frees a previously allocated region in the atlas.

    // \param R - The region to free.
    void Free(Region&& R);


    // Returns the number of free regions in the atlas.
    uint32 GetFreeRegionCount() const
    {
        VERIFY_EXPR(m_FreeRegionsByWidth.size() == m_FreeRegionsByHeight.size());
        return static_cast<uint32>(m_FreeRegionsByWidth.size());
    }

    // Returns the atlas width.
    uint32 GetWidth() const { return m_Width; }

    // Returns the atlas height.
    uint32 GetHeight() const { return m_Height; }

    // Returns the total free area of the atlas.

    // The total free area is the sum of the areas of all free regions in the atlas,
    // and thus may be fragmented.
    uint64 GetTotalFreeArea() const { return m_TotalFreeArea; }

    // Checks if the atlas is empty, i.e. if there are no allocated regions.
    bool IsEmpty() const
    {
        VERIFY_EXPR(m_AllocatedRegions.empty() && (m_TotalFreeArea == uint64{m_Width} * uint64{m_Height}) ||
                    !m_AllocatedRegions.empty() && (m_TotalFreeArea < uint64{m_Width} * uint64{m_Height}));
        return m_AllocatedRegions.empty();
    }

#define CMP(Member)                 \
    if (R0.Member < R1.Member)      \
        return true;                \
    else if (R0.Member > R1.Member) \
        return false;

    struct WidthFirstCompare
    {
        bool operator()(const Region& R0, const Region& R1) const
        {
            CMP(width)
            CMP(height)
            CMP(x)
            CMP(y)
            return false;
        }
    };
    struct HeightFirstCompare
    {
        bool operator()(const Region& R0, const Region& R1) const
        {
            CMP(height)
            CMP(width)
            CMP(y)
            CMP(x)
            return false;
        }
    };
#undef CMP

private:
#if SHZ_DEBUG
    void DbgVerifyRegion(const Region& R) const;
    void DbgVerifyConsistency() const;
    struct Node;
    void DbgRecursiveVerifyConsistency(const Node& N, uint32& Area) const;
#endif

    const uint32 m_Width;
    const uint32 m_Height;

    uint64 m_TotalFreeArea = 0;

    struct Node
    {
        Region R;
        bool   IsAllocated = false;
        Node*  Parent      = nullptr;

        void Split(const std::initializer_list<Region>& Regions);
        bool CanMergeChildren() const;
        void MergeChildren();
        bool HasChildren() const
        {
            VERIFY_EXPR(NumChildren == 0 && !Children || NumChildren != 0 && Children);
            VERIFY(!IsAllocated || NumChildren == 0, "Allocated nodes can't have children");
            return NumChildren != 0;
        }

        const Node& Child(uint32 i) const
        {
            VERIFY_EXPR(i < NumChildren);
            return Children[i];
        }
        Node& Child(uint32 i)
        {
            VERIFY_EXPR(i < NumChildren);
            return Children[i];
        }

        template <typename ProcessChildType>
        void ProcessChildren(ProcessChildType ProcessChild) const
        {
            for (uint32 i = 0; i < NumChildren; ++i)
                ProcessChild(Child(i));
        }
        template <typename ProcessChildType>
        void ProcessChildren(ProcessChildType ProcessChild)
        {
            for (uint32 i = 0; i < NumChildren; ++i)
                ProcessChild(Child(i));
        }

#if SHZ_DEBUG
        void Validate() const;
#endif
    private:
        uint32                  NumChildren = 0;
        std::unique_ptr<Node[]> Children;
    };
    std::unique_ptr<Node> m_Root{new Node};

    void RegisterNode(Node& N);
    void UnregisterNode(const Node& N);

    // Free regions ordered by width->height->x->y
    std::map<Region, Node*, WidthFirstCompare> m_FreeRegionsByWidth;
    // Free regions ordered by height->width->y->x
    std::map<Region, Node*, HeightFirstCompare> m_FreeRegionsByHeight;
    // Allocated regions
    std::unordered_map<Region, Node*, Region::Hasher> m_AllocatedRegions;
};

} // namespace shz
