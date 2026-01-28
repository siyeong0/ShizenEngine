#include "MaterialEditor.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/RuntimeData/Public/StaticMeshImporter.h"
#include "Engine/RuntimeData/Public/TextureImporter.h"
#include "Engine/RuntimeData/Public/MaterialImporter.h"
#include "Engine/AssetManager/Public/AssimpImporter.h"

#include "Engine/RuntimeData/Public/StaticMeshExporter.h"
#include "Engine/RuntimeData/Public/MaterialExporter.h"

namespace shz
{
	namespace
	{
#include "Shaders/HLSL_Structures.hlsli"

		static constexpr const char* kShaderRoot = "C:/Dev/ShizenEngine/Shaders";

		static float ComputeUniformScale(const Box& bounds)
		{
			float3 size = bounds.Size();
			float maxSize = std::max({ size.x, size.y, size.z });
			return (maxSize > 0.0f) ? (1.0f / maxSize) : 1.0f;
		}

		static int InputTextCallback_Resize(ImGuiInputTextCallbackData* data)
		{
			if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
			{
				auto* str = reinterpret_cast<std::string*>(data->UserData);
				str->resize((size_t)data->BufTextLen);
				data->Buf = str->data();
			}
			return 0;
		}

		static bool InputTextStdString(const char* label, std::string& str)
		{
			if (str.capacity() < 128)
				str.reserve(128);

			return ImGui::InputText(
				label,
				str.data(),
				str.capacity() + 1,
				ImGuiInputTextFlags_CallbackResize,
				InputTextCallback_Resize,
				(void*)&str);
		}

		static std::string SanitizeFilePath(std::string s)
		{
			if (s.empty())
				return s;

			for (char& c : s)
			{
				if (c == '\\')
					c = '/';
			}

			if (!s.empty() && (s.front() == '\"' || s.front() == '\''))
				s.erase(s.begin());

			if (!s.empty() && (s.back() == '\"' || s.back() == '\''))
				s.pop_back();

			return s;
		}

		static bool IsColorNameLike(const std::string& name)
		{
			return (name.find("Color") != std::string::npos) ||
				(name.find("Albedo") != std::string::npos) ||
				(name.find("BaseColor") != std::string::npos);
		}

