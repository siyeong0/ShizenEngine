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

#include "Engine/Renderer/Public/Renderer.h"

namespace shz
{
	class RenderScene;
	struct View;

	class TerrainSystem final
	{
	public:
		struct CreateInfo final
		{
			// Heightfield & masks (domain textures)
			std::string HeightPath = {};
			std::string DiffusePath = {};
			std::string NormalPath = {};
			std::string SlopePath = {};
			std::string FlowPath = {};
			std::string RockyPath = {};
			std::string SoilPath = {};
			std::string VegetationPath = {};

			// PBR layer folders (Soil/Rocky)
			std::string SoilMaterialPath = {};
			std::string RockyMaterialPath = {};

			// Geometry controls (SQUARE CHUNKS)
			float ChunkSize = 64.0f;
			float CellSize = 1.0f;

			// Heightfield mapping
			float WorldSpacing = 1.0f;

			float HeightScale = 100.0f;
			float HeightOffset = 0.0f;

			bool bCenterXZ = true;
			bool bReadRChannelOnly = true;
		};

	public:
		TerrainSystem() = default;
		~TerrainSystem() = default;

		TerrainSystem(const TerrainSystem&) = delete;
		TerrainSystem& operator=(const TerrainSystem&) = delete;

		void Initialize(Renderer& renderer, AssetManager& assetManager, const CreateInfo& ci);
		void Cleanup();

		void Update(Renderer& renderer, RenderScene* pScene, const View& view);

		// --------------------------------------------------------------------
		// Basic info
		// --------------------------------------------------------------------
		uint32 GetWidth()  const noexcept { return m_Width; }
		uint32 GetHeight() const noexcept { return m_Height; }

		// Grid resolution in quads per side (LOD0)
		uint32 GetChunkGridRes() const noexcept { return m_ChunkGridRes; }

		float GetChunkSize() const noexcept { return m_ChunkSize; }
		float GetCellSize()  const noexcept { return m_CellSize; }
		float GetWorldSpacing() const noexcept { return m_WorldSpacing; }

		float GetHeightScale()  const noexcept { return m_HeightScale; }
		float GetHeightOffset() const noexcept { return m_HeightOffset; }

		float GetWorldSizeX() const noexcept { return float(m_Width - 1) * m_WorldSpacing; }
		float GetWorldSizeZ() const noexcept { return float(m_Height - 1) * m_WorldSpacing; }

		float GetWorldOriginX() const noexcept;
		float GetWorldOriginZ() const noexcept;

		bool IsCenterXZ() const noexcept { return m_bCenterXZ; }

		float2 WorldXZToDomainUV(const float2& worldXZ) const noexcept;
		float2 DomainUVToWorldXZ(const float2& uv) const noexcept;

		// --------------------------------------------------------------------
		// CPU height data
		// --------------------------------------------------------------------
		const std::vector<uint16>& GetHeightU16() const noexcept { return m_HeightU16; }

		float GetNormalizedHeightAt(uint32 x, uint32 z) const;
		float GetWorldHeightAt(uint32 x, uint32 z) const;

		float SampleNormalizedHeight(float worldX, float worldZ) const;
		float SampleWorldHeight(float worldX, float worldZ) const;

		// --------------------------------------------------------------------
		// Assets (CPU)
		// --------------------------------------------------------------------
		const AssetRef<Texture>& GetHeightTextureRef() const noexcept { return m_HeightTexRef; }
		const AssetRef<Texture>& GetDiffuseTextureRef() const noexcept { return m_DiffuseTexRef; }

		const Texture* GetHeightTexture() const noexcept { return m_HeightTex.Get(); }

		// --------------------------------------------------------------------
		// Physics
		// --------------------------------------------------------------------
		void BuildPhysicsHeightSamples(std::vector<float>& outHeightsWorldMeters) const;

	private:
		void buildHeightU16FromHeightTexture(const Texture& heightTex);

		void buildGridVertices(uint32 chunkGridRes, std::vector<struct TerrainVertex>& outVerts) const;
		void buildGridIndices(
			uint32 chunkGridRes,
			uint32 step,
			uint8  stitchMask,
			std::vector<uint16>& outIdxU16) const;

		bool uploadTextureAssetWithMips(
			Renderer& renderer,
			AssetManager& assetManager,
			const char* resourceName,
			const AssetRef<Texture>& texRef,
			const char* shaderStaticName /* "g_TerrainDiffuseTex" etc */);

	private:
		CreateInfo m_CI = {};

		uint32 m_Width = 0;
		uint32 m_Height = 0;

		// --------------------------------------------------------------------
		// Geometry controls
		// --------------------------------------------------------------------
		float  m_ChunkSize = 64.0f;
		float  m_CellSize = 1.0f;
		uint32 m_ChunkGridRes = 64; // quads per side (LOD0), derived

		// --------------------------------------------------------------------
		// Heightfield mapping
		// --------------------------------------------------------------------
		float m_WorldSpacing = 1.0f;

		float m_HeightScale = 100.0f;
		float m_HeightOffset = 0.0f;

		bool m_bCenterXZ = true;

		// --------------------------------------------------------------------
		// Asset references
		// --------------------------------------------------------------------
		AssetRef<Texture> m_HeightTexRef = {};
		AssetRef<Texture> m_DiffuseTexRef = {};
		AssetRef<Texture> m_NormalTexRef = {};
		AssetRef<Texture> m_SlopeTexRef = {};
		AssetRef<Texture> m_FlowTexRef = {};
		AssetRef<Texture> m_RockyTexRef = {};
		AssetRef<Texture> m_SoilTexRef = {};
		AssetRef<Texture> m_VegetationTexRef = {};

		AssetPtr<Texture> m_HeightTex;
		std::vector<uint16> m_HeightU16 = {};

		// --------------------------------------------------------------------
		// GPU mesh (grid VB + stitched IBs)
		// --------------------------------------------------------------------
		static constexpr uint32 MAX_TERRAIN_LODS = 12;
		static constexpr uint32 NUM_STITCH_MASKS = 16;

		uint32 m_NumLods = 0; // = Log2(ChunkGridRes) + 1

		RefCntAutoPtr<IBuffer> m_pGridVB;
		RefCntAutoPtr<IBuffer> m_pLodIB[MAX_TERRAIN_LODS][NUM_STITCH_MASKS];
		uint32 m_LodIndexCount[MAX_TERRAIN_LODS][NUM_STITCH_MASKS] = {};

		// --------------------------------------------------------------------
		// Material
		// --------------------------------------------------------------------
		MaterialId m_TerrainMaterialId = 0;

		std::string m_SoilMaterialPath = {};
		std::string m_RockyMaterialPath = {};

		std::string m_TerrainVS = "Terrain.vsh";
		std::string m_TerrainPS = "Terrain.psh";

		std::vector<Handle<RenderScene::TerrainObject>> m_SceneHandles;
	};
} // namespace shz