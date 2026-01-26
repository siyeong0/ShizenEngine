#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "Primitives/BasicTypes.h"

namespace shz
{
	struct TerrainHeightFieldCreateInfo final
	{
		uint32 Width = 0;
		uint32 Height = 0;

		float WorldSpacingX = 1.f;
		float WorldSpacingZ = 1.f;

		float HeightScale = 1000.f;
		float HeightOffset = 0.f;

		constexpr TerrainHeightFieldCreateInfo() = default;

		constexpr TerrainHeightFieldCreateInfo(
			uint32 inWidth,
			uint32 inHeight,
			float inWorldSpacingX = 1.f,
			float inWorldSpacingZ = 1.f,
			float inHeightScale = 100.f,
			float inHeightOffset = 0.f)
			: Width(inWidth)
			, Height(inHeight)
			, WorldSpacingX(inWorldSpacingX)
			, WorldSpacingZ(inWorldSpacingZ)
			, HeightScale(inHeightScale)
			, HeightOffset(inHeightOffset)
		{
		}
	};

	class TerrainHeightField final
	{
	public:
		TerrainHeightField() = default;
		explicit TerrainHeightField(const TerrainHeightFieldCreateInfo& ci) { Initialize(ci); }

		TerrainHeightField(const TerrainHeightField&) = default;
		TerrainHeightField& operator=(const TerrainHeightField&) = default;
		TerrainHeightField(TerrainHeightField&&) noexcept = default;
		TerrainHeightField& operator=(TerrainHeightField&&) noexcept = default;
		~TerrainHeightField() { Cleanup(); }

		void Initialize(const TerrainHeightFieldCreateInfo& ci);
		void Cleanup();

		// Basic info
		bool IsValid() const { return (m_CI.Width > 0) && (m_CI.Height > 0) && !m_DataU16.empty(); }
		uint32 GetWidth() const { return m_CI.Width; }
		uint32 GetHeight() const { return m_CI.Height; }

		float GetWorldSpacingX() const { return m_CI.WorldSpacingX; }
		float GetWorldSpacingZ() const { return m_CI.WorldSpacingZ; }

		float GetHeightScale() const { return m_CI.HeightScale; }
		float GetHeightOffset() const { return m_CI.HeightOffset; }

		// Raw storage (0..65535). Useful for uploading to R16_UNORM texture.
		const std::vector<uint16>& GetDataU16() const { return m_DataU16; }

		// 0..1 API
		float GetNormalizedHeightAt(uint32 x, uint32 z) const;
		float GetWorldHeightAt(uint32 x, uint32 z) const;

		void SetNormalizedHeightAt(uint32 x, uint32 z, float normalizedHeight);

		float SampleNormalizedHeight(float worldX, float worldZ) const;
		float SampleWorldHeight(float worldX, float worldZ) const;

		float GetWorldSizeX() const { return (m_CI.Width > 1) ? (static_cast<float>(m_CI.Width - 1) * m_CI.WorldSpacingX) : 0.f; }
		float GetWorldSizeZ() const { return (m_CI.Height > 1) ? (static_cast<float>(m_CI.Height - 1) * m_CI.WorldSpacingZ) : 0.f; }

	private:
		uint32 getIndex(uint32 x, uint32 z) const { return z * m_CI.Width + x; }

		static float u16ToNormalized(uint16 v);
		static uint16 normalizedToU16(float n);

	private:
		TerrainHeightFieldCreateInfo m_CI = {};

		std::vector<uint16> m_DataU16 = {};
	};
}
