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
#include "DynamicAtlasManager.hpp"

#include "Engine/Core/Math/Math.h"

namespace shz
{

	static const DynamicAtlasManager::Region InvalidRegion{ UINT_MAX, UINT_MAX, 0, 0 };

#if SHZ_DEBUG
	void DynamicAtlasManager::Node::Validate() const
	{
		ASSERT(NumChildren == 0 || NumChildren == 2 || NumChildren == 3, "Only zero, two or three children are expected");
		ASSERT(NumChildren == 0 || !IsAllocated, "Allocated nodes must not have children");
		if (NumChildren > 0)
		{
			uint32 Area = 0;
			for (uint32 i = 0; i < NumChildren; ++i)
			{
				const Region& R0 = Child(i).R;

				ASSERT(!R0.IsEmpty(), "Region must not be empty");
				ASSERT(R0.x >= R.x && R0.x + R0.width <= R.x + R.width && R0.y >= R.y && R0.y + R0.height <= R.y + R.height,
					"Child region [", R0.x, ", ", R0.x + R0.width, ") x [", R0.y, ", ", R0.y + R0.height,
					") is not contained in its parent [", R.x, ", ", R.x + R.width, ") x [", R.y, ", ", R.y + R.height, ")");

				Area += R0.width * R0.height;

				for (uint32 j = i + 1; j < NumChildren; ++j)
				{
					const Region& R1 = Child(j).R;
					if (CheckBox2DBox2DOverlap<false>(Vector2{ (float)R0.x, (float)R0.y }, Vector2{ (float)R0.x + (float)R0.width, (float)R0.y + (float)R0.height },
						Vector2{ (float)R1.x, (float)R1.y }, Vector2{ (float)R1.x + (float)R1.width, R1.y + (float)R1.height }))
					{
						ASSERT(false, "Child regions [", R0.x, ", ", R0.x + R0.width, ") x [", R0.y, ", ", R0.y + R0.height,
							") and [", R1.x, ", ", R1.x + R1.width, ") x [", R1.y, ", ", R1.y + R1.height, ") overlap");
					}
				}
			}
			ASSERT(Area == R.width * R.height, "Children do not cover entire parent region");
		}
	}
#endif

	void DynamicAtlasManager::Node::Split(const std::initializer_list<Region>& Regions)
	{
		ASSERT(Regions.size() >= 2, "There must be at least two regions");
		ASSERT(!HasChildren(), "This node already has children and can't be split");
		ASSERT(!IsAllocated, "Allocated region can't be split");

		Children.reset(new Node[Regions.size()]);
		NumChildren = 0;
		for (const Region& ChildR : Regions)
		{
			Children[NumChildren].Parent = this;
			Children[NumChildren].R = ChildR;
			++NumChildren;
		}
		ASSERT_EXPR(NumChildren == Regions.size());

#if SHZ_DEBUG
		Validate();
#endif
	}

	bool DynamicAtlasManager::Node::CanMergeChildren() const
	{
		bool CanMerge = true;
		for (uint32 i = 0; i < NumChildren && CanMerge; ++i)
			CanMerge = !Child(i).IsAllocated && !Child(i).HasChildren();

		return CanMerge;
	}

	void DynamicAtlasManager::Node::MergeChildren()
	{
		ASSERT_EXPR(HasChildren());
		ASSERT_EXPR(CanMergeChildren());
		Children.reset();
		NumChildren = 0;
	}


	DynamicAtlasManager::DynamicAtlasManager(uint32 Width, uint32 Height)
		: m_Width{ Width }
		, m_Height{ Height }
		, m_TotalFreeArea{ uint64{Width} *uint64{Height} }
	{
		m_Root->R = Region{ 0, 0, Width, Height };
		RegisterNode(*m_Root);
	}