		static inline std::string ToLowerCopy(std::string s)
		{
			for (char& c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		static inline bool EndsWithNoCase(const std::string& s, const char* suffix)
		{
			if (!suffix) return false;
			std::string ss = ToLowerCopy(s);
			std::string suf = ToLowerCopy(suffix);
			if (ss.size() < suf.size()) return false;
			return ss.compare(ss.size() - suf.size(), suf.size(), suf) == 0;
		}

		static inline bool IsShzMeshJsonPath(const std::string& path)
		{
			return EndsWithNoCase(path, ".shzmesh.json");
		}

		static const char* FindMaterialFlagsParamName(const shz::MaterialTemplate& tmpl)
		{
			if (tmpl.FindValueParam("MaterialFlags"))    return "MaterialFlags";
			if (tmpl.FindValueParam("g_MaterialFlags"))  return "g_MaterialFlags";
			if (tmpl.FindValueParam("MAT_FLAGS"))        return "MAT_FLAGS";
			return nullptr;
		}

		static bool HasTexturePath(const std::unordered_map<std::string, std::string>& texPaths, const char* name)
		{
			auto it = texPaths.find(name);
			if (it == texPaths.end())
				return false;

			std::string p = it->second;
			p = SanitizeFilePath(p);
			return !p.empty();
		}

		static const char* BlendModeLabel(MATERIAL_BLEND_MODE m)
		{
			switch (m)
			{
			case MATERIAL_BLEND_MODE_OPAQUE:        return "OPAQUE";
			case MATERIAL_BLEND_MODE_MASKED:        return "MASKED";
			case MATERIAL_BLEND_MODE_TRANSLUCENT:   return "TRANSLUCENT";
			case MATERIAL_BLEND_MODE_ADDITIVE:      return "ADDITIVE";
			case MATERIAL_BLEND_MODE_PREMULTIPLIED: return "PREMULTIPLIED";
			default:                                return "UNKNOWN";
			}
		}
	} // namespace

	SampleBase* CreateSample()
	{
		return new MaterialEditor();
	}

	// ------------------------------------------------------------
	// Utilities
	// ------------------------------------------------------------

	void MaterialEditor::MarkAllSlotUiDirty()
	{
		for (auto& kv : m_SlotUi)
			kv.second.Dirty = true;
	}

	// ------------------------------------------------------------
	// Main object access
	// ------------------------------------------------------------

	RenderScene::RenderObject* MaterialEditor::GetMainRenderObjectOrNull()
	{
		if (!m_pRenderScene)
			return nullptr;

		if (!m_Main.ObjectId.IsValid())
			return nullptr;

		// NOTE:
		// Replace with your actual scene API.
		return m_pRenderScene->GetObjectOrNull(m_Main.ObjectId);
	}

	// ------------------------------------------------------------
	// Load / rebuild flow
	// ------------------------------------------------------------

	bool MaterialEditor::LoadOrReplaceMainObject(
		const char* path,
		float3 position,
		float3 rotation,
		float3 scale,
		bool bCastShadow)
	{
		ASSERT(path && path[0] != '\0', "Invalid mesh path.");
		ASSERT(m_pAssetManager, "AssetManager is null.");
		ASSERT(m_pRenderScene, "RenderScene is null.");
		ASSERT(m_pRenderer, "Renderer is null.");

		// Remove old object
		if (m_Main.ObjectId.IsValid())
		{
			m_pRenderScene->RemoveObject(m_Main.ObjectId);
			m_Main.ObjectId = {};
		}

		// Reset state
		m_SelectedSlot = 0;

		// NOTE: if ImportedCpuMesh is owned (import path), release it.
		// We keep behavior same as your previous code: delete when imported.
		if (m_Main.AssimpPtr && m_Main.ImportedCpuMesh)
		{
			delete m_Main.ImportedCpuMesh;
			m_Main.ImportedCpuMesh = nullptr;
		}

		m_Main = {};
		m_Main.Path = path;
		m_Main.Position = position;
		m_Main.Rotation = rotation;
		m_Main.Scale = scale;
		m_Main.bCastShadow = bCastShadow;

		StaticMesh* cpu = nullptr;

		// 1) Native mesh: *.shzmesh.json
		if (IsShzMeshJsonPath(m_Main.Path))
		{
			m_Main.MeshRef = m_pAssetManager->RegisterAsset<StaticMesh>(m_Main.Path);
			m_Main.MeshPtr = m_pAssetManager->LoadBlocking(m_Main.MeshRef);

			cpu = m_Main.MeshPtr.Get();
			m_Main.ImportedCpuMesh = cpu;

			if (!cpu)
				return false;
		}
		// 2) Imported mesh: fbx/gltf/...
		else
		{
			m_Main.AssimpRef = m_pAssetManager->RegisterAsset<AssimpAsset>(m_Main.Path);
			m_Main.AssimpPtr = m_pAssetManager->LoadBlocking(m_Main.AssimpRef);

			const AssimpAsset* assimp = m_Main.AssimpPtr.Get();
			if (!assimp)
				return false;

			m_Main.ImportedCpuMesh = new StaticMesh();
			ASSERT(m_Main.ImportedCpuMesh, "ImportedCpuMesh alloc failed.");

			AssimpImportSettings settings = {};

			std::string err;
			if (!BuildStaticMeshAsset(
				*assimp,
				m_Main.ImportedCpuMesh,
				settings,
				&err,
				m_pAssetManager.get()))
			{
				ASSERT(false, err.c_str());
				return false;
			}

			cpu = m_Main.ImportedCpuMesh;
		}

		ASSERT(cpu, "CPU mesh is null.");

		// Build GPU render data
		m_Main.MeshRD = m_pRenderer->CreateStaticMesh(*cpu, m_Main.RebuildKey++, "MaterialEditor Main Mesh");

		if (m_bUniformScale)
		{
			float uniformScale = ComputeUniformScale(cpu->GetBounds());
			m_Main.Scale = float3{ uniformScale, uniformScale, uniformScale };
		}

		m_Main.ObjectId = m_pRenderScene->AddObject(
			m_Main.MeshRD, 
			Matrix4x4::TRS(m_Main.Position, m_Main.Rotation, m_Main.Scale), 
			m_Main.bCastShadow);
		ASSERT(m_Main.ObjectId.IsValid(), "Failed to add RenderObject.");

		// UI state should be refreshed
		m_SlotUi.clear();

		return true;
	}

	bool MaterialEditor::RebuildMainMeshRenderData()
	{
		ASSERT(m_pRenderer, "Renderer is null.");
		ASSERT(m_pRenderScene, "RenderScene is null.");

		StaticMesh* cpu = m_Main.ImportedCpuMesh;
		if (!cpu)
			return false;

		RenderScene::RenderObject* obj = GetMainRenderObjectOrNull();
		if (!obj)
			return false;

		m_Main.MeshRD = m_pRenderer->CreateStaticMesh(*cpu, m_Main.RebuildKey++, "MaterialEditor Main Mesh (Rebuild)");
		obj->Mesh = m_Main.MeshRD;

		return true;
	}

	// ------------------------------------------------------------
	// Slot UI state
	// ------------------------------------------------------------

	MaterialEditor::SlotUiState& MaterialEditor::GetOrCreateSlotUi(uint32 slotIndex)
	{
		auto it = m_SlotUi.find(slotIndex);
		if (it != m_SlotUi.end())
			return it->second;

		SlotUiState ui = {};
		ui.Dirty = true;
		auto [insIt, _] = m_SlotUi.emplace(slotIndex, std::move(ui));
		return insIt->second;
	}

	void MaterialEditor::SyncSlotUiFromMaterial(SlotUiState& ui, const Material& mat)
	{
		// This is UI-only snapshot for displaying values/resources.
		// Material itself is the source of truth.
		ui.PendingTemplateName = mat.GetTemplateName();

		std::vector<MaterialSerializedValue> values;
		std::vector<MaterialSerializedResource> resources;
		mat.BuildSerializedSnapshot(&values, &resources);

		ui.ValueBytes.clear();
		for (const auto& v : values)
		{
			if (v.Name.empty())
				continue;
			ui.ValueBytes[v.Name] = v.Data;
		}

		ui.TexturePaths.clear();
		ui.bHasSamplerOverride.clear();
		ui.SamplerOverrideDesc.clear();

		for (const auto& r : resources)
		{
			if (r.Name.empty())
				continue;

			// NOTE: if AssetRef<T> has no path getter, adapt this line.
			std::string path = {};
			if (r.TextureRef.IsValid())
				path = r.TextureRef.GetSourcePath();

			ui.TexturePaths[r.Name] = SanitizeFilePath(path);
			ui.bHasSamplerOverride[r.Name] = r.bHasSamplerOverride;
			ui.SamplerOverrideDesc[r.Name] = r.SamplerOverrideDesc;
		}

		ui.PendingTemplateName = mat.GetTemplateName();

		ui.TemplateComboIndex = -1;
		if (m_pRenderer)
		{
			const auto names = m_pRenderer->GetAllMaterialTemplateNames();
			for (size_t i = 0; i < names.size(); ++i)
			{
				if (names[i] == ui.PendingTemplateName)
				{
					ui.TemplateComboIndex = (int)i;
					break;
				}
			}
		}

		ui.Dirty = false;
	}

	// Recreate a new Material from OldMat + NewTemplateName, copying serializable payload.
	bool MaterialEditor::RecreateMaterialWithTemplate(
		Material* pOutNewMat,
		const Material& oldMat,
		const std::string& newTemplateName)
	{
		ASSERT(pOutNewMat, "Out material is null.");
		if (newTemplateName.empty())
			return false;

		// 1) Create new material bound to new template
		Material newMat(oldMat.GetName(), newTemplateName);

		// 2) Copy render pass name and options (these have getters)
		newMat.SetRenderPassName(oldMat.GetRenderPassName());

		newMat.SetBlendMode(oldMat.GetBlendMode());
		newMat.SetCullMode(oldMat.GetCullMode());
		newMat.SetFrontCounterClockwise(oldMat.GetFrontCounterClockwise());

		newMat.SetDepthEnable(oldMat.GetDepthEnable());
		newMat.SetDepthWriteEnable(oldMat.GetDepthWriteEnable());
		newMat.SetDepthFunc(oldMat.GetDepthFunc());

		newMat.SetTextureBindingMode(oldMat.GetTextureBindingMode());
		newMat.SetLinearWrapSamplerName(oldMat.GetLinearWrapSamplerName());
		newMat.SetLinearWrapSamplerDesc(oldMat.GetLinearWrapSamplerDesc());

		// 3) Copy serialized values/resources
		std::vector<MaterialSerializedValue> values;
		std::vector<MaterialSerializedResource> resources;
		oldMat.BuildSerializedSnapshot(&values, &resources);

		for (const auto& v : values)
		{
			if (v.Name.empty())
				continue;

			if (v.Type == MATERIAL_VALUE_TYPE_UNKNOWN)
				continue;

			if (v.Data.empty())
				continue;

			(void)newMat.SetRaw(v.Name.c_str(), v.Type, v.Data.data(), (uint32)v.Data.size());
		}

		for (const auto& r : resources)
		{
			if (r.Name.empty())
				continue;

			if (r.Type == MATERIAL_RESOURCE_TYPE_UNKNOWN)
				continue;

			if (r.TextureRef.IsValid())
			{
				(void)newMat.SetTextureAssetRef(r.Name.c_str(), r.Type, r.TextureRef);
			}

			if (r.bHasSamplerOverride)
			{
				(void)newMat.SetSamplerOverrideDesc(r.Name.c_str(), r.SamplerOverrideDesc);
			}
		}

		std::destroy_at(pOutNewMat);
		std::construct_at(pOutNewMat, std::move(newMat));
		return true;
	}

	// Apply UI edits to Material immediately.
	// If bRebuildMeshRD is true, it rebuilds StaticMeshRenderData to reflect pipeline/layout changes.
	void MaterialEditor::ApplySlotUiToMaterial(Material& mat, SlotUiState& ui, bool bRebuildMeshRD)
	{
		const MaterialTemplate& tmpl = mat.GetTemplate();

		// ------------------------------------------------------------
		// Values
		// ------------------------------------------------------------
		for (uint32 i = 0; i < tmpl.GetValueParamCount(); ++i)
		{
			const MaterialValueParamDesc& desc = tmpl.GetValueParam(i);
			if (desc.Name.empty())
				continue;

			auto it = ui.ValueBytes.find(desc.Name);
			if (it == ui.ValueBytes.end())
				continue;

			const std::vector<uint8>& bytes = it->second;
			if (bytes.empty())
				continue;

			(void)mat.SetRaw(desc.Name.c_str(), desc.Type, bytes.data(), (uint32)bytes.size());
		}

		// ------------------------------------------------------------
		// Resources (textures + sampler override)
		// ------------------------------------------------------------
		ASSERT(m_pAssetManager, "AssetManager is null.");

		for (uint32 i = 0; i < tmpl.GetResourceCount(); ++i)
		{
			const MaterialResourceDesc& res = tmpl.GetResource(i);
			if (res.Name.empty())
				continue;

			// We only edit texture-like resources here.
			if (!IsTextureType(res.Type))
				continue;

			// Path -> AssetRef<Texture>
			std::string path = {};
			{
				auto itPath = ui.TexturePaths.find(res.Name);
				if (itPath != ui.TexturePaths.end())
					path = SanitizeFilePath(itPath->second);
			}

			if (!path.empty())
			{
				const AssetRef<Texture> texRef = m_pAssetManager->RegisterAsset<Texture>(path);
				(void)mat.SetTextureAssetRef(res.Name.c_str(), res.Type, texRef);
			}
			else
			{
				// If you want "clear texture binding" behavior:
				// - Material API currently doesn't show RemoveTextureBinding().
				// - So we leave it as-is when path is empty.
			}

			// Sampler override
			bool bHas = false;
			SamplerDesc sdesc = {};
			{
				auto itHas = ui.bHasSamplerOverride.find(res.Name);
				if (itHas != ui.bHasSamplerOverride.end())
					bHas = itHas->second;

				auto itDesc = ui.SamplerOverrideDesc.find(res.Name);
				if (itDesc != ui.SamplerOverrideDesc.end())
					sdesc = itDesc->second;
			}

			if (bHas)
			{
				(void)mat.SetSamplerOverrideDesc(res.Name.c_str(), sdesc);
			}
			else
			{
				(void)mat.ClearSamplerOverride(res.Name.c_str());
			}
		}

		// ------------------------------------------------------------
		// MaterialFlags example (based on edited texture paths)
		// ------------------------------------------------------------
		{
			uint32 flags = 0;

			if (HasTexturePath(ui.TexturePaths, "g_BaseColorTex"))          flags |= MAT_HAS_BASECOLOR;
			if (HasTexturePath(ui.TexturePaths, "g_NormalTex"))             flags |= MAT_HAS_NORMAL;

			if (HasTexturePath(ui.TexturePaths, "g_MRTex") ||
				HasTexturePath(ui.TexturePaths, "g_MetallicRoughnessTex"))   flags |= MAT_HAS_MR;

			if (HasTexturePath(ui.TexturePaths, "g_AOTex"))                 flags |= MAT_HAS_AO;
			if (HasTexturePath(ui.TexturePaths, "g_EmissiveTex"))           flags |= MAT_HAS_EMISSIVE;
			if (HasTexturePath(ui.TexturePaths, "g_HeightTex"))             flags |= MAT_HAS_HEIGHT;

			if (const char* flagsName = FindMaterialFlagsParamName(tmpl))
			{
				(void)mat.SetUint(flagsName, flags);
			}
		}

		// If pipeline/layout/resources changed, you said you want to rebuild mesh RD.
		if (bRebuildMeshRD)
			(void)RebuildMainMeshRenderData();

		// Refresh UI snapshot
		ui.Dirty = true;
	}

	// ------------------------------------------------------------
	// Save flow
	// ------------------------------------------------------------

	bool MaterialEditor::RebuildMainSaveObjectFromScene(std::string* outError)
	{
		if (outError) outError->clear();

		if (!m_pAssetManager)
		{
			if (outError) *outError = "AssetManager is null.";
			return false;
		}

		if (!m_Main.ImportedCpuMesh)
		{
			if (outError) *outError = "Main CPU mesh is null. Load Main first.";
			return false;
		}

		// Copy CPU mesh as baked output
		StaticMesh baked = *m_Main.ImportedCpuMesh;

		const uint32 slotCount = baked.GetMaterialSlotCount();
		for (uint32 slot = 0; slot < slotCount; ++slot)
		{
			Material& mat = baked.GetMaterialSlot(slot);

			SlotUiState& ui = GetOrCreateSlotUi(slot);
			if (ui.Dirty)
				SyncSlotUiFromMaterial(ui, mat);

			// If pending template differs, recreate IN-PLACE for the baked copy.
			{
				const std::string desiredTmpl = ui.PendingTemplateName.empty()
					? mat.GetTemplateName()
					: ui.PendingTemplateName;

				if (!desiredTmpl.empty() && desiredTmpl != mat.GetTemplateName())
				{
					// IMPORTANT:
					// RecreateMaterialWithTemplate() must snapshot oldMat BEFORE destroying pOutNewMat.
					if (!RecreateMaterialWithTemplate(&mat, mat, desiredTmpl))
					{
						if (outError) *outError = "RecreateMaterialWithTemplate failed (baked copy).";
						return false;
					}

					// New template => UI snapshot must be refreshed for this slot.
					ui.Dirty = true;
					SyncSlotUiFromMaterial(ui, mat);
				}
			}

			// Apply UI bytes/paths to this baked copy.
			ApplySlotUiToMaterial(mat, ui, /*bRebuildMeshRD*/false);

			// Keep UI cache consistent (optional but safe)
			if (ui.Dirty)
				SyncSlotUiFromMaterial(ui, mat);
		}

		m_pMainBuiltObjForSave = std::make_unique<TypedAssetObject<StaticMesh>>(std::move(baked));
		return true;
	}

	bool MaterialEditor::SaveMainObject(const std::string& outPath, EAssetSaveFlags /*flags*/, std::string* outError)
	{
		if (outError) outError->clear();

		if (!m_pAssetManager)
		{
			if (outError) *outError = "AssetManager is null.";
			return false;
		}

		const std::string p = SanitizeFilePath(outPath);
		if (p.empty())
		{
			if (outError) *outError = "Out path is empty.";
			return false;
		}

		// Rebuild save object from current scene/material state
		{
			std::string err;
			if (!RebuildMainSaveObjectFromScene(&err))
			{
				if (outError) *outError = err;
				return false;
			}
		}

		if (!m_pMainBuiltObjForSave)
		{
			if (outError) *outError = "Save object cache is null.";
			return false;
		}

		StaticMeshExporter exporter = {};

		AssetMeta meta = {};
		meta.TypeID = AssetTypeTraits<StaticMesh>::TypeID;
		meta.SourcePath = m_Main.Path.empty() ? m_MainMeshPath : m_Main.Path;

		std::string err;
		const bool ok = exporter(
			*m_pAssetManager,
			meta,
			m_pMainBuiltObjForSave.get(),
			p,
			&err);

		if (!ok)
		{
			if (outError) *outError = err.empty() ? "StaticMeshExporter failed." : err;
			return false;
		}

		return true;
	}

	// ------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------

	void MaterialEditor::Initialize(const SampleInitInfo& initInfo)
	{
		SampleBase::Initialize(initInfo);

		// AssetManager
		m_pAssetManager = std::make_unique<AssetManager>();
		ASSERT(m_pAssetManager, "AssetManager is null.");
		m_pAssetManager->RegisterImporter(AssetTypeTraits<StaticMesh>::TypeID, StaticMeshImporter{});
		m_pAssetManager->RegisterImporter(AssetTypeTraits<Texture>::TypeID, TextureImporter{});
		m_pAssetManager->RegisterImporter(AssetTypeTraits<Material>::TypeID, MaterialImporter{});
		m_pAssetManager->RegisterImporter(AssetTypeTraits<AssimpAsset>::TypeID, AssimpImporter{});

		m_pAssetManager->RegisterExporter(AssetTypeTraits<StaticMesh>::TypeID, StaticMeshExporter{});
		m_pAssetManager->RegisterExporter(AssetTypeTraits<Material>::TypeID, MaterialExporter{});

		// Renderer + shader factory
		m_pRenderer = std::make_unique<Renderer>();
		ASSERT(m_pRenderer, "Renderer is null.");

		ASSERT(m_pEngineFactory, "EngineFactory is null.");
		m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(kShaderRoot, &m_pShaderSourceFactory);
		ASSERT(m_pShaderSourceFactory, "ShaderSourceFactory is null.");

		ASSERT(m_pSwapChain, "SwapChain is null.");
		const auto scDesc = m_pSwapChain->GetDesc();

		RendererCreateInfo rci = {};
		rci.pEngineFactory = m_pEngineFactory;
		rci.pShaderSourceFactory = m_pShaderSourceFactory;
		rci.pDevice = m_pDevice;
		rci.pImmediateContext = m_pImmediateContext;
		rci.pDeferredContexts = m_pDeferredContexts;
		rci.pSwapChain = m_pSwapChain;
		rci.pImGui = m_pImGui;
		rci.BackBufferWidth = std::max(1u, scDesc.Width);
		rci.BackBufferHeight = std::max(1u, scDesc.Height);
		rci.pAssetManager = m_pAssetManager.get();

		rci.EnvTexturePath = "C:/Dev/ShizenEngine/Assets/Cubemap/Sample/SampleEnvHDR.dds";
		rci.DiffuseIrradianceTexPath = "C:/Dev/ShizenEngine/Assets/Cubemap/Sample/SampleDiffuseHDR.dds";
		rci.SpecularIrradianceTexPath = "C:/Dev/ShizenEngine/Assets/Cubemap/Sample/SampleSpecularHDR.dds";
		rci.BrdfLUTTexPath = "C:/Dev/ShizenEngine/Assets/Cubemap/Sample/SampleBrdf.dds";

		(void)m_pRenderer->Initialize(rci);

		// Scene
		m_pRenderScene = std::make_unique<RenderScene>();
		ASSERT(m_pRenderScene, "RenderScene is null.");

		// ViewFamily
		m_ViewFamily.Views.clear();
		m_ViewFamily.Views.push_back({});

		// Camera
		m_Viewport.Width = std::max(1u, scDesc.Width);
		m_Viewport.Height = std::max(1u, scDesc.Height);

		m_Camera.SetProjAttribs(
			0.1f,
			300.0f,
			(float)m_Viewport.Width / (float)m_Viewport.Height,
			PI / 4.0f,
			SURFACE_TRANSFORM_IDENTITY);

		m_Camera.SetPos(float3(0.0f, 0.3f, -3.0f));
		m_Camera.SetRotation(0.0f, 0.0f);
		m_Camera.SetMoveSpeed(3.0f);
		m_Camera.SetRotationSpeed(0.01f);

		// Light
		m_GlobalLight.Direction = float3(0.4f, -1.0f, 0.3f);
		m_GlobalLight.Color = float3(1.0f, 1.0f, 1.0f);
		m_GlobalLight.Intensity = 2.0f;
		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);

		// Floor
		{
			AssetRef<AssimpAsset> floorRef = m_pAssetManager->RegisterAsset<AssimpAsset>(m_FloorMeshPath);
			AssetPtr<AssimpAsset> floorPtr = m_pAssetManager->LoadBlocking(floorRef);

			StaticMesh cpuFloorMesh = {};
			(void)BuildStaticMeshAsset(
				*floorPtr,
				&cpuFloorMesh,
				AssimpImportSettings{},
				nullptr,
				m_pAssetManager.get());

			m_Floor = m_pRenderScene->AddObject(
				m_pRenderer->CreateStaticMesh(cpuFloorMesh),
				Matrix4x4::TRS(
					{ 0.0f, -1.0f, 0.0f },
					{ 0.0f, 0.0f, 0.0f },
					{ 10.0f, 1.0f, 10.0f }),
				true);
		}

		// Main
		(void)LoadOrReplaceMainObject(
			m_MainMeshPath.c_str(),
			{ 0.0f, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ 1.0f, 1.0f, 1.0f },
			true);
	}

