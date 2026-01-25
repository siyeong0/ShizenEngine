#include "pch.h"
#include "Engine/RuntimeData/Public/StaticMeshAsset.h"

#include <limits>

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
	}

	void StaticMeshAsset::SetIndicesU16(std::vector<uint16>&& indices)
	{
		m_IndexType = VT_UINT16;
		m_IndicesU16 = std::move(indices);

		m_IndicesU32.clear();
	}

	const void* StaticMeshAsset::GetIndexData() const noexcept
	{
		if (m_IndexType == VT_UINT32)
		{
			if (m_IndicesU32.empty())
			{
				return nullptr;
			}
			return static_cast<const void*>(m_IndicesU32.data());
		}
		else
		{
			if (m_IndicesU16.empty())
			{
				return nullptr;
			}
			return static_cast<const void*>(m_IndicesU16.data());
		}
	}

	uint32 StaticMeshAsset::GetIndexDataSizeBytes() const noexcept
	{
		if (m_IndexType == VT_UINT32)
		{
			const uint64 bytes = static_cast<uint64>(m_IndicesU32.size()) * sizeof(uint32);
			return static_cast<uint32>(bytes);
		}
		else
		{
			const uint64 bytes = static_cast<uint64>(m_IndicesU16.size()) * sizeof(uint16);
			return static_cast<uint32>(bytes);
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

	uint32 StaticMeshAsset::GetIndexAt(uint32 i) const noexcept
	{
		if (m_IndexType == VT_UINT32)
		{
			return m_IndicesU32[i];
		}
		else
		{
			return static_cast<uint32>(m_IndicesU16[i]);
		}
	}

	// ------------------------------------------------------------
	// Material slots
	// ------------------------------------------------------------
	MaterialAsset& StaticMeshAsset::GetMaterialSlot(uint32 slot) noexcept
	{
		ASSERT(slot < static_cast<uint32>(m_MaterialSlots.size()), "Material slot index out of range.");
		return m_MaterialSlots[slot];
	}

	const MaterialAsset& StaticMeshAsset::GetMaterialSlot(uint32 slot) const noexcept
	{
		ASSERT(slot < static_cast<uint32>(m_MaterialSlots.size()), "Material slot index out of range.");
		return m_MaterialSlots[slot];
	}

	// ------------------------------------------------------------
	// Validation / policy
	// ------------------------------------------------------------
	bool StaticMeshAsset::IsValid() const noexcept
	{
		// Positions are required.
		if (m_Positions.empty())
		{
			return false;
		}

		// Indices are required.
		if (GetIndexCount() == 0)
		{
			return false;
		}

		const size_t vtxCount = m_Positions.size();

		// Optional streams: if present, they must match vertex count.
		if (!m_Normals.empty())
		{
			if (m_Normals.size() != vtxCount)
			{
				return false;
			}
		}

		if (!m_Tangents.empty())
		{
			if (m_Tangents.size() != vtxCount)
			{
				return false;
			}
		}

		if (!m_TexCoords.empty())
		{
			if (m_TexCoords.size() != vtxCount)
			{
				return false;
			}
		}

		// Sections are optional. If provided, they must be in-range.
		const uint32 indexCount = GetIndexCount();
		for (const Section& sec : m_Sections)
		{
			if (sec.IndexCount == 0)
			{
				return false;
			}

			const uint64 end = static_cast<uint64>(sec.FirstIndex) + static_cast<uint64>(sec.IndexCount);
			if (end > static_cast<uint64>(indexCount))
			{
				return false;
			}

			// If materials exist, ensure section slot is within range.
			if (!m_MaterialSlots.empty())
			{
				if (sec.MaterialSlot >= static_cast<uint32>(m_MaterialSlots.size()))
				{
					return false;
				}
			}
		}

		return true;
	}

	bool StaticMeshAsset::HasCPUData() const noexcept
	{
		if (m_Positions.empty())
		{
			return false;
		}

		if (GetIndexCount() == 0)
		{
			return false;
		}

		return true;
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
			if (p.x < minV.x) { minV.x = p.x; }
			if (p.y < minV.y) { minV.y = p.y; }
			if (p.z < minV.z) { minV.z = p.z; }

			if (p.x > maxV.x) { maxV.x = p.x; }
			if (p.y > maxV.y) { maxV.y = p.y; }
			if (p.z > maxV.z) { maxV.z = p.z; }
		}

		m_Bounds = Box(minV, maxV);

		RecomputeSectionBounds();
	}

	void StaticMeshAsset::RecomputeSectionBounds()
	{
		if (m_Sections.empty())
		{
			return;
		}

		if (!HasCPUData())
		{
			for (Section& sec : m_Sections)
			{
				sec.LocalBounds = Box{};
			}
			return;
		}

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
				{
					continue;
				}

				const float3& p = m_Positions[idx];

				if (p.x < minV.x) { minV.x = p.x; }
				if (p.y < minV.y) { minV.y = p.y; }
				if (p.z < minV.z) { minV.z = p.z; }

				if (p.x > maxV.x) { maxV.x = p.x; }
				if (p.y > maxV.y) { maxV.y = p.y; }
				if (p.z > maxV.z) { maxV.z = p.z; }
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
		m_Normals.clear();
		m_Tangents.clear();
		m_TexCoords.clear();

		m_IndicesU32.clear();
		m_IndicesU16.clear();
	}

	void StaticMeshAsset::Clear()
	{
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
