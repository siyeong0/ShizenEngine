#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

#include "Engine/Core/Runtime/Public/SampleBase.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/AssetRuntime/AssetManager/Public/AssetManager.h"
#include "Engine/AssetRuntime/Common/AssetRef.hpp"
#include "Engine/AssetRuntime/Common/AssetPtr.hpp"

#include "Engine/Framework/Public/FirstPersonCamera.h"

#include "Engine/Material/Public/MaterialInstance.h"
#include "Engine/Material/Public/MaterialTemplate.h"

namespace shz
{
	class MaterialEditor final : public SampleBase
	{
	public:
		virtual void Initialize(const SampleInitInfo& InitInfo) override final;

		virtual void Render() override final;
		virtual void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;

		virtual void ReleaseSwapChainBuffers() override final;
		virtual void WindowResize(uint32 Width, uint32 Height) override final;

		virtual const Char* GetSampleName() const override final { return "MaterialEditor"; }

	protected:
		void UpdateUI() override;

	private:
		struct LoadedMesh final
		{
			std::string Path = {};

			AssetID MeshID = {};
			AssetRef<StaticMeshAsset> MeshRef = {};
			AssetPtr<StaticMeshAsset> MeshPtr = {}; // keeps resident

			Handle<StaticMeshRenderData> MeshHandle = {};
			Handle<RenderScene::RenderObject> ObjectId = {};

			int32 SceneObjectIndex = -1;

			float3 Position = { 0, 0, 0 };
			float3 BaseRotation = { 0, 0, 0 };
			float3 Scale = { 1, 1, 1 };
		};

		struct ViewportState final
		{
			uint32 Width = 0;
			uint32 Height = 0;
			bool Hovered = false;
			bool Focused = false;
		};

		struct MaterialUiCache final
		{
			// Keyed by shader variable name (reflection name)
			std::unordered_map<std::string, std::vector<uint8>> ValueBytes = {};
			std::unordered_map<std::string, std::string> TexturePaths = {};

			// Pipeline knobs (stored as UI-state, applied directly to MaterialInstance when changed)
			std::string RenderPassName = "GBuffer";
			uint32 SubpassIndex = 0;

			CULL_MODE CullMode = CULL_MODE_BACK;
			bool FrontCounterClockwise = true;

			bool DepthEnable = true;
			bool DepthWriteEnable = true;
			COMPARISON_FUNCTION DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

			MATERIAL_TEXTURE_BINDING_MODE TextureBindingMode = MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC;

			std::string LinearWrapSamplerName = "g_LinearWrapSampler";
			SamplerDesc LinearWrapSamplerDesc =
			{
				FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
			};

			bool Dirty = true; // first-time apply / template changed
		};

	private:
		// Asset helpers
		void registerAssetLoaders();

		AssetRef<StaticMeshAsset> registerStaticMeshPath(const std::string& path);
		AssetRef<TextureAsset>    registerTexturePath(const std::string& path);

		AssetPtr<StaticMeshAsset> loadStaticMeshBlocking(AssetRef<StaticMeshAsset> ref, EAssetLoadFlags flags = EAssetLoadFlags::None);
		AssetPtr<TextureAsset>    loadTextureBlocking(AssetRef<TextureAsset> ref, EAssetLoadFlags flags = EAssetLoadFlags::None);

		// Template / Instance
		std::string makeTemplateKeyFromInputs() const;
		MaterialTemplate* getOrCreateTemplateFromInputs();

		// Scene helpers
		void loadPreviewMesh(const char* path, float3 position, float3 rotation, float3 scale, bool bUniformScale = true);
		std::vector<MaterialInstance> buildMaterialsForCpuMeshSlots(const StaticMeshAsset& cpuMesh);

		// UI framework
		void uiDockspace();
		void uiScenePanel();
		void uiViewportPanel();
		void uiMaterialInspector();
		void uiStatsPanel();

		// Material UI (reflection-driven)
		MaterialInstance* getSelectedMaterialOrNull();
		MaterialUiCache& getOrCreateMaterialCache(uint64 key);
		uint64 makeSelectionKey(int32 objIndex, int32 slotIndex) const;

		void syncCacheFromTemplateDefaults(MaterialUiCache& cache, const MaterialTemplate& tmpl);
		void applyCacheToInstance(MaterialInstance& inst, MaterialUiCache& cache);

		void drawValueEditor(MaterialInstance& inst, MaterialUiCache& cache, const MaterialTemplate& tmpl);
		void drawResourceEditor(MaterialInstance& inst, MaterialUiCache& cache, const MaterialTemplate& tmpl);
		void drawPipelineEditor(MaterialInstance& inst, MaterialUiCache& cache);

		// Utility
		static std::string sanitizeFilePath(std::string s);

	private:
		std::unique_ptr<Renderer> m_pRenderer = nullptr;
		std::unique_ptr<RenderScene> m_pRenderScene = nullptr;
		std::unique_ptr<AssetManager> m_pAssetManager = nullptr;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		ViewFamily m_ViewFamily = {};
		FirstPersonCamera m_Camera;

		RenderScene::LightObject m_GlobalLight = {};
		Handle<RenderScene::LightObject> m_GlobalLightHandle = {};

		std::unordered_map<std::string, MaterialTemplate> m_TemplateCache = {};
		std::vector<LoadedMesh> m_Loaded = {};

		int32 m_SelectedObject = -1;
		int32 m_SelectedMaterialSlot = 0;

		ViewportState m_Viewport = {};

		// Shader inputs
		int32 m_SelectedPresetIndex = 0;
		std::string m_ShaderVS = "GBuffer.vsh";
		std::string m_ShaderPS = "GBuffer.psh";
		std::string m_VSEntry = "main";
		std::string m_PSEntry = "main";

		// Material UI caches (per selection)
		std::unordered_map<uint64, MaterialUiCache> m_MaterialUi = {};

		// Frame
		bool m_DockBuilt = false;
	};
} // namespace shz