	DynamicAtlasManager::~DynamicAtlasManager()
	{
		if (m_Root)
		{
#if SHZ_DEBUG
			DbgVerifyConsistency();
#endif

			ASSERT(!m_Root->IsAllocated && !m_Root->HasChildren(), "Root node is expected to be free and have no children");
			ASSERT_EXPR(m_FreeRegionsByWidth.size() == m_FreeRegionsByHeight.size());
			ASSERT(m_FreeRegionsByWidth.size() == 1, "There expected to be a single free region");
			ASSERT(m_AllocatedRegions.empty(), "There must be no allocated regions");
		}
		else
		{
			ASSERT_EXPR(m_FreeRegionsByWidth.empty());
			ASSERT_EXPR(m_FreeRegionsByHeight.empty());
			ASSERT_EXPR(m_AllocatedRegions.empty());
		}
	}

	void DynamicAtlasManager::RegisterNode(Node& N)
	{
		ASSERT(!N.HasChildren(), "Registering node that has children");
		ASSERT(!N.R.IsEmpty(), "Region must not be empty");

		ASSERT(m_AllocatedRegions.find(N.R) == m_AllocatedRegions.end(), "New region should not be present in allocated regions hash map");
		ASSERT(m_FreeRegionsByWidth.find(N.R) == m_FreeRegionsByWidth.end(), "New region should not be present in free regions map");
		ASSERT(m_FreeRegionsByHeight.find(N.R) == m_FreeRegionsByHeight.end(), "New region should not be present in free regions map");

		if (N.IsAllocated)
		{
			m_AllocatedRegions.emplace(N.R, &N);
		}
		else
		{
			m_FreeRegionsByWidth.emplace(N.R, &N);
			m_FreeRegionsByHeight.emplace(N.R, &N);
		}
	}

	void DynamicAtlasManager::UnregisterNode(const Node& N)
	{
		ASSERT(!N.HasChildren(), "Unregistering node that has children");
		ASSERT(!N.R.IsEmpty(), "Region must not be empty");

		if (N.IsAllocated)
		{
			ASSERT(m_AllocatedRegions.find(N.R) != m_AllocatedRegions.end(), "Region is not found in allocated regions hash map");
			m_AllocatedRegions.erase(N.R);
		}
		else
		{
			ASSERT(m_FreeRegionsByWidth.find(N.R) != m_FreeRegionsByWidth.end(), "Region is not found in free regions map");
			ASSERT(m_FreeRegionsByHeight.find(N.R) != m_FreeRegionsByHeight.end(), "Region is not is not in free regions map");
			m_FreeRegionsByWidth.erase(N.R);
			m_FreeRegionsByHeight.erase(N.R);
		}
	}



