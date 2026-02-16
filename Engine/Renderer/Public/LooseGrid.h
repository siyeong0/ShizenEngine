#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <algorithm>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Math/Public/ViewFrustum.h"

namespace shz
{
	// Dynamic objects: hashed loose grid.
	// - Insert by world AABB
	// - Query by frustum AABB range (derived from frustum corners)
	class LooseGrid final
	{
	public:
		struct CreateInfo final
		{
			float CellSize = 32.0f;     // meters
			float LooseFactor = 2.0f;   // >1 means each cell acts larger (reduces churn)
			uint32 ReserveCells = 1024;
		};

		LooseGrid() = default;
		explicit LooseGrid(const CreateInfo& ci) { Initialize(ci); }

		void Initialize(const CreateInfo& ci);
		void Reset();

		// Handle payload (e.g., object dense index).
		void InsertOrUpdate(uint32 payload, const Box& worldAabb);
		void Remove(uint32 payload);

		std::vector<uint32> QueryFrustum(const ViewFrustumExt& frustum) const;

	private:
		struct CellCoord final
		{
			int32 x = 0;
			int32 y = 0;
			int32 z = 0;

			bool operator==(const CellCoord& rhs) const noexcept { return x == rhs.x && y == rhs.y && z == rhs.z; }
		};

		struct CellCoordHasher final
		{
			size_t operator()(const CellCoord& c) const noexcept
			{
				// simple mix
				size_t h = static_cast<size_t>(c.x) * 73856093u;
				h ^= static_cast<size_t>(c.y) * 19349663u;
				h ^= static_cast<size_t>(c.z) * 83492791u;
				return h;
			}
		};

		struct PayloadCells final
		{
			std::vector<CellCoord> Cells;
		};

	private:
		CellCoord toCell(const float3& p) const;
		void computeCoveredCellsLoose(const Box& aabb, std::vector<CellCoord>& out) const;

		static Box FrustumWorldAabb(const ViewFrustumExt& frustum);

		// Dedup helper: gather unique payloads from cells
		static void dedupAppend(std::vector<uint32>& out, const std::vector<uint32>& in);

	private:
		float m_CellSize = 32.0f;
		float m_InvCellSize = 1.0f / 32.0f;
		float m_LooseFactor = 2.0f;

		// cell -> payload list
		std::unordered_map<CellCoord, std::vector<uint32>, CellCoordHasher> m_Cells;

		// payload -> list of cells it lives in (for fast update/remove)
		std::unordered_map<uint32, PayloadCells> m_PayloadMap;
	};
} // namespace shz