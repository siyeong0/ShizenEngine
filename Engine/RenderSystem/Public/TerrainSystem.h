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
            std::string TreesPath = {};

            // PBR layer folders (Soil/Rocky)
            std::string SoilMaterialPath = {};
            std::string RockyMaterialPath = {};

            // ----------------------------
            // Geometry controls
            // ----------------------------
            // Chunk grid resolution in QUADS per side (LOD0)
            // - must be power-of-two (e.g. 16, 32, 64...)
            // - vertex grid becomes (QuadsPerSide + 1)^2
            uint32 ChunkQuadsPerSide = 64;

            // Quad size in world meters (LOD0)
            // ChunkSizeXZ = QuadSizeXZ * ChunkQuadsPerSide
            float QuadSizeX = 1.0f;
            float QuadSizeZ = 1.0f;

            // ----------------------------
            // Heightfield mapping
            // ----------------------------
            // World spacing between height texels (meters per texel in X/Z).
            // Usually equals QuadSizeX/Z when you want vertex sampling to align with texels.
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

        void Initialize(Renderer& renderer, AssetManager& assetManager, const CreateInfo& ci);
        void Cleanup();

        // Per-frame update: frustum cull + (Add/Remove TerrainObject in RenderScene)
        void Update(Renderer& renderer, RenderScene* pScene, const View& view);

        // Basic info
        uint32 GetWidth()  const noexcept { return m_Width; }
        uint32 GetHeight() const noexcept { return m_Height; }

        float GetWorldSpacingX() const noexcept { return m_WorldSpacingX; }
        float GetWorldSpacingZ() const noexcept { return m_WorldSpacingZ; }

        uint32 GetChunkQuadsPerSide() const noexcept { return m_ChunkQuadsPerSide; }

        float GetQuadSizeX() const noexcept { return m_QuadSizeX; }
        float GetQuadSizeZ() const noexcept { return m_QuadSizeZ; }

        float GetChunkSizeX() const noexcept { return m_ChunkSizeXZ.x; }
        float GetChunkSizeZ() const noexcept { return m_ChunkSizeXZ.y; }

        float2 GetChunkSizeXZ() const noexcept { return m_ChunkSizeXZ; }

        float GetHeightScale()  const noexcept { return m_HeightScale; }
        float GetHeightOffset() const noexcept { return m_HeightOffset; }

        float GetWorldSizeX() const noexcept { return float(m_Width - 1) * m_WorldSpacingX; }
        float GetWorldSizeZ() const noexcept { return float(m_Height - 1) * m_WorldSpacingZ; }

        float GetWorldOriginX() const noexcept;
        float GetWorldOriginZ() const noexcept;

        bool GetCenterXZ() const noexcept { return m_bCenterXZ; }

        float2 WorldXZToDomainUV(const float2& worldXZ) const noexcept;

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

        // Derived
        static bool IsPowerOfTwoU32(uint32 v) noexcept { return v != 0 && (v & (v - 1)) == 0; }
        static uint32 Log2U32(uint32 v) noexcept;

    private:
        CreateInfo m_CI = {};

        uint32 m_Width = 0;
        uint32 m_Height = 0;

        // Geometry controls
        uint32 m_ChunkQuadsPerSide = 16;
        float  m_QuadSizeX = 1.0f;
        float  m_QuadSizeZ = 1.0f;
        float2 m_ChunkSizeXZ = float2{ 16.0f, 16.0f };

        // Heightfield mapping
        float m_WorldSpacingX = 1.0f;
        float m_WorldSpacingZ = 1.0f;

        float m_HeightScale = 100.0f;
        float m_HeightOffset = 0.0f;

        bool m_bCenterXZ = true;

        AssetRef<Texture> m_HeightTexRef = {};
        AssetRef<Texture> m_DiffuseTexRef = {};
        AssetRef<Texture> m_NormalTexRef = {};
        AssetRef<Texture> m_SlopeTexRef = {};
        AssetRef<Texture> m_FlowTexRef = {};
        AssetRef<Texture> m_RockyTexRef = {};
        AssetRef<Texture> m_SoilTexRef = {};
        AssetRef<Texture> m_VegetationTexRef = {};
        AssetRef<Texture> m_TreesTexRef = {};

        AssetPtr<Texture> m_HeightTex;
        std::vector<uint16> m_HeightU16 = {};

        // ----------------------------
        // GPU mesh (grid + stitched IBs)
        // ----------------------------
        static constexpr uint32 kMaxTerrainLods = 12; // supports up to 2^11=2048 quads (way more than you¡¯ll use)
        static constexpr uint32 kNumStitchMasks = 16;

        uint32 m_NumLods = 0; // = log2(ChunkQuadsPerSide)+1

        RefCntAutoPtr<IBuffer> m_pGridVB;
        RefCntAutoPtr<IBuffer> m_pLodIB[kMaxTerrainLods][kNumStitchMasks];
        uint32 m_LodIndexCount[kMaxTerrainLods][kNumStitchMasks] = {};

        // Material
        MaterialId m_TerrainMaterialId = 0;

        std::string m_SoilMaterialPath = {};
        std::string m_RockyMaterialPath = {};

        // shader paths
        std::string m_TerrainVS = "Terrain.vsh";
        std::string m_TerrainPS = "Terrain.psh";

        std::vector<Handle<RenderScene::TerrainObject>> m_SceneHandles;
    };
} // namespace shz