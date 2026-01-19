// ============================================================================
// Samples/MaterialEditor/MaterialEditor.h
//   - Workflow:
//     1) Pick shader preset OR type shader paths (VS/PS + Entry)
//     2) Build MaterialTemplate (shaders + reflection) and cache it
//     3) Create MaterialInstance per mesh material slot
//     4) Set RenderPass / Raster / Depth / Binding policy via UI (Set* APIs)
//     5) Fill instance values/textures via UI (Set* APIs)
//   - A-option (Recommended):
//     - Template cache is global, but APPLY is explicit:
//       * Build/Update Template: builds template only (no auto apply)
//       * Apply Template to Selected Slot/Object/All: applies only where requested
// ============================================================================

#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

#include "Engine/Core/Runtime/Public/SampleBase.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/AssetRuntime/Public/AssetManager.h"
#include "Engine/AssetRuntime/Public/AssetRef.hpp"
#include "Engine/AssetRuntime/Public/AssetPtr.hpp"

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

		struct DefaultMaterialTextures final
		{
			RefCntAutoPtr<ITexture> Black;
			RefCntAutoPtr<ITexture> White;
			RefCntAutoPtr<ITexture> Normal;
			RefCntAutoPtr<ITexture> MetallicRoughness;
			RefCntAutoPtr<ITexture> AO;
			RefCntAutoPtr<ITexture> Emissive;
		};

		struct EditorMaterialOverrides final
		{
			// RenderPass / pipeline knobs (per-selected material)
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

			// Textures (runtime override paths)
			std::string BaseColorPath = {};
			std::string NormalPath = {};
			std::string MetallicRoughnessPath = {};
			std::string AOPath = {};
			std::string EmissivePath = {};
			std::string HeightPath = {};

			// Factors
			float4 BaseColor = { 1, 1, 1, 1 };
			float  Roughness = 1.0f;
			float  Metallic = 0.0f;
			float  Occlusion = 1.0f;

			float3 EmissiveColor = { 0, 0, 0 };
			float  EmissiveIntensity = 0.0f;

			float  AlphaCutoff = 0.5f;
			float  NormalScale = 1.0f;

			// Per-texture vertical flip (for imported textures / UV conventions)
			bool BaseColorFlipVertically = false;
			bool NormalFlipVertically = false;
			bool MetallicRoughnessFlipVertically = false;
			bool AOFlipVertically = false;
			bool EmissiveFlipVertically = false;
			bool HeightFlipVertically = false;


			// Flags (mirrors shader flags if used)
			uint32 MaterialFlags = 0;
		};

	private:
		// Asset helpers
		void registerAssetLoaders();
		AssetID makeAssetIDFromPath(AssetTypeID typeId, const std::string& path) const;

		AssetRef<StaticMeshAsset> registerStaticMeshPath(const std::string& path);
		AssetRef<TextureAsset>    registerTexturePath(const std::string& path);

		AssetPtr<StaticMeshAsset> loadStaticMeshBlocking(AssetRef<StaticMeshAsset> ref, EAssetLoadFlags flags = EAssetLoadFlags::None);
		AssetPtr<TextureAsset>    loadTextureBlocking(AssetRef<TextureAsset> ref, EAssetLoadFlags flags = EAssetLoadFlags::None);

		// Path / runtime texture cache
		static std::string sanitizeFilePath(std::string s);
		ITextureView* getOrCreateTextureSRV(const std::string& path, bool isSRGB, bool flipVertically);
		void ensureResourceStateSRV(ITexture* pTex);

		// Win32 helpers (optional)
		bool openFileDialog(std::string& outPath, const char* filter, const char* title) const;
		void enableWindowDragDrop();  // WM_DROPFILES hookup point (if available)
		void onFilesDropped(const std::vector<std::string>& paths);

		// Template / Instance workflow (inputs -> template)
		std::string makeTemplateKeyFromInputs() const;
		MaterialTemplate* getOrCreateTemplateFromInputs();

		// Explicit apply (A-option)
		bool rebuildAllMaterialsFromCurrentTemplate();
		bool rebuildSelectedMaterialSlotFromCurrentTemplate();
		bool rebuildSelectedObjectFromCurrentTemplate();

		// Preview scene
		void loadPreviewMesh(const char* path, float3 position, float3 rotation, float3 scale, bool bUniformScale = true);

		// Material build/apply
		std::vector<MaterialInstance> buildMaterialsForCpuMeshSlots(const StaticMeshAsset& cpuMesh);

		void seedFromImportedAsset(MaterialInstance& mat, const MaterialInstanceAsset& matAsset);
		void applyPipelineOverrides(MaterialInstance& mat, const EditorMaterialOverrides& ov);
		void applyMaterialOverrides(MaterialInstance& mat, const EditorMaterialOverrides& ov);

		// UI helpers
		void drawTextureSlotUI(const char* label, std::string& path, const char* dialogFilter, const char* dialogTitle);

	private:
		std::unique_ptr<Renderer> m_pRenderer = nullptr;
		std::unique_ptr<RenderScene> m_pRenderScene = nullptr;
		std::unique_ptr<AssetManager> m_pAssetManager = nullptr;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		ViewFamily m_ViewFamily = {};
		FirstPersonCamera m_Camera;

		RenderScene::LightObject m_GlobalLight = {};
		Handle<RenderScene::LightObject> m_GlobalLightHandle = {};

		DefaultMaterialTextures m_DefaultTextures = {};

		std::unordered_map<std::string, RefCntAutoPtr<ITexture>> m_RuntimeTextureCache = {};
		std::unordered_map<std::string, MaterialTemplate> m_TemplateCache = {};

		std::vector<LoadedMesh> m_Loaded = {};

		int32 m_SelectedObject = -1;
		int32 m_SelectedMaterialSlot = 0;

		EditorMaterialOverrides m_Overrides = {};

		// Shader inputs (no ShaderPreset struct)
		int32 m_SelectedPresetIndex = 0;

		std::string m_ShaderVS = "GBuffer.vsh";
		std::string m_ShaderPS = "GBuffer.psh";
		std::string m_VSEntry = "main";
		std::string m_PSEntry = "main";

		// Drag&drop support
		std::vector<std::string> m_DroppedFiles = {};
	};
} // namespace shz
