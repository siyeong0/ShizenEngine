#pragma once
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

#include "Engine/AssetManager/Public/AssetRef.hpp"
#include "Engine/AssetManager/Public/AssetPtr.hpp"
#include "Engine/AssetManager/Public/AssetManager.h"

#include "Engine/RuntimeData/Public/Texture.h"
#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/RuntimeData/Public/Material.h"

namespace shz
{
	class TerrainSystem final
	{
	public:
		// CreateInfo
		struct CreateInfo final
		{
			std::string HeightMapPath = {};
			std::string DiffusePath = {}; // optional

			float WorldSpacingX = 1.0f;
			float WorldSpacingZ = 1.0f;

			float HeightScale = 100.0f;
			float HeightOffset = 0.0f;

			bool bCenterXZ = true;

			// When reading from source height texture:
			// - use only R channel
			// - if format is RGB/RGBA, read first component as grayscale
			bool bReadRChannelOnly = true;
		};

		// Mesh build settings
		struct MeshBuildSettings final
		{
			bool bGenerateNormals = true;
			bool bGenerateTexCoords = true;
			bool bPreferU16Indices = true;

			bool bFlipWinding = false;

			// Normal computation tuning (same intent as your old builder)
			float NormalUpBias = 2.0f;
		};

	public:
		TerrainSystem() = default;
		~TerrainSystem() = default;

		TerrainSystem(const TerrainSystem&) = delete;
		TerrainSystem& operator=(const TerrainSystem&) = delete;

		void Initialize(AssetManager& assetManager, const CreateInfo& ci);
		void Cleanup();

		bool IsValid() const noexcept { return m_bValid; }

		// Basic info
		uint32 GetWidth()  const noexcept { return m_Width; }
		uint32 GetHeight() const noexcept { return m_Height; }

		float GetWorldSpacingX() const noexcept { return m_WorldSpacingX; }
		float GetWorldSpacingZ() const noexcept { return m_WorldSpacingZ; }

		float GetHeightScale()  const noexcept { return m_HeightScale; }
		float GetHeightOffset() const noexcept { return m_HeightOffset; }

		float GetWorldSizeX() const noexcept { return (m_Width > 0 ? float(m_Width - 1) : 0.0f) * m_WorldSpacingX; }
		float GetWorldSizeZ() const noexcept { return (m_Height > 0 ? float(m_Height - 1) : 0.0f) * m_WorldSpacingZ; }

		float GetWorldOriginX() const noexcept;
		float GetWorldOriginZ() const noexcept;

		// CPU height data
		const std::vector<uint16>& GetHeightU16() const noexcept { return m_HeightU16; }

		float GetNormalizedHeightAt(uint32 x, uint32 z) const;
		float GetWorldHeightAt(uint32 x, uint32 z) const;

		// Bilinear
		float SampleNormalizedHeight(float worldX, float worldZ) const;
		float SampleWorldHeight(float worldX, float worldZ) const;

		// Assets
		const AssetRef<Texture>& GetHeightTextureRef() const noexcept { return m_HeightTexRef; }
		const AssetRef<Texture>& GetDiffuseTextureRef() const noexcept { return m_DiffuseTexRef; }

		const Texture* GetHeightTexture() const noexcept { return m_HeightTex.Get(); }
		const Texture* GetDiffuseTexture() const noexcept { return m_DiffuseTex.Get(); }

		// Mesh build
		bool BuildStaticMesh(
			StaticMesh* pOutMesh,
			MaterialId terrainMaterial,
			const MeshBuildSettings& settings) const;

		// ------------------------------------------------------------
		// Physics helper
		// Convert stored u16 normalized into world-meter float samples
		// (same as your old conversion block)
		// ------------------------------------------------------------
		void BuildPhysicsHeightSamples(std::vector<float>& outHeightsWorldMeters) const;

	private:
		uint32 getIndex(uint32 x, uint32 z) const noexcept { return z * m_Width + x; }
		void buildHeightU16FromHeightTexture(const Texture& heightTex);
		float3 computeNormalCentralDiff(uint32 x, uint32 z, const MeshBuildSettings& s) const;

	private:
		CreateInfo m_CI = {};

		bool  m_bValid = false;

		uint32 m_Width = 0;
		uint32 m_Height = 0;

		float m_WorldSpacingX = 1.0f;
		float m_WorldSpacingZ = 1.0f;

		float m_HeightScale = 100.0f;
		float m_HeightOffset = 0.0f;

		bool  m_bCenterXZ = true;

		AssetRef<Texture> m_HeightTexRef = {};
		AssetRef<Texture> m_DiffuseTexRef = {};

		AssetPtr<Texture> m_HeightTex;
		AssetPtr<Texture> m_DiffuseTex;

		// CPU height samples as u16 normalized
		std::vector<uint16> m_HeightU16 = {};
	};
} // namespace shz
