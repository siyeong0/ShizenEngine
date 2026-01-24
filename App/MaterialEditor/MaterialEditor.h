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
#include "Engine/AssetRuntime/Common/AssetTypeTraits.h"

#include "Engine/Framework/Public/FirstPersonCamera.h"

#include "Engine/AssetRuntime/AssetData/Public/StaticMeshAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/TextureAsset.h"

#include "Engine/Material/Public/MaterialTemplate.h"
#include "Engine/AssetRuntime/AssetData/Public/MaterialAsset.h"

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

			bool bCastShadow = true;

			// ------------------------------------------------------------
			// Source 1) Native mesh asset (shzmesh.json)
			// ------------------------------------------------------------
			AssetRef<StaticMeshAsset> MeshRef = {};
			AssetPtr<StaticMeshAsset> MeshPtr = {};

			// ------------------------------------------------------------
			// Source 2) Imported mesh via Assimp (fbx/gltf/glb/...)
			// ------------------------------------------------------------
			AssetRef<AssimpAsset> AssimpRef = {};
			AssetPtr<AssimpAsset> AssimpPtr = {};

			// CPU
			StaticMeshAsset* ImportedCpuMesh = {}; // owns CPU mesh after BuildStaticMeshAsset()

			// GPU
			StaticMeshRenderData MeshRD = {};
			Handle<RenderScene::RenderObject> ObjectId = {};

			uint64 RebuildKey = 1;
		};

		struct MaterialUiCache final
		{
			bool Dirty = true;

			// Metadata
			std::string TemplateName = "DefaultLit";
			std::string RenderPassName = "GBuffer";

			// Options
			MATERIAL_BLEND_MODE BlendMode = MATERIAL_BLEND_MODE_OPAQUE;

			CULL_MODE CullMode = CULL_MODE_BACK;
			bool FrontCounterClockwise = true;

			bool DepthEnable = true;
			bool DepthWriteEnable = true;
			COMPARISON_FUNCTION DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

			MATERIAL_TEXTURE_BINDING_MODE TextureBindingMode = MATERIAL_TEXTURE_BINDING_MODE_MUTABLE;

			std::string LinearWrapSamplerName = "g_LinearWrapSampler";
			SamplerDesc LinearWrapSamplerDesc =
			{
				FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
			};

			// Asset-only options
			bool bTwoSided = false;
			bool bCastShadow = true;

			// Reflection-driven payload
			std::unordered_map<std::string, std::vector<uint8>> ValueBytes = {};
			std::unordered_map<std::string, std::string> TexturePaths = {};
		};

	private:
		bool loadOrReplaceMainObject(
			const char* Path,
			float3 Position,
			float3 Rotation,
			float3 Scale,
			bool bCastShadow);

		bool rebuildMainMeshRenderData(); // CreateStaticMesh(key++) and patch scene object

		RenderScene::RenderObject* getMainRenderObjectOrNull();

		MaterialUiCache& getOrCreateSlotCache(uint32 slotIndex);
		void syncCacheFromMaterialAsset(MaterialUiCache& cache, const MaterialAsset& mat, const MaterialTemplate& tmpl);
		void applyCacheToMaterialAsset(MaterialAsset& mat, const MaterialUiCache& cache, const MaterialTemplate& tmpl);

		bool rebuildMainSaveObjectFromScene(std::string* outError);
		bool saveMainObject(const std::string& outPath, EAssetSaveFlags flags, std::string* outError);

		void clearTemplateCache();

	private:
		// ------------------------------------------------------------
		// UI panels
		// ------------------------------------------------------------
		void uiDockspace();
		void uiScenePanel();
		void uiViewportPanel();
		void uiMaterialPanel();
		void uiStatsPanel();

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

		bool  m_bFitToUnitCube = true;
		float m_FitUnitCubeSize = 1.0f; 

		// Slot UI cache
		std::unordered_map<uint32, MaterialUiCache> m_SlotUi = {};

		// Dock
		bool m_DockBuilt = false;
	};
} // namespace shz
