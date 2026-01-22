// ============================================================================
// MaterialEditor.h
// ============================================================================

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
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
		void Initialize(const SampleInitInfo& InitInfo) override final;

		void Render() override final;
		void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;

		void ReleaseSwapChainBuffers() override final;
		void WindowResize(uint32 Width, uint32 Height) override final;

		const Char* GetSampleName() const override final { return "MaterialEditor"; }

	protected:
		void UpdateUI() override final;

	private:
		enum class EPreviewObject : uint8
		{
			Floor = 0,
			Main,
		};

		struct LoadedMesh final
		{
			std::string Path = {};

			AssetRef<StaticMeshAsset> MeshRef = {};
			AssetPtr<StaticMeshAsset> MeshPtr = {}; // keeps resident

			Handle<StaticMeshRenderData>      MeshHandle = {};
			Handle<RenderScene::RenderObject> ObjectId = {}; // authoritative handle

			// Fallback only (avoid depending on array index!)
			int32 SceneObjectIndex = -1;

			float3 Position = { 0, 0, 0 };
			float3 BaseRotation = { 0, 0, 0 };
			float3 Scale = { 1, 1, 1 };

			bool bCastShadow = true;
			bool bAlphaMasked = false;

			bool IsValid() const noexcept { return ObjectId.IsValid(); }
		};

		struct ViewportState final
		{
			uint32 Width = 1;
			uint32 Height = 1;
			bool Hovered = false;
			bool Focused = false;
		};

		struct MaterialUiCache final
		{
			// Reflection-driven storage (Template variable name -> raw bytes / texture path).
			std::unordered_map<std::string, std::vector<uint8>> ValueBytes = {};
			std::unordered_map<std::string, std::string>        TexturePaths = {};

			// Pipeline knobs
			std::string RenderPassName = "GBuffer";
			uint32      SubpassIndex = 0;

			CULL_MODE CullMode = CULL_MODE_BACK;
			bool      FrontCounterClockwise = true;

			bool                DepthEnable = true;
			bool                DepthWriteEnable = true;
			COMPARISON_FUNCTION DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

			MATERIAL_TEXTURE_BINDING_MODE TextureBindingMode = MATERIAL_TEXTURE_BINDING_MODE_MUTABLE;

			std::string LinearWrapSamplerName = "g_LinearWrapSampler";
			SamplerDesc LinearWrapSamplerDesc =
			{
				FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
			};

			bool Dirty = true;
		};

	private:
		// Asset helpers
		void RegisterAssetLoaders();

		AssetRef<StaticMeshAsset> RegisterStaticMeshPath(const std::string& Path);
		AssetRef<TextureAsset>    RegisterTexturePath(const std::string& Path);

		AssetPtr<StaticMeshAsset> LoadStaticMeshBlocking(AssetRef<StaticMeshAsset> Ref, EAssetLoadFlags Flags = EAssetLoadFlags::None);
		AssetPtr<TextureAsset>    LoadTextureBlocking(AssetRef<TextureAsset> Ref, EAssetLoadFlags Flags = EAssetLoadFlags::None);

		// Template / instance
		std::string      MakeTemplateKeyFromInputs() const;
		MaterialTemplate* GetOrCreateTemplateFromInputs();
		MaterialTemplate* RebuildTemplateFromInputs();
		void             RebindMainMaterialToTemplate(MaterialTemplate* pNewTmpl);

		// Scene helpers
		bool LoadPreviewMesh(
			EPreviewObject Which,
			const char* Path,
			float3 Position,
			float3 Rotation,
			float3 Scale,
			bool bUniformScale,
			bool bCastShadow,
			bool bAlphaMasked);

		bool ImportMainFromSavedPath(const std::string& savedPath);
		std::vector<MaterialInstance> BuildMaterialsForCpuMeshSlots(const StaticMeshAsset& CpuMesh);

		LoadedMesh* GetMainMeshOrNull() noexcept;
		const LoadedMesh* GetMainMeshOrNull() const noexcept;

		RenderScene::RenderObject* GetMainRenderObjectOrNull();
		const RenderScene::RenderObject* GetMainRenderObjectOrNull() const;

		MaterialInstance* GetMainMaterialOrNull();

		// UI framework
		void UiDockspace();
		void UiScenePanel();
		void UiViewportPanel();
		void UiMaterialInspector();
		void UiStatsPanel();

		// Material UI
		uint64          MakeSelectionKeyForMain(int32 SlotIndex) const;
		MaterialUiCache& GetOrCreateMaterialCache(uint64 Key);

		void SyncCacheFromInstance(MaterialUiCache& Cache, const MaterialInstance& Inst, const MaterialTemplate& Tmpl);
		void ApplyCacheToInstance(MaterialInstance& Inst, MaterialUiCache& Cache, const MaterialTemplate& Tmpl);

		void DrawValueEditor(MaterialInstance& Inst, MaterialUiCache& Cache, const MaterialTemplate& Tmpl);
		void DrawResourceEditor(MaterialInstance& Inst, MaterialUiCache& Cache, const MaterialTemplate& Tmpl);
		void DrawPipelineEditor(MaterialInstance& Inst, MaterialUiCache& Cache);

		bool RebuildMainSaveObjectFromScene(std::string* outError);
		bool SaveMainObject(const std::string& outPath, EAssetSaveFlags flags, std::string* outError);
		// Utility
		static std::string SanitizeFilePath(std::string S);

	private:
		std::unique_ptr<Renderer>     m_pRenderer = nullptr;
		std::unique_ptr<RenderScene>  m_pRenderScene = nullptr;
		std::unique_ptr<AssetManager> m_pAssetManager = nullptr;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		ViewFamily         m_ViewFamily = {};
		FirstPersonCamera  m_Camera = {};

		RenderScene::LightObject      m_GlobalLight = {};
		Handle<RenderScene::LightObject> m_GlobalLightHandle = {};

		std::unordered_map<std::string, MaterialTemplate> m_TemplateCache = {};

		LoadedMesh m_Floor = {};
		LoadedMesh m_Main = {};

		int32 m_SelectedMaterialSlot = 0;
		ViewportState m_Viewport = {};

		// Shader inputs (Template selection)
		int32       m_SelectedPresetIndex = 0;
		std::string m_ShaderVS = "GBuffer.vsh";
		std::string m_ShaderPS = "GBuffer.psh";
		std::string m_VSEntry = "main";
		std::string m_PSEntry = "main";

		// Material UI caches (per MAIN slot)
		std::unordered_map<uint64, MaterialUiCache> m_MaterialUi = {};

		// UI state
		bool m_DockBuilt = false;

		// Main object settings
		std::string m_MainMeshPath = {};
		float3 m_MainMeshPos = { 0.0f, -0.5f, 3.0f };
		float3 m_MainMeshRot = { 0.0f, 0.0f, 0.0f };
		float3 m_MainMeshScale = { 1.0f, 1.0f, 1.0f };

		bool m_MainMeshUniformScale = true;
		bool m_MainMeshCastShadow = true;
		bool m_MainMeshAlphaMasked = false;

		// Main save UI
		std::string m_MainMeshSavePath = "C:/Dev/ShizenEngine/Assets/Exported/Main.shzmesh.json";
		AssimpImportSettings m_MainImportSettings = {};

		std::unique_ptr<AssetObject> m_pMainBuiltObjForSave = nullptr;
	};
} // namespace shz