	void MaterialEditor::Render()
	{
		ASSERT(m_pRenderer, "Renderer is null.");
		ASSERT(m_pRenderScene, "RenderScene is null.");

		m_ViewFamily.FrameIndex++;

		m_pRenderer->BeginFrame();
		m_pRenderer->Render(*m_pRenderScene, m_ViewFamily);
		m_pRenderer->EndFrame();
	}

	void MaterialEditor::Update(double currTime, double elapsedTime, bool doUpdateUI)
	{
		SampleBase::Update(currTime, elapsedTime, doUpdateUI);

		const float dt = (float)elapsedTime;
		const float t = (float)currTime;

		m_Camera.Update(m_InputController, dt);

		m_ViewFamily.DeltaTime = dt;
		m_ViewFamily.CurrentTime = t;

		if (!m_ViewFamily.Views.empty())
		{
			auto& v = m_ViewFamily.Views[0];
			v.Viewport.left = 0;
			v.Viewport.top = 0;
			v.Viewport.right = m_Viewport.Width;
			v.Viewport.bottom = m_Viewport.Height;

			v.CameraPosition = m_Camera.GetPos();
			v.ViewMatrix = m_Camera.GetViewMatrix();
			v.ProjMatrix = m_Camera.GetProjMatrix();

			v.NearPlane = m_Camera.GetProjAttribs().NearClipPlane;
			v.FarPlane = m_Camera.GetProjAttribs().FarClipPlane;
		}

		if (m_pRenderScene && m_GlobalLightHandle.IsValid())
			m_pRenderScene->UpdateLight(m_GlobalLightHandle, m_GlobalLight);
	}

