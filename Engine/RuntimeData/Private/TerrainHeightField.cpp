#include "pch.h"
#include "Engine/RuntimeData/Public/TerrainHeightField.h"

#include <cmath>

#include "Engine/Core/Math/Math.h"

namespace shz
{
	// -----------------------------
	// Helpers
	// -----------------------------

	float TerrainHeightField::u16ToNormalized(uint16 v)
	{
		// Map [0..65535] -> [0..1]
		return static_cast<float>(v) * (1.0f / 65535.0f);
	}

	uint16 TerrainHeightField::normalizedToU16(float n)
	{
		// Clamp to [0..1], then map to [0..65535]
		const float c = Clamp01(n);

		// Round to nearest for less bias.
		// 0 -> 0, 1 -> 65535
		const float scaled = c * 65535.0f;
		const uint32 iv = static_cast<uint32>(scaled + 0.5f);

		return static_cast<uint16>(iv > 65535u ? 65535u : iv);
	}

	// -----------------------------
	// Lifecycle
	// -----------------------------

	void TerrainHeightField::Initialize(const TerrainHeightFieldCreateInfo& ci)
	{
		m_CI = ci;
		ASSERT(m_CI.Width > 0 && m_CI.Height > 0, "TerrainHeightFieldCreateInfo has invalid dimensions.");

		const uint64 count = static_cast<uint64>(m_CI.Width) * static_cast<uint64>(m_CI.Height);
		m_DataU16.assign(static_cast<size_t>(count), 0u);
	}

	void TerrainHeightField::Cleanup()
	{
		m_DataU16.clear();
		m_DataU16.shrink_to_fit();
		m_CI = {};
	}

	// -----------------------------
	// Accessors
	// -----------------------------

	float TerrainHeightField::GetNormalizedHeightAt(uint32 x, uint32 z) const
	{
		ASSERT(IsValid(), "TerrainHeightField is not valid.");
		ASSERT(x < m_CI.Width, "X coordinate out of range.");
		ASSERT(z < m_CI.Height, "Z coordinate out of range.");

		const uint32 idx = getIndex(x, z);
		ASSERT(idx < static_cast<uint32>(m_DataU16.size()), "Index out of range.");

		return u16ToNormalized(m_DataU16[idx]);
	}

	float TerrainHeightField::GetWorldHeightAt(uint32 x, uint32 z) const
	{
		const float n = GetNormalizedHeightAt(x, z);
		return m_CI.HeightOffset + n * m_CI.HeightScale;
	}

	void TerrainHeightField::SetNormalizedHeightAt(uint32 x, uint32 z, float normalizedHeight)
	{
		ASSERT(IsValid(), "TerrainHeightField is not valid.");
		ASSERT(x < m_CI.Width, "X coordinate out of range.");
		ASSERT(z < m_CI.Height, "Z coordinate out of range.");

		const uint32 idx = getIndex(x, z);
		ASSERT(idx < static_cast<uint32>(m_DataU16.size()), "Index out of range.");

		m_DataU16[idx] = normalizedToU16(normalizedHeight);
	}

	// -----------------------------
	// Sampling
	// -----------------------------

	float TerrainHeightField::SampleNormalizedHeight(float worldX, float worldZ) const
	{
		ASSERT(IsValid(), "TerrainHeightField is not valid.");
		ASSERT(m_CI.WorldSpacingX > 0.f, "WorldSpacingX must be greater than zero.");
		ASSERT(m_CI.WorldSpacingZ > 0.f, "WorldSpacingZ must be greater than zero.");

		// Compute origin used by TerrainMeshBuilder
		const float sizeX = GetWorldSizeX(); // (Width - 1) * spacingX
		const float sizeZ = GetWorldSizeZ();

		const float originX = -0.5f * sizeX;
		const float originZ = -0.5f * sizeZ;

		// World -> grid coordinate (float)
		const float gx = (worldX - originX) / m_CI.WorldSpacingX;
		const float gz = (worldZ - originZ) / m_CI.WorldSpacingZ;

		const float maxX = static_cast<float>(m_CI.Width - 1);
		const float maxZ = static_cast<float>(m_CI.Height - 1);

		const float x = Clamp(gx, 0.f, maxX);
		const float z = Clamp(gz, 0.f, maxZ);

		const uint32 x0 = static_cast<uint32>(std::floor(x));
		const uint32 z0 = static_cast<uint32>(std::floor(z));

		const uint32 x1 = (x0 + 1 < m_CI.Width) ? (x0 + 1) : x0;
		const uint32 z1 = (z0 + 1 < m_CI.Height) ? (z0 + 1) : z0;

		const float tx = x - static_cast<float>(x0);
		const float tz = z - static_cast<float>(z0);

		const float h00 = GetNormalizedHeightAt(x0, z0);
		const float h10 = GetNormalizedHeightAt(x1, z0);
		const float h01 = GetNormalizedHeightAt(x0, z1);
		const float h11 = GetNormalizedHeightAt(x1, z1);

		const float hx0 = h00 + (h10 - h00) * tx;
		const float hx1 = h01 + (h11 - h01) * tx;
		return Clamp01(hx0 + (hx1 - hx0) * tz);
	}


	float TerrainHeightField::SampleWorldHeight(float worldX, float worldZ) const
	{
		const float n = SampleNormalizedHeight(worldX, worldZ);
		return m_CI.HeightOffset + n * m_CI.HeightScale;
	}
}
