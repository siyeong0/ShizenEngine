#include "pch.h"
#include "StaticMeshAsset.h"

#include <limits>
#include <utility>

namespace shz
{
	// ------------------------------------------------------------
	// Geometry setters
	// ------------------------------------------------------------
	void StaticMeshAsset::ReserveVertices(uint32 count)
	{
		m_Positions.reserve(count);
		m_Normals.reserve(count);
		m_Tangents.reserve(count);
		m_TexCoords.reserve(count);
	}

	// ------------------------------------------------------------
	// Indices
	// ------------------------------------------------------------

	void StaticMeshAsset::SetIndicesU32(std::vector<uint32>&& indices)
	{
		m_IndexType = VT_UINT32;
		m_IndicesU32 = std::move(indices);

		m_IndicesU16.clear();
		m_IndicesU16.shrink_to_fit();
	}

	void StaticMeshAsset::SetIndicesU16(std::vector<uint16>&& indices)
	{
		m_IndexType = VT_UINT16;
		m_IndicesU16 = std::move(indices);

		m_IndicesU32.clear();
		m_IndicesU32.shrink_to_fit();
	}

	const void* StaticMeshAsset::GetIndexData() const noexcept
	{
		if (m_IndexType == VT_UINT32)
		{
			return m_IndicesU32.empty() ? nullptr : static_cast<const void*>(m_IndicesU32.data());
		}
		else
		{
			return m_IndicesU16.empty() ? nullptr : static_cast<const void*>(m_IndicesU16.data());
		}
	}

	uint32 StaticMeshAsset::GetIndexDataSizeBytes() const noexcept
	{
		if (m_IndexType == VT_UINT32)
		{
			return static_cast<uint32>(m_IndicesU32.size() * sizeof(uint32));
		}
		else
		{
			return static_cast<uint32>(m_IndicesU16.size() * sizeof(uint16));
		}
	}

	uint32 StaticMeshAsset::GetIndexCount() const noexcept
	{
		if (m_IndexType == VT_UINT32)
		{
			return static_cast<uint32>(m_IndicesU32.size());
		}
		else
		{
			return static_cast<uint32>(m_IndicesU16.size());
		}
	}

	// ------------------------------------------------------------
	// Material slots
	// ------------------------------------------------------------

	MaterialAsset* StaticMeshAsset::GetMaterialSlot(uint32 slot) noexcept
	{
		if (slot >= static_cast<uint32>(m_MaterialSlots.size()))
			return nullptr;

		return &m_MaterialSlots[slot];
	}

	const MaterialAsset* StaticMeshAsset::GetMaterialSlot(uint32 slot) const noexcept
	{
		if (slot >= static_cast<uint32>(m_MaterialSlots.size()))
			return nullptr;

		return &m_MaterialSlots[slot];
	}

	// ------------------------------------------------------------
	// Validation / policy
	// ------------------------------------------------------------

	bool StaticMeshAsset::IsValid() const noexcept
	{
		if (m_Positions.empty())
			return false;

		// Enforce attribute array size consistency (for your current "simple" mesh format).
		const size_t vtxCount = m_Positions.size();
		if (m_Normals.size() != vtxCount)  return false;
		if (m_Tangents.size() != vtxCount) return false;
		if (m_TexCoords.size() != vtxCount) return false;

		if (GetIndexCount() == 0)
			return false;

		// Sections are optional. If provided, they should be consistent.
		for (const Section& sec : m_Sections)
		{
			if (sec.IndexCount == 0)
				return false;

			const uint64 end = static_cast<uint64>(sec.FirstIndex) + static_cast<uint64>(sec.IndexCount);
			if (end > static_cast<uint64>(GetIndexCount()))
				return false;

			// If materials exist, ensure section slot is within range.
			if (!m_MaterialSlots.empty())
			{
				if (sec.MaterialSlot >= static_cast<uint32>(m_MaterialSlots.size()))
					return false;
			}
		}

		return true;
	}

	bool StaticMeshAsset::HasCPUData() const noexcept
	{
		return !m_Positions.empty() && (GetIndexCount() != 0);
	}

