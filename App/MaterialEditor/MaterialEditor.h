#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include "Engine/Core/Runtime/Public/SampleBase.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/AssetManager/Public/AssetRef.hpp"
#include "Engine/AssetManager/Public/AssetTypeTraits.h"

#include "Engine/Framework/Public/FirstPersonCamera.h"

#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/RuntimeData/Public/Texture.h"

#include "Engine/Material/Public/MaterialTemplate.h"
#include "Engine/RuntimeData/Public/Material.h"

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
		// ------------------------------------------------------------
		// UI structs
		// ------------------------------------------------------------
		struct ViewportPanelState final
		{
			uint32 Width = 1;
			uint32 Height = 1;
			bool Hovered = false;
			bool Focused = false;
		};

		struct MainObjectState final
		{
			std::string Path = {};
			float3 Position = {};
			float3 Rotation = {};
			float3 Scale = { 1,1,1 };

			// Object-only option (Material has no CastShadow)
			bool bCastShadow = true;

			// ------------------------------------------------------------
			// Source 1) Native mesh (shzmesh.json)
			// ------------------------------------------------------------
			AssetRef<StaticMesh> MeshRef = {};
			AssetPtr<StaticMesh> MeshPtr = {};

			// ------------------------------------------------------------
			// Source 2) Imported mesh (fbx/gltf/...)
			// ------------------------------------------------------------
			AssetRef<AssimpAsset> AssimpRef = {};
			AssetPtr<AssimpAsset> AssimpPtr = {};

			// CPU mesh pointer for current main object
			StaticMesh* ImportedCpuMesh = nullptr; // owns CPU mesh when imported

			// GPU + Scene
			StaticMeshRenderData MeshRD = {};
			Handle<RenderScene::RenderObject> ObjectId = {};

			uint64 RebuildKey = 1;
		};

		// UI-only scratch state (NOT a runtime material cache).
		// Used because Material does not expose getters for arbitrary param blobs.
		struct SlotUiState final
		{
			bool Dirty = true;

			// Pending template change (Material needs recreation)
			std::string PendingTemplateName = {};
			int TemplateComboIndex = -1;

			// Reflection-driven param bytes (for UI display/edit)
			std::unordered_map<std::string, std::vector<uint8>> ValueBytes = {};

			// Resource edit strings (paths) + sampler override flags
			std::unordered_map<std::string, std::string> TexturePaths = {};
			std::unordered_map<std::string, bool> bHasSamplerOverride = {};
			std::unordered_map<std::string, SamplerDesc> SamplerOverrideDesc = {};
		};

	private:
		bool LoadOrReplaceMainObject(
			const char* Path,
			float3 Position,
			float3 Rotation,
			float3 Scale,
			bool bCastShadow);

		bool RebuildMainMeshRenderData(); // CreateStaticMesh(key++) and patch scene object

		RenderScene::RenderObject* GetMainRenderObjectOrNull();

		SlotUiState& GetOrCreateSlotUi(uint32 SlotIndex);
		void SyncSlotUiFromMaterial(SlotUiState& Ui, const Material& Mat);
		void ApplySlotUiToMaterial(Material& Mat, SlotUiState& Ui, bool bRebuildMeshRD);

		bool RecreateMaterialWithTemplate(
			Material* pOutNewMat,
			const Material& OldMat,
			const std::string& NewTemplateName);

		bool RebuildMainSaveObjectFromScene(std::string* outError);
		bool SaveMainObject(const std::string& outPath, EAssetSaveFlags flags, std::string* outError);

		void MarkAllSlotUiDirty();

	private:
		// ------------------------------------------------------------
		// UI panels
		// ------------------------------------------------------------
		void UiDockspace();
		void UiScenePanel();
		void UiViewportPanel();
		void UiMaterialPanel();
		void UiStatsPanel();

	private:
		// ------------------------------------------------------------
		// State
		// ------------------------------------------------------------
		std::unique_ptr<Renderer>     m_pRenderer = nullptr;
		std::unique_ptr<RenderScene>  m_pRenderScene = nullptr;
		std::unique_ptr<AssetManager> m_pAssetManager = nullptr;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		ViewportPanelState m_Viewport = {};
		ViewFamily        m_ViewFamily = {};
		FirstPersonCamera m_Camera = {};

		// Light (optional)
		RenderScene::LightObject         m_GlobalLight = {};
		Handle<RenderScene::LightObject> m_GlobalLightHandle = {};

		// Main object
		MainObjectState m_Main = {};
		std::string m_MainMeshPath = "C:/Dev/ShizenEngine/Assets/Basic/DamagedHelmet/DamagedHelmet.gltf";

		std::unique_ptr<AssetObject> m_pMainBuiltObjForSave = nullptr;
		std::string m_MainMeshSavePath = "C:/Dev/ShizenEngine/Assets/Exported/Main.shzmesh.json";

		// Floor mesh
		Handle<RenderScene::RenderObject> m_Floor = {};
		std::string m_FloorMeshPath = "C:/Dev/ShizenEngine/Assets/Basic/floor/FbxFloor.fbx";

		// Material selection
		int32 m_SelectedSlot = 0;

		bool m_bUniformScale = true;

		// UI-only per slot state
		std::unordered_map<uint32, SlotUiState> m_SlotUi = {};

		// Dock
		bool m_DockBuilt = false;
	};
} // namespace shz
