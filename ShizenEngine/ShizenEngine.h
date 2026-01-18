#pragma once

#include <vector>
#include <string>
#include <unordered_map>

#include "Engine/Core/Runtime/Public/SampleBase.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/AssetRuntime/Public/AssetManagerImpl.h"
#include "Engine/AssetRuntime/Public/AssetRef.hpp"
#include "Engine/AssetRuntime/Public/AssetPtr.hpp"

#include "FirstPersonCamera.h"

namespace shz
{
	class ShizenEngine final : public SampleBase
	{
	public:
		virtual void Initialize(const SampleInitInfo& InitInfo) override final;

		virtual void Render() override final;
		virtual void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;

		virtual const Char* GetSampleName() const override final { return "Shizen Engine"; }

	protected:
		void UpdateUI() override;

	private:
		struct LoadedMesh final
		{
			std::string Path = {};

			AssetID MeshID = {};
			AssetRef<StaticMeshAsset> MeshRef = {};
			AssetPtr<StaticMeshAsset> MeshPtr = {}; // keeps resident (strong ref)

			Handle<StaticMeshRenderData> MeshHandle = {};
			Handle<RenderScene::RenderObject> ObjectId = {};

			float3 Position = { 0, 0, 0 };
			float3 BaseRotation = { 0, 0, 0 };
			float3 Scale = { 1, 1, 1 };

			uint32 RotateAxis = 1;     // 0:X, 1:Y, 2:Z
			float  RotateSpeed = 1.0f; // rad/sec
		};

	private:
		MaterialInstance CreateMaterialInstanceFromAsset(const MaterialInstanceAsset& matInstanceAsset);

		void spawnMeshesOnXYGrid(
			const std::vector<const char*>& meshPaths,
			float3 gridCenter,
			float spacingX,
			float spacingY,
			float spacingZ);

	private:
		// ------------------------------------------------------------
		// Asset helpers (new asset manager integration)
		// ------------------------------------------------------------
		void registerAssetLoaders();
		AssetID makeAssetIDFromPath(AssetTypeID typeId, const std::string& path) const;

		AssetRef<StaticMeshAsset> registerStaticMeshPath(const std::string& path);
		AssetRef<TextureAsset>    registerTexturePath(const std::string& path);

		AssetPtr<StaticMeshAsset> loadStaticMeshBlocking(AssetRef<StaticMeshAsset> ref, EAssetLoadFlags flags = EAssetLoadFlags::None);
		AssetPtr<TextureAsset>    loadTextureBlocking(AssetRef<TextureAsset> ref, EAssetLoadFlags flags = EAssetLoadFlags::None);

		ITextureView* getOrCreateTextureSRV(const std::string& path);

		void ensureResourceStateSRV(ITexture* pTex);

	private:
		std::unique_ptr<Renderer> m_pRenderer = nullptr;
		std::unique_ptr<RenderScene> m_pRenderScene = nullptr;

		// New asset system
		std::unique_ptr<AssetManagerImpl> m_pAssetManager = nullptr;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		ViewFamily m_ViewFamily = {};
		FirstPersonCamera m_Camera;

		// Debug cube (renderer-owned)
		Handle<StaticMeshRenderData> m_CubeHandle = {};
		Handle<StaticMeshRenderData> m_FloorHandle = {};

		// Loaded meshes (1 object per path)
		std::vector<LoadedMesh> m_Loaded = {};

		RenderScene::LightObject m_GlobalLight = {};
		Handle<RenderScene::LightObject> m_GlobalLightHandle;

		MaterialTemplate m_PBRMaterialTemplate = {};

		struct DefaultMaterialTextures
		{
			RefCntAutoPtr<ITexture> Black;
			RefCntAutoPtr<ITexture> White;
			RefCntAutoPtr<ITexture> Normal;
			RefCntAutoPtr<ITexture> MetallicRoughness;
			RefCntAutoPtr<ITexture> AO;
			RefCntAutoPtr<ITexture> Emissive;
		};

		DefaultMaterialTextures m_DefaultTextures;

		// Runtime GPU texture cache for material bindings (path -> texture)
		std::unordered_map<std::string, RefCntAutoPtr<ITexture>> m_RuntimeTextureCache = {};
	};
} // namespace shz