	// ------------------------------------------------------------
	// Bounds
	// ------------------------------------------------------------

	void StaticMeshAsset::RecomputeBounds()
	{
		if (m_Positions.empty())
		{
			m_Bounds = Box{};
			for (Section& sec : m_Sections)
			{
				sec.LocalBounds = Box{};
			}
			return;
		}

		float3 minV(
			+std::numeric_limits<float>::infinity(),
			+std::numeric_limits<float>::infinity(),
			+std::numeric_limits<float>::infinity());

		float3 maxV(
			-std::numeric_limits<float>::infinity(),
			-std::numeric_limits<float>::infinity(),
			-std::numeric_limits<float>::infinity());

		for (const float3& p : m_Positions)
		{
			if (p.x < minV.x) minV.x = p.x;
			if (p.y < minV.y) minV.y = p.y;
			if (p.z < minV.z) minV.z = p.z;

			if (p.x > maxV.x) maxV.x = p.x;
			if (p.y > maxV.y) maxV.y = p.y;
			if (p.z > maxV.z) maxV.z = p.z;
		}

		m_Bounds = Box(minV, maxV);

		recomputeSectionBounds();
	}

	void StaticMeshAsset::recomputeSectionBounds()
	{
		if (m_Sections.empty())
			return;

		if (!HasCPUData())
		{
			for (Section& sec : m_Sections)
			{
				sec.LocalBounds = Box{};
			}
			return;
		}

		auto GetIndexAt = [&](uint32 i) -> uint32
		{
			if (m_IndexType == VT_UINT32)
			{
				return m_IndicesU32[i];
			}
			else
			{
				return static_cast<uint32>(m_IndicesU16[i]);
			}
		};

		for (Section& sec : m_Sections)
		{
			if (sec.IndexCount == 0)
			{
				sec.LocalBounds = Box{};
				continue;
			}

			float3 minV(
				+std::numeric_limits<float>::infinity(),
				+std::numeric_limits<float>::infinity(),
				+std::numeric_limits<float>::infinity());

			float3 maxV(
				-std::numeric_limits<float>::infinity(),
				-std::numeric_limits<float>::infinity(),
				-std::numeric_limits<float>::infinity());

			const uint32 end = sec.FirstIndex + sec.IndexCount;
			for (uint32 i = sec.FirstIndex; i < end; ++i)
			{
				const uint32 idx = GetIndexAt(i);
				if (idx >= static_cast<uint32>(m_Positions.size()))
					continue;

				const float3& p = m_Positions[idx];

				if (p.x < minV.x) minV.x = p.x;
				if (p.y < minV.y) minV.y = p.y;
				if (p.z < minV.z) minV.z = p.z;

				if (p.x > maxV.x) maxV.x = p.x;
				if (p.y > maxV.y) maxV.y = p.y;
				if (p.z > maxV.z) maxV.z = p.z;
			}

			sec.LocalBounds = Box(minV, maxV);
		}
	}

	// ------------------------------------------------------------
	// Memory
	// ------------------------------------------------------------

	void StaticMeshAsset::StripCPUData()
	{
		m_Positions.clear();
		m_Positions.shrink_to_fit();

		m_Normals.clear();
		m_Normals.shrink_to_fit();

		m_Tangents.clear();
		m_Tangents.shrink_to_fit();

		m_TexCoords.clear();
		m_TexCoords.shrink_to_fit();

		m_IndicesU32.clear();
		m_IndicesU32.shrink_to_fit();

		m_IndicesU16.clear();
		m_IndicesU16.shrink_to_fit();
	}

	void StaticMeshAsset::Clear()
	{
		m_Name.clear();
		m_SourcePath.clear();

		m_Positions.clear();
		m_Normals.clear();
		m_Tangents.clear();
		m_TexCoords.clear();

		m_IndicesU32.clear();
		m_IndicesU16.clear();
		m_Sections.clear();
		m_MaterialSlots.clear();

		m_IndexType = VT_UINT32;
		m_Bounds = Box{};
	}
} // namespace shz
