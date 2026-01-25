#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "Primitives/BasicTypes.h"

namespace shz
{

	enum HEIGHT_FIELD_SAMPLE_FORMAT : uint8
	{
		HEIGHT_FIELD_SAMPLE_FORMAT_UNKNOWN = 0,
		HEIGHT_FIELD_SAMPLE_FORMAT_UINT8 = 1,
		HEIGHT_FIELD_SAMPLE_FORMAT_UINT16 = 2,
		HEIGHT_FIELD_SAMPLE_FORMAT_FLOAT32 = 3,
	};

	struct TerrainHeightFieldCreateInfo final
	{
		HEIGHT_FIELD_SAMPLE_FORMAT SampleFormat = HEIGHT_FIELD_SAMPLE_FORMAT_UNKNOWN;

		uint32 Width = 0;
		uint32 Height = 0;

		float WorldSpacingX = 1.f;
		float WorldSpacingZ = 1.f;

		float HeightScale = 100.f;
		float HeightOffset = 0.f;

		// Optional: 원본 파일 경로들(디버그/리임포트/툴링용)
		std::string SourceHeightMapPath = {};
		std::string SourceColorMapPath = {};

		constexpr TerrainHeightFieldCreateInfo() = default;

		constexpr TerrainHeightFieldCreateInfo(
			uint32 inWidth,
			uint32 inHeight,
			HEIGHT_FIELD_SAMPLE_FORMAT  inSampleFormat,
			float inWorldSpacingX = 1.f,
			float inWorldSpacingZ = 1.f,
			float inHeightScale = 100.f,
			float inHeightOffset = 0.f)
			: SampleFormat(inSampleFormat)
			, Width(inWidth)
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
		bool IsValid() const { return (m_CI.Width > 0) && (m_CI.Height > 0) && !m_Data.empty(); }
		uint32 GetWidth() const { return m_CI.Width; }
		uint32 GetHeight() const { return m_CI.Height; }

		HEIGHT_FIELD_SAMPLE_FORMAT GetSampleFormat() const { return m_CI.SampleFormat; }

		float GetWorldSpacingX() const { return m_CI.WorldSpacingX; }
		float GetWorldSpacingZ() const { return m_CI.WorldSpacingZ; }

		float GetHeightScale() const { return m_CI.HeightScale; }
		float GetHeightOffset() const { return m_CI.HeightOffset; }

		const std::string& GetSourceHeightMapPath() const { return m_CI.SourceHeightMapPath; }
		const std::string& GetSourceColorMapPath()  const { return m_CI.SourceColorMapPath; }

		const std::vector<float>& GetData() const { return m_Data; }

		float GetNormalizedHeightAt(uint32 x, uint32 z) const;
		float GetWorldHeightAt(uint32 x, uint32 z) const;

		void SetNormalizedHeightAt(uint32 x, uint32 z, float normalizedHeight);

		float SampleNormalizedHeight(float worldX, float worldZ) const;
		float SampleWorldHeight(float worldX, float worldZ) const;

		float GetWorldSizeX() const { return (m_CI.Width > 1) ? (static_cast<float>(m_CI.Width - 1) * m_CI.WorldSpacingX) : 0.f; }
		float GetWorldSizeZ() const { return (m_CI.Height > 1) ? (static_cast<float>(m_CI.Height - 1) * m_CI.WorldSpacingZ) : 0.f; }

	private:
		uint32 getIndex(uint32 x, uint32 z) const { return z * m_CI.Width + x; }

	private:
		TerrainHeightFieldCreateInfo m_CI = {};

		std::vector<float> m_Data = {};
	};
}
