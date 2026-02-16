#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Math/Public/ViewFrustum.h"

namespace shz
{
	// BVH entry for an object (identified by user payload index).
	struct BVHEntry final
	{
		Box   WorldAabb = {};
		Sphere WorldSphere = {};
		uint32 Payload = 0; // e.g., object dense index or OcIndex
	};

	class StaticBVH final
	{
	public:
		StaticBVH() = default;
		StaticBVH(const StaticBVH&) = delete;
		StaticBVH& operator=(const StaticBVH&) = delete;

		void Reset();

		// Replace all entries and rebuild.
		void Build(const std::vector<BVHEntry>& entries);

		bool IsBuilt() const noexcept { return m_Built; }
		uint32 GetEntryCount() const noexcept { return static_cast<uint32>(m_Entries.size()); }

		// Frustum query (AABB planes test) -> output payload list
		std::vector<uint32> QueryFrustum(const ViewFrustum& frustum) const;

	private:
		struct Node final
		{
			Box Bounds = {};
			uint32 Left = 0xFFFFFFFFu;
			uint32 Right = 0xFFFFFFFFu;

			uint32 First = 0;
			uint32 Count = 0;

			bool IsLeaf() const noexcept { return Left == 0xFFFFFFFFu && Right == 0xFFFFFFFFu; }
		};

	private:
		uint32 buildNode(uint32 first, uint32 count);

		static Box UnionAabb(const Box& a, const Box& b);
		static Box ComputeBounds(const std::vector<BVHEntry>& entries, const std::vector<uint32>& idx, uint32 first, uint32 count);
		static float3 ComputeCenter(const Box& aabb);

		static bool IntersectsFrustumAABB(const ViewFrustum& frustum, const Box& aabb);

	private:
		std::vector<BVHEntry> m_Entries;
		std::vector<uint32>   m_Index;   // permutation into m_Entries
		std::vector<Node>     m_Nodes;

		bool m_Built = false;

		// Tuning
		uint32 m_MaxLeafSize = 8;
	};
} // namespace shz