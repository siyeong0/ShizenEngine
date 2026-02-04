#pragma once
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/AssetManager/Public/AssetRef.hpp"
#include "Engine/AssetManager/Public/AssetPtr.hpp"
#include "Engine/AssetManager/Public/AssetManager.h"

#include "Engine/RuntimeData/Public/Texture.h"

#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;

	class TerrainSystem final
	{
	public:
		struct CreateInfo final
		{
			std::string HeightMapPath = {};
			std::string DiffusePath = {}; // optional

			float ChunkSize = 64.0f;
			float WorldSpacingX = 1.0f;
			float WorldSpacingZ = 1.0f;
			float HeightScale = 100.0f;
			float HeightOffset = 0.0f;

			bool bCenterXZ = true;
			bool bReadRChannelOnly = true;
		};

		struct MeshBuildSettings final
		{
			// legacy CPU mesh path (kept for physics sampling etc.)
			bool  bGenerateNormals = true;
			bool  bGenerateTexCoords = true;
			bool  bPreferU16Indices = true;
			bool  bFlipWinding = false;
			float NormalUpBias = 2.0f;
		};

	public:
		TerrainSystem() = default;
		~TerrainSystem() = default;

		TerrainSystem(const TerrainSystem&) = delete;
		TerrainSystem& operator=(const TerrainSystem&) = delete;

		void Initialize(AssetManager& assetManager, const CreateInfo& ci);
		void Cleanup();

		// Pass registration (feature-owned)
		void InstallPasses(Renderer& renderer);

		// Basic info
		uint32 GetWidth()  const noexcept { return m_Width; }
		uint32 GetHeight() const noexcept { return m_Height; }

		float GetWorldSpacingX() const noexcept { return m_WorldSpacingX; }
		float GetWorldSpacingZ() const noexcept { return m_WorldSpacingZ; }

		float GetHeightScale()  const noexcept { return m_HeightScale; }
		float GetHeightOffset() const noexcept { return m_HeightOffset; }

		float GetWorldSizeX() const noexcept { return float(m_Width - 1) * m_WorldSpacingX; }
		float GetWorldSizeZ() const noexcept { return float(m_Height - 1) * m_WorldSpacingZ; }

		float GetWorldOriginX() const noexcept;
		float GetWorldOriginZ() const noexcept;

		// CPU height data
		const std::vector<uint16>& GetHeightU16() const noexcept { return m_HeightU16; }

		float GetNormalizedHeightAt(uint32 x, uint32 z) const;
		float GetWorldHeightAt(uint32 x, uint32 z) const;

		float SampleNormalizedHeight(float worldX, float worldZ) const;
		float SampleWorldHeight(float worldX, float worldZ) const;

		// Assets (CPU)
		const AssetRef<Texture>& GetHeightTextureRef() const noexcept { return m_HeightTexRef; }
		const AssetRef<Texture>& GetDiffuseTextureRef() const noexcept { return m_DiffuseTexRef; }

		const Texture* GetHeightTexture() const noexcept { return m_HeightTex.Get(); }

		// Physics
		void BuildPhysicsHeightSamples(std::vector<float>& outHeightsWorldMeters) const;

	private:
		void buildHeightU16FromHeightTexture(const Texture& heightTex);

	private:
		CreateInfo m_CI = {};

		uint32 m_Width = 0;
		uint32 m_Height = 0;

		float m_ChunkSize = 64.0f;
		float m_WorldSpacingX = 1.0f;
		float m_WorldSpacingZ = 1.0f;

		float m_HeightScale = 100.0f;
		float m_HeightOffset = 0.0f;

		bool m_bCenterXZ = true;

		AssetRef<Texture> m_HeightTexRef = {};
		AssetRef<Texture> m_DiffuseTexRef = {};

		AssetPtr<Texture> m_HeightTex;

		std::vector<uint16> m_HeightU16 = {};

		// ------------------------------------------------------------
		// GPU render state (feature-owned)
		// ------------------------------------------------------------
		RefCntAutoPtr<IPipelineState>         m_pTerrainGBufferPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pTerrainGBufferSRB;

		RefCntAutoPtr<IBuffer> m_pGridVB;

		// NOTE:
		// - IB is selected by (LOD, StitchMask). Mask is 4-bit: L/R/B/T.
		// - Neighbor LOD diff is clamped to <= 1 step for stitch simplicity.
		RefCntAutoPtr<IBuffer> m_pLodIB[5][16];
		uint32 m_LodIndexCount[5][16] = {};

		// shader paths
		std::string m_TerrainVS = "Terrain.vsh";
		std::string m_TerrainPS = "GBuffer.psh"; // reuse

	};
} // namespace shz