	DynamicAtlasManager::Region DynamicAtlasManager::Allocate(uint32 Width, uint32 Height)
	{
		auto it_w = m_FreeRegionsByWidth.lower_bound(Region{ 0, 0, Width, 0 });
		while (it_w != m_FreeRegionsByWidth.end() && it_w->first.height < Height)
			++it_w;
		ASSERT_EXPR(it_w == m_FreeRegionsByWidth.end() || (it_w->first.width >= Width && it_w->first.height >= Height));

		auto it_h = m_FreeRegionsByHeight.lower_bound(Region{ 0, 0, 0, Height });
		while (it_h != m_FreeRegionsByHeight.end() && it_h->first.width < Width)
			++it_h;
		ASSERT_EXPR(it_h == m_FreeRegionsByHeight.end() || (it_h->first.width >= Width && it_h->first.height >= Height));

		const uint32 AreaW = it_w != m_FreeRegionsByWidth.end() ? it_w->first.width * it_w->first.height : 0;
		const uint32 AreaH = it_h != m_FreeRegionsByHeight.end() ? it_h->first.width * it_h->first.height : 0;
		ASSERT_EXPR(AreaW == 0 || AreaW >= Width * Height);
		ASSERT_EXPR(AreaH == 0 || AreaH >= Width * Height);

		Node* pSrcNode = nullptr;
		// Use the smaller area source region
		if (AreaW > 0 && AreaH > 0)
		{
			pSrcNode = AreaW < AreaH ? it_w->second : it_h->second;
		}
		else if (AreaW > 0)
		{
			pSrcNode = it_w->second;
		}
		else if (AreaH > 0)
		{
			pSrcNode = it_h->second;
		}
		else
		{
			return Region{};
		}

		UnregisterNode(*pSrcNode);

		Region R = pSrcNode->R;
		if (R.width > Width && R.height > Height)
		{
			if (R.width > R.height)
			{
				//    _____________________
				//   |       |             |
				//   |   B   |             |
				//   |_______|      A      |
				//   |       |             |
				//   |   R   |             |
				//   |_______|_____________|
				//
				pSrcNode->Split(
					{

						Region{R.x,         R.y,          Width,           Height           }, // R
						Region{R.x + Width, R.y,          R.width - Width, R.height         }, // A
						Region{R.x,         R.y + Height, Width,           R.height - Height}  // B

					});
			}
			else
			{
				//   _____________
				//  |             |
				//  |             |
				//  |      A      |
				//  |             |
				//  |_____ _______|
				//  |     |       |
				//  |  R  |   B   |
				//  |_____|_______|
				//
				pSrcNode->Split(
					{

						Region{R.x,         R.y,          Width,           Height           }, // R
						Region{R.x,         R.y + Height, R.width,         R.height - Height}, // A
						Region{R.x + Width, R.y,          R.width - Width, Height           }  // B

					});
			}
		}
		else if (R.width > Width)
		{
			//   _______ __________
			//  |       |          |
			//  |   R   |    A     |
			//  |_______|__________|
			//
			pSrcNode->Split(
				{

					Region{R.x,         R.y, Width,           Height  }, // R
					Region{R.x + Width, R.y, R.width - Width, R.height}  // A

				});
		}
		else if (R.height > Height)
		{
			//    _______
			//   |       |
			//   |   A   |
			//   |_______|
			//   |       |
			//   |   R   |
			//   |_______|
			//
			pSrcNode->Split(
				{

					Region{R.x,          R.y,   Width, Height           }, // R
					Region{R.x, R.y + Height, R.width, R.height - Height}  // A

				});
		}

		R.width = Width;
		R.height = Height;
		if (pSrcNode->HasChildren())
		{
			ASSERT_EXPR(pSrcNode->Child(0).R == R);
			pSrcNode->Child(0).IsAllocated = true;
			pSrcNode->ProcessChildren([this](Node& Child) //
				{
					RegisterNode(Child);
				});
		}
		else
		{
			ASSERT_EXPR(pSrcNode->R == R);
			pSrcNode->IsAllocated = true;
			RegisterNode(*pSrcNode);
		}

		ASSERT_EXPR(m_TotalFreeArea >= uint64{ R.width } *uint64{ R.height });
		m_TotalFreeArea -= uint64{ R.width } *uint64{ R.height };

#if SHZ_DEBUG
		DbgVerifyConsistency();
#endif

		return R;
	}


	void DynamicAtlasManager::Free(Region&& R)
	{
#if SHZ_DEBUG
		DbgVerifyRegion(R);
#endif

		auto node_it = m_AllocatedRegions.find(R);
		if (node_it == m_AllocatedRegions.end())
		{
			ASSERT(false, "Unable to find region [", R.x, ", ", R.x + R.width, ") x [", R.y, ", ", R.y + R.height, ") among allocated regions. Have you ever allocated it?");
			return;
		}

		ASSERT_EXPR(node_it->first == R && node_it->second->R == R);
		Node* N = node_it->second;
		ASSERT_EXPR(N->IsAllocated && !N->HasChildren());
		UnregisterNode(*N);
		N->IsAllocated = false;
		RegisterNode(*N);

		N = N->Parent;
		while (N != nullptr && N->CanMergeChildren())
		{
			N->ProcessChildren([this](const Node& Child) //
				{
					UnregisterNode(Child);
				});
			N->MergeChildren();
			RegisterNode(*N);

			N = N->Parent;
		}

		m_TotalFreeArea += uint64{ R.width } *uint64{ R.height };

#if SHZ_DEBUG
		DbgVerifyConsistency();
#endif

		R = InvalidRegion;
	}


#if SHZ_DEBUG