	void MaterialEditor::ReleaseSwapChainBuffers()
	{
		SampleBase::ReleaseSwapChainBuffers();

		if (m_pRenderer)
			m_pRenderer->ReleaseSwapChainBuffers();
	}

	void MaterialEditor::WindowResize(uint32 width, uint32 height)
	{
		SampleBase::WindowResize(width, height);

		m_Viewport.Width = std::max(1u, width);
		m_Viewport.Height = std::max(1u, height);

		m_Camera.SetProjAttribs(
			m_Camera.GetProjAttribs().NearClipPlane,
			m_Camera.GetProjAttribs().FarClipPlane,
			(float)m_Viewport.Width / (float)m_Viewport.Height,
			m_Camera.GetProjAttribs().FOV,
			SURFACE_TRANSFORM_IDENTITY);

		if (m_pRenderer)
			m_pRenderer->OnResize(m_Viewport.Width, m_Viewport.Height);
	}

	// ------------------------------------------------------------
	// UI
	// ------------------------------------------------------------

	void MaterialEditor::UpdateUI()
	{
		UiDockspace();
		UiScenePanel();
		UiViewportPanel();
		UiMaterialPanel();
		UiStatsPanel();
	}

	void MaterialEditor::UiDockspace()
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);

		const ImGuiWindowFlags hostFlags =
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus |
			ImGuiWindowFlags_MenuBar;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		if (ImGui::Begin("##MaterialEditorDockHost", nullptr, hostFlags))
		{
			ImGui::PopStyleVar(3);

			const ImGuiID dockspaceID = ImGui::GetID("##MaterialEditorDockspace");
			ImGui::DockSpace(dockspaceID, ImVec2(0, 0), ImGuiDockNodeFlags_None);

			if (!m_DockBuilt)
			{
				m_DockBuilt = true;

				ImGui::DockBuilderRemoveNode(dockspaceID);
				ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
				ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->Size);

				ImGuiID dockMain = dockspaceID;
				ImGuiID dockRight = 0;
				ImGuiID dockLeft = 0;
				ImGuiID dockBottom = 0;

				ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.34f, &dockRight, &dockMain);
				ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.24f, &dockLeft, &dockMain);
				ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.26f, &dockBottom, &dockMain);

				ImGui::DockBuilderDockWindow("Viewport", dockMain);
				ImGui::DockBuilderDockWindow("Scene", dockLeft);
				ImGui::DockBuilderDockWindow("Material", dockRight);
				ImGui::DockBuilderDockWindow("Stats", dockBottom);

				ImGui::DockBuilderFinish(dockspaceID);
			}

			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("UI"))
				{
					if (ImGui::MenuItem("Refresh Slot UI"))
						MarkAllSlotUiDirty();

					ImGui::EndMenu();
				}
				ImGui::EndMenuBar();
			}
		}
		ImGui::End();
	}

	void MaterialEditor::UiScenePanel()
	{
		if (!ImGui::Begin("Scene"))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("Main Object");
		ImGui::Separator();

		InputTextStdString("Path", m_MainMeshPath);

		ImGui::Separator();
		ImGui::Text("Load Options");

		ImGui::DragFloat3("Position", reinterpret_cast<float*>(&m_Main.Position), 0.01f);
		ImGui::DragFloat3("Rotation", reinterpret_cast<float*>(&m_Main.Rotation), 0.5f);
		ImGui::DragFloat3("Scale", reinterpret_cast<float*>(&m_Main.Scale), 0.01f);

		if (ImGui::Checkbox("Uniform Scale", &m_bUniformScale))
		{
			if (m_Main.ImportedCpuMesh)
			{
				if (m_bUniformScale)
				{
					float uniformScale = ComputeUniformScale(m_Main.ImportedCpuMesh->GetBounds());
					m_Main.Scale = float3{ uniformScale, uniformScale, uniformScale };
				}
				else
				{
					m_Main.Scale = float3{ 1.0f, 1.0f, 1.0f };
				}
			}
		}

		ImGui::Checkbox("Cast Shadow (Object)", &m_Main.bCastShadow);

		if (ImGui::Button("Load / Replace"))
		{
			const std::string p = SanitizeFilePath(m_MainMeshPath);
			if (!p.empty())
			{
				(void)LoadOrReplaceMainObject(
					p.c_str(),
					m_Main.Position,
					m_Main.Rotation,
					m_Main.Scale,
					m_Main.bCastShadow);
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Save Main Object");
		ImGui::Separator();

		InputTextStdString("Out Path", m_MainMeshSavePath);

		if (ImGui::Button("Save"))
		{
			std::string err;
			const bool ok = SaveMainObject(
				m_MainMeshSavePath,
				EAssetSaveFlags::None,
				&err);

			if (!ok)
				ASSERT(false, err.empty() ? "Save failed." : err.c_str());
		}

		// Apply transform live
		if (m_Main.ImportedCpuMesh != nullptr)
		{
			if (RenderScene::RenderObject* obj = GetMainRenderObjectOrNull())
			{
				obj->World = Matrix4x4::TRS(m_Main.Position, m_Main.Rotation, m_Main.Scale);
				obj->WorldInvTranspose = obj->World.Inversed().Transposed();
				obj->bCastShadow = m_Main.bCastShadow;
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Light");
		ImGui::Separator();

		ImGui::gizmo3D("##LightDirection", m_GlobalLight.Direction, ImGui::GetTextLineHeight() * 7);
		ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&m_GlobalLight.Color));
		ImGui::SliderFloat("Intensity", &m_GlobalLight.Intensity, 0.01f, 20.0f);

		ImGui::End();
	}

	void MaterialEditor::UiViewportPanel()
	{
		if (!ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImGui::End();
			return;
		}

		m_Viewport.Hovered = ImGui::IsWindowHovered();
		m_Viewport.Focused = ImGui::IsWindowFocused();

		const ImVec2 avail = ImGui::GetContentRegionAvail();

		const uint32 newW = (uint32)std::max(1.0f, avail.x);
		const uint32 newH = (uint32)std::max(1.0f, avail.y);

		if (newW != m_Viewport.Width || newH != m_Viewport.Height)
		{
			m_Viewport.Width = newW;
			m_Viewport.Height = newH;

			m_Camera.SetProjAttribs(
				m_Camera.GetProjAttribs().NearClipPlane,
				m_Camera.GetProjAttribs().FarClipPlane,
				(float)newW / (float)newH,
				m_Camera.GetProjAttribs().FOV,
				SURFACE_TRANSFORM_IDENTITY);

			if (m_pRenderer)
				m_pRenderer->OnResize(newW, newH);
		}

		ITextureView* pColor = m_pRenderer ? m_pRenderer->GetLightingSRV() : nullptr;
		if (pColor)
		{
			ImTextureID tid = reinterpret_cast<ImTextureID>(pColor);
			ImGui::Image(tid, ImVec2((float)m_Viewport.Width, (float)m_Viewport.Height), ImVec2(0, 0), ImVec2(1, 1));
		}
		else
		{
			ImGui::TextDisabled("No renderer output.");
		}

		ImGui::End();
	}

	void MaterialEditor::UiMaterialPanel()
	{
		if (!ImGui::Begin("Material"))
		{
			ImGui::End();
			return;
		}

		StaticMesh* cpu = m_Main.ImportedCpuMesh;
		if (!cpu)
		{
			ImGui::TextDisabled("Load a StaticMesh first.");
			ImGui::End();
			return;
		}

		const uint32 slotCount = cpu->GetMaterialSlotCount();
		if (slotCount == 0)
		{
			ImGui::TextDisabled("This mesh has no material slots.");
			ImGui::End();
			return;
		}

		// Slot picker
		{
			int slot = m_SelectedSlot;
			if (ImGui::SliderInt("Slot", &slot, 0, std::max(0, (int)slotCount - 1)))
			{
				m_SelectedSlot = slot;
				GetOrCreateSlotUi((uint32)m_SelectedSlot).Dirty = true;
			}
		}

		const uint32 slotIndex = (uint32)std::clamp<int32>(m_SelectedSlot, 0, (int32)slotCount - 1);
		Material& mat = cpu->GetMaterialSlot(slotIndex);
		const MaterialTemplate& tmpl = mat.GetTemplate();

		SlotUiState& ui = GetOrCreateSlotUi(slotIndex);
		if (ui.Dirty)
			SyncSlotUiFromMaterial(ui, mat);

		ImGui::Text("Material Slot %u", slotIndex);
		ImGui::Separator();

		// ------------------------------------------------------------
		// Template (requires recreate)
		// ------------------------------------------------------------
		{
			ASSERT(m_pRenderer, "Renderer is null.");

			const auto tmplNames = m_pRenderer->GetAllMaterialTemplateNames();

			// Build const char* list for ImGui
			static std::vector<const char*> s_Items;
			s_Items.clear();
			for (const std::string& s : tmplNames)
				s_Items.push_back(s.c_str());

			// Ensure index is valid
			if (ui.TemplateComboIndex < 0 || ui.TemplateComboIndex >= (int)s_Items.size())
			{
				ui.TemplateComboIndex = 0;
				if (!tmplNames.empty())
					ui.PendingTemplateName = tmplNames[0];
			}

			ImGui::Text("Material Template");

			if (ImGui::Combo(
				"Template",
				&ui.TemplateComboIndex,
				s_Items.data(),
				(int)s_Items.size()))
			{
				// Selection changed (NOT applied yet)
				ui.PendingTemplateName = tmplNames[ui.TemplateComboIndex];
			}

			ImGui::SameLine();

			if (ImGui::Button("Recreate"))
			{
				const std::string& desiredTmpl = ui.PendingTemplateName;
				if (!desiredTmpl.empty() && desiredTmpl != mat.GetTemplateName())
				{
					if (RecreateMaterialWithTemplate(&mat, mat, desiredTmpl))
					{
						// Template change affects PSO/layout
						(void)RebuildMainMeshRenderData();
						ui.Dirty = true;
					}
				}
			}

			ImGui::SameLine();
			ImGui::TextDisabled("Current: %s", mat.GetTemplateName().c_str());

			// Render pass name (unchanged)
			{
				std::string rp = mat.GetRenderPassName();
				if (InputTextStdString("RenderPass", rp))
					mat.SetRenderPassName(rp);
			}
		}

		ImGui::Spacing();

		// ------------------------------------------------------------
		// Options (direct set/get)
		// ------------------------------------------------------------
		if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Blend mode
			{
				const char* items[] = { "OPAQUE","MASKED","TRANSLUCENT","ADDITIVE","PREMULTIPLIED" };
				int sel = 0;
				switch (mat.GetBlendMode())
				{
				case MATERIAL_BLEND_MODE_OPAQUE:        sel = 0; break;
				case MATERIAL_BLEND_MODE_MASKED:        sel = 1; break;
				case MATERIAL_BLEND_MODE_TRANSLUCENT:   sel = 2; break;
				case MATERIAL_BLEND_MODE_ADDITIVE:      sel = 3; break;
				case MATERIAL_BLEND_MODE_PREMULTIPLIED: sel = 4; break;
				default:                                sel = 0; break;
				}

				if (ImGui::Combo("BlendMode", &sel, items, 5))
				{
					static const MATERIAL_BLEND_MODE map[] =
					{
						MATERIAL_BLEND_MODE_OPAQUE,
						MATERIAL_BLEND_MODE_MASKED,
						MATERIAL_BLEND_MODE_TRANSLUCENT,
						MATERIAL_BLEND_MODE_ADDITIVE,
						MATERIAL_BLEND_MODE_PREMULTIPLIED
					};
					mat.SetBlendMode(map[sel]);
					ui.Dirty = true;
				}

				ImGui::SameLine();
				ImGui::TextDisabled("(%s)", BlendModeLabel(mat.GetBlendMode()));
			}

			// Cull mode
			{
				CULL_MODE cm = mat.GetCullMode();
				const char* items[] = { "None", "Front", "Back" };
				int idx = (cm == CULL_MODE_NONE) ? 0 : (cm == CULL_MODE_FRONT) ? 1 : 2;

				if (ImGui::Combo("Cull", &idx, items, 3))
				{
					const CULL_MODE newCm =
						(idx == 0) ? CULL_MODE_NONE :
						(idx == 1) ? CULL_MODE_FRONT :
						CULL_MODE_BACK;

					mat.SetCullMode(newCm);
					ui.Dirty = true;
				}
			}

			// FrontCCW
			{
				bool v = mat.GetFrontCounterClockwise();
				if (ImGui::Checkbox("FrontCCW", &v))
				{
					mat.SetFrontCounterClockwise(v);
					ui.Dirty = true;
				}
			}

			// Depth
			{
				bool v = mat.GetDepthEnable();
				if (ImGui::Checkbox("DepthEnable", &v))
				{
					mat.SetDepthEnable(v);
					ui.Dirty = true;
				}
			}
			{
				bool v = mat.GetDepthWriteEnable();
				if (ImGui::Checkbox("DepthWrite", &v))
				{
					mat.SetDepthWriteEnable(v);
					ui.Dirty = true;
				}
			}
			{
				COMPARISON_FUNCTION f = mat.GetDepthFunc();
				const char* labels[] = { "NEVER","LESS","EQUAL","LEQUAL","GREATER","NOT_EQUAL","GEQUAL","ALWAYS" };
				int sel = 3;
				switch (f)
				{
				case COMPARISON_FUNC_NEVER:         sel = 0; break;
				case COMPARISON_FUNC_LESS:          sel = 1; break;
				case COMPARISON_FUNC_EQUAL:         sel = 2; break;
				case COMPARISON_FUNC_LESS_EQUAL:    sel = 3; break;
				case COMPARISON_FUNC_GREATER:       sel = 4; break;
				case COMPARISON_FUNC_NOT_EQUAL:     sel = 5; break;
				case COMPARISON_FUNC_GREATER_EQUAL: sel = 6; break;
				case COMPARISON_FUNC_ALWAYS:        sel = 7; break;
				default:                            sel = 3; break;
				}

				if (ImGui::Combo("DepthFunc", &sel, labels, 8))
				{
					static const COMPARISON_FUNCTION map[] =
					{
						COMPARISON_FUNC_NEVER,
						COMPARISON_FUNC_LESS,
						COMPARISON_FUNC_EQUAL,
						COMPARISON_FUNC_LESS_EQUAL,
						COMPARISON_FUNC_GREATER,
						COMPARISON_FUNC_NOT_EQUAL,
						COMPARISON_FUNC_GREATER_EQUAL,
						COMPARISON_FUNC_ALWAYS,
					};
					mat.SetDepthFunc(map[sel]);
					ui.Dirty = true;
				}
			}

			// Texture binding mode
			{
				MATERIAL_TEXTURE_BINDING_MODE m = mat.GetTextureBindingMode();
				const char* items[] = { "DYNAMIC", "MUTABLE" };
				int mode = (m == MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC) ? 0 : 1;

				if (ImGui::Combo("TexBinding", &mode, items, 2))
				{
					mat.SetTextureBindingMode((mode == 0) ? MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC : MATERIAL_TEXTURE_BINDING_MODE_MUTABLE);
					ui.Dirty = true;
				}
			}

			// LinearWrap Sampler
			{
				std::string samplerName = mat.GetLinearWrapSamplerName();
				if (InputTextStdString("LinearWrapName", samplerName))
				{
					mat.SetLinearWrapSamplerName(samplerName);
					ui.Dirty = true;
				}

				// NOTE: Expose desc edit if needed later (kept simple now).
			}

			ImGui::Separator();
			ImGui::TextDisabled("CastShadow is per RenderObject (not in Material).");
		}

		ImGui::Spacing();

		// ------------------------------------------------------------
		// Values (reflection-driven) - edit UI.ValueBytes then Apply
		// ------------------------------------------------------------
		if (ImGui::CollapsingHeader("Values", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static char s_Filter[128] = {};
			ImGui::InputTextWithHint("Filter", "name contains...", s_Filter, sizeof(s_Filter));

			auto passFilter = [&](const std::string& name) -> bool
				{
					if (s_Filter[0] == 0)
						return true;
					return name.find(s_Filter) != std::string::npos;
				};

			for (uint32 i = 0; i < tmpl.GetValueParamCount(); ++i)
			{
				const MaterialValueParamDesc& desc = tmpl.GetValueParam(i);
				if (desc.Name.empty())
					continue;

				if (!passFilter(desc.Name))
					continue;

				ImGui::PushID((int)i);

				uint32 sz = desc.ByteSize != 0 ? desc.ByteSize : ValueTypeByteSize(desc.Type);
				if (sz == 0)
				{
					ImGui::TextDisabled("%s (invalid size)", desc.Name.c_str());
					ImGui::PopID();
					continue;
				}

				std::vector<uint8>& bytes = ui.ValueBytes[desc.Name];
				if ((uint32)bytes.size() != sz)
				{
					bytes.resize(sz);
					std::memset(bytes.data(), 0, bytes.size());
				}

				switch (desc.Type)
				{
				case MATERIAL_VALUE_TYPE_FLOAT:
				{
					float v = 0.0f;
					std::memcpy(&v, bytes.data(), sizeof(float));
					if (ImGui::DragFloat(desc.Name.c_str(), &v, 0.01f))
						std::memcpy(bytes.data(), &v, sizeof(float));
					break;
				}
				case MATERIAL_VALUE_TYPE_FLOAT2:
				{
					float v[2] = {};
					std::memcpy(v, bytes.data(), sizeof(float) * 2);
					if (ImGui::DragFloat2(desc.Name.c_str(), v, 0.01f))
						std::memcpy(bytes.data(), v, sizeof(float) * 2);
					break;
				}
				case MATERIAL_VALUE_TYPE_FLOAT3:
				{
					float v[3] = {};
					std::memcpy(v, bytes.data(), sizeof(float) * 3);

					if (IsColorNameLike(desc.Name))
					{
						if (ImGui::ColorEdit3(desc.Name.c_str(), v))
							std::memcpy(bytes.data(), v, sizeof(float) * 3);
					}
					else
					{
						if (ImGui::DragFloat3(desc.Name.c_str(), v, 0.01f))
							std::memcpy(bytes.data(), v, sizeof(float) * 3);
					}
					break;
				}
				case MATERIAL_VALUE_TYPE_FLOAT4:
				{
					float v[4] = {};
					std::memcpy(v, bytes.data(), sizeof(float) * 4);

					if (IsColorNameLike(desc.Name))
					{
						if (ImGui::ColorEdit4(desc.Name.c_str(), v))
							std::memcpy(bytes.data(), v, sizeof(float) * 4);
					}
					else
					{
						if (ImGui::DragFloat4(desc.Name.c_str(), v, 0.01f))
							std::memcpy(bytes.data(), v, sizeof(float) * 4);
					}
					break;
				}
				case MATERIAL_VALUE_TYPE_INT:
				{
					int32 v = 0;
					std::memcpy(&v, bytes.data(), sizeof(int32));
					if (ImGui::DragInt(desc.Name.c_str(), &v, 1.0f))
						std::memcpy(bytes.data(), &v, sizeof(int32));
					break;
				}
				case MATERIAL_VALUE_TYPE_UINT:
				{
					uint32 v = 0;
					std::memcpy(&v, bytes.data(), sizeof(uint32));

					int tmp = (int)v;
					if (ImGui::DragInt(desc.Name.c_str(), &tmp, 1.0f, 0, INT32_MAX))
					{
						v = (uint32)std::max(0, tmp);
						std::memcpy(bytes.data(), &v, sizeof(uint32));
					}
					break;
				}
				case MATERIAL_VALUE_TYPE_FLOAT4X4:
				{
					ImGui::Text("%s (float4x4)", desc.Name.c_str());
					ImGui::SameLine();
					if (ImGui::SmallButton("Reset Identity"))
					{
						const float m[16] =
						{
							1,0,0,0,
							0,1,0,0,
							0,0,1,0,
							0,0,0,1
						};
						bytes.resize(sizeof(m));
						std::memcpy(bytes.data(), m, sizeof(m));
					}
					break;
				}
				default:
				{
					ImGui::Text("%s (type=%u, %u bytes)", desc.Name.c_str(), (uint32)desc.Type, (uint32)bytes.size());
					break;
				}
				}

				ImGui::PopID();
			}
		}

		ImGui::Spacing();

		// ------------------------------------------------------------
		// Resources (reflection-driven)
		// ------------------------------------------------------------
		if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static char s_Filter[128] = {};
			ImGui::InputTextWithHint("Filter##Res", "name contains...", s_Filter, sizeof(s_Filter));

			auto passFilter = [&](const std::string& name) -> bool
				{
					if (s_Filter[0] == 0)
						return true;
					return name.find(s_Filter) != std::string::npos;
				};

			for (uint32 i = 0; i < tmpl.GetResourceCount(); ++i)
			{
				const MaterialResourceDesc& res = tmpl.GetResource(i);
				if (res.Name.empty())
					continue;

				if (!IsTextureType(res.Type))
					continue;

				if (!passFilter(res.Name))
					continue;

				ImGui::PushID((int)i);

				std::string& path = ui.TexturePaths[res.Name];
				bool& bHasSampler = ui.bHasSamplerOverride[res.Name];
				SamplerDesc& sdesc = ui.SamplerOverrideDesc[res.Name];

				ImGui::Text("%s", res.Name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("(%s)",
					(res.Type == MATERIAL_RESOURCE_TYPE_TEXTURECUBE) ? "Cube" :
					(res.Type == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ? "2DArray" : "2D");

				InputTextStdString("Path", path);

				ImGui::SameLine();
				if (ImGui::Button("Clear"))
					path.clear();

				// Sampler override
				{
					if (ImGui::Checkbox("SamplerOverride", &bHasSampler))
					{
						// keep sdesc as-is
					}

					if (bHasSampler)
					{
						// NOTE: Keep minimal UI: just address/filter presets are not exposed.
						// If you want full SamplerDesc editor, add it here.
						ImGui::TextDisabled("SamplerDesc editor is TODO");
					}
				}

				ImGui::PopID();
				ImGui::Separator();
			}
		}

		ImGui::Spacing();
		ImGui::Separator();

		// ------------------------------------------------------------
		// Apply
		// ------------------------------------------------------------
		{
			if (ImGui::Button("Apply"))
			{
				ApplySlotUiToMaterial(mat, ui, true);
				ui.Dirty = true;
			}
		}

		// Keep UI in sync if requested
		if (ui.Dirty)
			SyncSlotUiFromMaterial(ui, mat);

		ImGui::End();
	}

	void MaterialEditor::UiStatsPanel()
	{
		if (!ImGui::Begin("Stats"))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("Viewport: %ux%u", m_Viewport.Width, m_Viewport.Height);
		ImGui::Text("Selected Slot: %d", m_SelectedSlot);

		ImGuiIO& io = ImGui::GetIO();
		ImGui::Text("ImGui FPS: %.1f", io.Framerate);

		if (m_pRenderer)
		{
			const auto passTable = m_pRenderer->GetPassDrawCallCountTable();

			uint64 total = 0;
			for (const auto& kv : passTable)
				total += kv.second;

			ImGui::Separator();
			ImGui::Text("Total Draw Calls: %llu", (unsigned long long)total);

			for (const auto& kv : passTable)
				ImGui::Text("%s: %llu", kv.first.c_str(), (unsigned long long)kv.second);
		}

		ImGui::End();
	}
} // namespace shz

