// ============================================================================
// GrassViewer.h
// ============================================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include "Engine/Core/Runtime/Public/SampleBase.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/AssetRuntime/AssetManager/Public/AssetManager.h"
#include "Engine/AssetRuntime/Common/AssetRef.hpp"
#include "Engine/AssetRuntime/Common/AssetPtr.hpp"
#include "Engine/AssetRuntime/Common/AssetTypeTraits.h"

#include "Engine/Framework/Public/FirstPersonCamera.h"

#include "Engine/Material/Public/MaterialTemplate.h"
#include "Engine/Material/Public/MaterialInstance.h"

namespace shz
{
	class GrassViewer final : public SampleBase
	{
	public:
		void Initialize(const SampleInitInfo& InitInfo) override final;

		void Render() override final;
		void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;

		void ReleaseSwapChainBuffers() override final;
		void WindowResize(uint32 Width, uint32 Height) override final;

		const Char* GetSampleName() const override final { return "GrassViewer"; }

	protected:
		void UpdateUI() override final;

	private:
		struct ViewportState final
		{
			uint32 Width = 1;
			uint32 Height = 1;
		};

		struct LoadedStaticMesh final
		{
			std::string Path = {};

			AssetRef<StaticMeshAsset> MeshRef = {};
			AssetPtr<StaticMeshAsset> MeshPtr = {}; // keep resident

			Handle<StaticMeshRenderData> MeshHandle = {};

			Handle<RenderScene::RenderObject> ObjectId = {};
			int32 SceneObjectIndex = -1; // fallback

			bool bCastShadow = true;
			bool bAlphaMasked = false;

			bool IsValid() const noexcept { return ObjectId.IsValid(); }
		};

	private:
		// Templates
		bool buildInitialTemplateCache();
		MaterialTemplate* getOrCreateTemplateByKey(const std::string& TemplateKey);
		MaterialTemplate* getFallbackTemplate(bool bAlphaMasked);

		// Scene building (hard-coded)
		bool loadStaticMeshObject(
			LoadedStaticMesh& InOut,
			const char* Path,
			float3 Position,
			float3 Rotation,
			float3 Scale,
			bool bCastShadow,
			bool bAlphaMasked);

		std::vector<MaterialInstance> buildMaterialsForCpuMeshSlots(const StaticMeshAsset& CpuMesh);

	private:
		std::unique_ptr<Renderer>     m_pRenderer = nullptr;
		std::unique_ptr<RenderScene>  m_pRenderScene = nullptr;
		std::unique_ptr<AssetManager> m_pAssetManager = nullptr;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		ViewportState      m_Viewport = {};
		ViewFamily         m_ViewFamily = {};
		FirstPersonCamera  m_Camera = {};

		// Global light (UI editable)
		RenderScene::LightObject         m_GlobalLight = {};
		Handle<RenderScene::LightObject> m_GlobalLightHandle = {};

		// Fixed templates + cache map
		MaterialTemplate m_TmplGBuffer = {};
		MaterialTemplate m_TmplGBufferMasked = {};
		bool m_TemplatesReady = false;

		// IMPORTANT: TemplateKey -> Template
		std::unordered_map<std::string, MaterialTemplate*> m_pTemplateCache = {};

		// Hard-coded scene objects
		LoadedStaticMesh m_Floor = {};
		std::vector<LoadedStaticMesh> m_Grasses = {};
	};
} // namespace shz