	void DynamicAtlasManager::DbgVerifyRegion(const Region& R) const
	{
		ASSERT_EXPR(R != InvalidRegion);
		ASSERT_EXPR(!R.IsEmpty());

		ASSERT(R.x < m_Width, "Region x (", R.x, ") exceeds atlas width (", m_Width, ").");
		ASSERT(R.y < m_Height, "Region y (", R.y, ") exceeds atlas height (", m_Height, ").");
		ASSERT(R.x + R.width <= m_Width, "Region right boundary (", R.x + R.width, ") exceeds atlas width (", m_Width, ").");
		ASSERT(R.y + R.height <= m_Height, "Region top boundary (", R.y + R.height, ") exceeds atlas height (", m_Height, ").");
	}

	void DynamicAtlasManager::DbgRecursiveVerifyConsistency(const Node& N, uint32& Area) const
	{
		N.Validate();
		if (N.HasChildren())
		{
			ASSERT_EXPR(!N.IsAllocated);
			ASSERT(m_AllocatedRegions.find(N.R) == m_AllocatedRegions.end(), "Regions with children must not be present in allocated regions hash map");
			ASSERT(m_FreeRegionsByWidth.find(N.R) == m_FreeRegionsByWidth.end(), "Regions with children must not be present in free regions map");
			ASSERT(m_FreeRegionsByHeight.find(N.R) == m_FreeRegionsByHeight.end(), "Regions with children must not be present in free regions map");

			N.ProcessChildren([&Area, this](const Node& Child) //
				{
					DbgRecursiveVerifyConsistency(Child, Area);
				});
		}
		else
		{
			if (N.IsAllocated)
			{
				ASSERT(m_AllocatedRegions.find(N.R) != m_AllocatedRegions.end(), "Allocated region is not found in allocated regions hash map");
				ASSERT(m_FreeRegionsByWidth.find(N.R) == m_FreeRegionsByWidth.end(), "Allocated region should not be present in free regions map");
				ASSERT(m_FreeRegionsByHeight.find(N.R) == m_FreeRegionsByHeight.end(), "Allocated region should not be present in free regions map");
			}
			else
			{
				ASSERT(m_AllocatedRegions.find(N.R) == m_AllocatedRegions.end(), "Free region is found in allocated regions hash map");
				ASSERT(m_FreeRegionsByWidth.find(N.R) != m_FreeRegionsByWidth.end(), "Free region is not found in free regions map");
				ASSERT(m_FreeRegionsByHeight.find(N.R) != m_FreeRegionsByHeight.end(), "Free region is not found in free regions map");
			}

			Area += N.R.width * N.R.height;
		}
	}

	void DynamicAtlasManager::DbgVerifyConsistency() const
	{
		ASSERT_EXPR(m_FreeRegionsByWidth.size() == m_FreeRegionsByHeight.size());
		uint32 Area = 0;

		DbgRecursiveVerifyConsistency(*m_Root, Area);

		ASSERT(Area == m_Width * m_Height, "Not entire atlas area has been covered");

		{
			uint64 FreeArea = 0;
			for (const auto& it : m_FreeRegionsByWidth)
				FreeArea += uint64{ it.second->R.width } *uint64{ it.second->R.height };
			ASSERT_EXPR(FreeArea == m_TotalFreeArea);
		}
		{
			uint32 FreeArea = 0;
			for (const auto& it : m_FreeRegionsByHeight)
				FreeArea += uint64{ it.second->R.width } *uint64{ it.second->R.height };
			ASSERT_EXPR(FreeArea == m_TotalFreeArea);
		}
	}
#endif // SHZ_DEBUG

} // namespace shz
