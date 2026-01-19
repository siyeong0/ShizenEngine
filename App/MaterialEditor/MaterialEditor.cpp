// ============================================================================
// Samples/MaterialEditor/MaterialEditor.cpp
// ============================================================================

#include "MaterialEditor.h"

#include <iostream>
#include <cmath>
#include <cstring>
#include <algorithm>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/ImGuiUtils.hpp"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/AssetRuntime/Public/AssimpImporter.h"
#include "Engine/AssetRuntime/Public/AssetTypeTraits.h"

#include "Tools/Image/Public/TextureUtilities.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <commdlg.h>
#endif

namespace shz
{
	namespace
	{
#include "Shaders/HLSL_Structures.hlsli"

		static float computeUniformScaleToFitUnitCube(const Box& bounds, float targetSize = 1.0f) noexcept
		{
			const float3 size = bounds.Max - bounds.Min;

			float maxDim = size.x;
			if (size.y > maxDim) maxDim = size.y;
			if (size.z > maxDim) maxDim = size.z;

			const float eps = 1e-6f;
			if (maxDim < eps)
				return 1.0f;

			return targetSize / maxDim;
		}

		static uint32 PackRGBA8(uint8 r, uint8 g, uint8 b, uint8 a) noexcept
		{
			return (uint32(r) << 0) | (uint32(g) << 8) | (uint32(b) << 16) | (uint32(a) << 24);
		}

		static MaterialEditor::ShaderPreset g_ShaderPresets[] =
		{
			{
				"PBR GBuffer (GBuffer.vsh / GBuffer.psh)",
				"GBuffer.vsh",
				"GBuffer.psh",
				"main",
				"main",
				false,
				false
			},
			// { "Forward (Forward.vsh / Forward.psh)", "Forward.vsh", "Forward.psh", "main", "main", false, false },
		};

		static const char* g_RenderPassNames[] =
		{
			"GBuffer",
			"Forward",
			"Shadow",
			"Post",
		};

		static const char* CullModeLabel(CULL_MODE m)
		{
			switch (m)
			{
			case CULL_MODE_NONE:  return "None";
			case CULL_MODE_FRONT: return "Front";
			case CULL_MODE_BACK:  return "Back";
			default:              return "Unknown";
			}
		}

		static const char* DepthFuncLabel(COMPARISON_FUNCTION f)
		{
			switch (f)
			{
			case COMPARISON_FUNC_NEVER:         return "NEVER";
			case COMPARISON_FUNC_LESS:          return "LESS";
			case COMPARISON_FUNC_EQUAL:         return "EQUAL";
			case COMPARISON_FUNC_LESS_EQUAL:    return "LEQUAL";
			case COMPARISON_FUNC_GREATER:       return "GREATER";
			case COMPARISON_FUNC_NOT_EQUAL:     return "NOT_EQUAL";
			case COMPARISON_FUNC_GREATER_EQUAL: return "GEQUAL";
			case COMPARISON_FUNC_ALWAYS:        return "ALWAYS";
			default:                            return "UNKNOWN";
			}
		}

		static const char* TexBindModeLabel(MATERIAL_TEXTURE_BINDING_MODE m)
		{
			switch (m)
			{
			case MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC: return "DYNAMIC (Editor friendly)";
			case MATERIAL_TEXTURE_BINDING_MODE_MUTABLE: return "MUTABLE (No overwrite by default)";
			default: return "UNKNOWN";
			}
		}
	}

	SampleBase* CreateSample()
	{
		return new MaterialEditor();
	}

	// ------------------------------------------------------------
	// AssetManager integration helpers
	// ------------------------------------------------------------

	AssetID MaterialEditor::makeAssetIDFromPath(AssetTypeID typeId, const std::string& path) const
	{
		const size_t h0 = std::hash<std::string>{}(path);
		const size_t h1 = std::hash<std::string>{}(path + std::to_string(static_cast<uint64>(typeId)));

		const uint64 hi = static_cast<uint64>(h0) ^ (static_cast<uint64>(typeId) * 0x9E3779B185EBCA87ull);
		const uint64 lo = static_cast<uint64>(h1) ^ (static_cast<uint64>(typeId) * 0xC2B2AE3D27D4EB4Full);

		return AssetID(hi, lo);
	}

	void MaterialEditor::registerAssetLoaders()
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");

		m_pAssetManager->RegisterLoader(AssetTypeTraits<StaticMeshAsset>::TypeID,
			[](const AssetRegistry::AssetMeta& meta,
				std::unique_ptr<AssetObject>& outObject,
				uint64& outResidentBytes,
				std::string& outError) -> bool
			{
				StaticMeshAsset mesh = {};
				if (!AssimpImporter::LoadStaticMeshAsset(meta.SourcePath.c_str(), &mesh))
				{
					outError = "LoadStaticMeshAsset failed.";
					return false;
				}

				outObject = std::make_unique<TypedAssetObject<StaticMeshAsset>>(static_cast<StaticMeshAsset&&>(mesh));
				outResidentBytes = 0;
				outError.clear();
				return true;
			});

		m_pAssetManager->RegisterLoader(AssetTypeTraits<TextureAsset>::TypeID,
			[](const AssetRegistry::AssetMeta& meta,
				std::unique_ptr<AssetObject>& outObject,
				uint64& outResidentBytes,
				std::string& outError) -> bool
			{
				TextureAsset tex = {};
				(void)meta;

				outObject = std::make_unique<TypedAssetObject<TextureAsset>>(static_cast<TextureAsset&&>(tex));
				outResidentBytes = 0;
				outError.clear();
				return true;
			});

		m_pAssetManager->RegisterLoader(AssetTypeTraits<MaterialInstanceAsset>::TypeID,
			[](const AssetRegistry::AssetMeta& meta,
				std::unique_ptr<AssetObject>& outObject,
				uint64& outResidentBytes,
				std::string& outError) -> bool
			{
				MaterialInstanceAsset mi = {};
				(void)meta;

				outObject = std::make_unique<TypedAssetObject<MaterialInstanceAsset>>(static_cast<MaterialInstanceAsset&&>(mi));
				outResidentBytes = 0;
				outError.clear();
				return true;
			});
	}

	AssetRef<StaticMeshAsset> MaterialEditor::registerStaticMeshPath(const std::string& path)
	{
		const AssetID id = makeAssetIDFromPath(AssetTypeTraits<StaticMeshAsset>::TypeID, path);
		m_pAssetManager->RegisterAsset(id, AssetTypeTraits<StaticMeshAsset>::TypeID, path);
		return AssetRef<StaticMeshAsset>(id);
	}

	AssetRef<TextureAsset> MaterialEditor::registerTexturePath(const std::string& path)
	{
		const AssetID id = makeAssetIDFromPath(AssetTypeTraits<TextureAsset>::TypeID, path);
		m_pAssetManager->RegisterAsset(id, AssetTypeTraits<TextureAsset>::TypeID, path);
		return AssetRef<TextureAsset>(id);
	}

	AssetPtr<StaticMeshAsset> MaterialEditor::loadStaticMeshBlocking(AssetRef<StaticMeshAsset> ref, EAssetLoadFlags flags)
	{
		return m_pAssetManager->LoadBlocking(ref, flags);
	}

	AssetPtr<TextureAsset> MaterialEditor::loadTextureBlocking(AssetRef<TextureAsset> ref, EAssetLoadFlags flags)
	{
		return m_pAssetManager->LoadBlocking(ref, flags);
	}

	// ------------------------------------------------------------
	// Path sanitize
	// ------------------------------------------------------------

	std::string MaterialEditor::sanitizeFilePath(std::string s)
	{
		auto IsSpace = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };

		while (!s.empty() && IsSpace((unsigned char)s.front()))
			s.erase(s.begin());
		while (!s.empty() && IsSpace((unsigned char)s.back()))
			s.pop_back();

		if (s.size() >= 2)
		{
			const char a = s.front();
			const char b = s.back();
			if ((a == '"' && b == '"') || (a == '\'' && b == '\''))
			{
				s = s.substr(1, s.size() - 2);

				while (!s.empty() && IsSpace((unsigned char)s.front()))
					s.erase(s.begin());
				while (!s.empty() && IsSpace((unsigned char)s.back()))
					s.pop_back();
			}
		}

		for (char& c : s)
		{
			if (c == '\\') c = '/';
		}

		return s;
	}

	// ------------------------------------------------------------
	// Runtime texture cache
	// ------------------------------------------------------------

	void MaterialEditor::ensureResourceStateSRV(ITexture* pTex)
	{
		if (!m_pImmediateContext || !pTex)
			return;

		StateTransitionDesc barrier = {};
		barrier.pResource = pTex;
		barrier.OldState = RESOURCE_STATE_UNKNOWN;
		barrier.NewState = RESOURCE_STATE_SHADER_RESOURCE;
		barrier.Flags = STATE_TRANSITION_FLAG_UPDATE_STATE;

		m_pImmediateContext->TransitionResourceStates(1, &barrier);
	}

	ITextureView* MaterialEditor::getOrCreateTextureSRV(const std::string& inPath, bool isSRGB)
	{
		std::string path = sanitizeFilePath(inPath);
		if (path.empty())
			return nullptr;

		auto it = m_RuntimeTextureCache.find(path);
		if (it != m_RuntimeTextureCache.end() && it->second)
		{
			return it->second->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		}

		AssetRef<TextureAsset> texRef = registerTexturePath(path);
		(void)loadTextureBlocking(texRef, EAssetLoadFlags::AllowFallback);

		RefCntAutoPtr<ITexture> tex;

		TextureLoadInfo tli = {};
		tli.IsSRGB = isSRGB;

		CreateTextureFromFile(path.c_str(), tli, m_pDevice, &tex);
		if (!tex)
			return nullptr;

		ensureResourceStateSRV(tex.RawPtr());

		m_RuntimeTextureCache.emplace(path, tex);
		return tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
	}

	// ------------------------------------------------------------
	// Win32 dialog / drag&drop (optional)
	// ------------------------------------------------------------

	bool MaterialEditor::openFileDialog(std::string& outPath, const char* filter, const char* title) const
	{
#if defined(_WIN32)
		char buf[MAX_PATH] = {};
		OPENFILENAMEA ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = nullptr; // TODO: set your sample window HWND here if accessible
		ofn.lpstrFile = buf;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		ofn.lpstrTitle = title;

		if (GetOpenFileNameA(&ofn))
		{
			outPath = sanitizeFilePath(buf);
			return true;
		}
#else
		(void)outPath; (void)filter; (void)title;
#endif
		return false;
	}

	void MaterialEditor::enableWindowDragDrop()
	{
		// NOTE:
		// This needs access to HWND to call DragAcceptFiles(hwnd, TRUE).
		// If your SampleBase exposes it (or platform layer does), wire it here.
	}

	void MaterialEditor::onFilesDropped(const std::vector<std::string>& paths)
	{
		// Minimal policy:
		// - store list for UI use (user chooses which slot to apply)
		m_DroppedFiles = paths;
	}

	// ------------------------------------------------------------
	// Template / Instance workflow
	// ------------------------------------------------------------

	const MaterialEditor::ShaderPreset& MaterialEditor::getCurrentShaderPreset() const
	{
		ASSERT(m_SelectedShaderPreset >= 0 && m_SelectedShaderPreset < (int32)_countof(g_ShaderPresets), "Invalid shader preset index.");
		return g_ShaderPresets[m_SelectedShaderPreset];
	}

	std::string MaterialEditor::makeTemplateKeyFromPreset(const ShaderPreset& p) const
	{
		std::string k;
		k.reserve(256);
		k += "VS:";  k += (p.VS ? p.VS : "");
		k += "|VSE:"; k += (p.VSEntry ? p.VSEntry : "");
		k += "|PS:";  k += (p.PS ? p.PS : "");
		k += "|PSE:"; k += (p.PSEntry ? p.PSEntry : "");
		k += "|CS:";
		k += (p.VS_CombinedSamplers ? "1" : "0");
		k += (p.PS_CombinedSamplers ? "1" : "0");
		return k;
	}

	MaterialTemplate* MaterialEditor::getOrCreateTemplate(const ShaderPreset& preset)
	{
		const std::string key = makeTemplateKeyFromPreset(preset);

		auto it = m_TemplateCache.find(key);
		if (it != m_TemplateCache.end())
			return &it->second;

		MaterialTemplate tmpl = {};

		MaterialTemplateCreateInfo tci = {};
		tci.PipelineType = MATERIAL_PIPELINE_TYPE_GRAPHICS;
		tci.TemplateName = std::string("MaterialEditor: ") + preset.Label;

		tci.ShaderStages.clear();
		tci.ShaderStages.reserve(2);

		MaterialShaderStageDesc vs = {};
		vs.ShaderType = SHADER_TYPE_VERTEX;
		vs.DebugName = "MaterialEditor VS";
		vs.FilePath = preset.VS;
		vs.EntryPoint = preset.VSEntry ? preset.VSEntry : "main";
		vs.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		vs.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
		vs.UseCombinedTextureSamplers = preset.VS_CombinedSamplers;

		MaterialShaderStageDesc ps = {};
		ps.ShaderType = SHADER_TYPE_PIXEL;
		ps.DebugName = "MaterialEditor PS";
		ps.FilePath = preset.PS;
		ps.EntryPoint = preset.PSEntry ? preset.PSEntry : "main";
		ps.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		ps.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
		ps.UseCombinedTextureSamplers = preset.PS_CombinedSamplers;

		tci.ShaderStages.push_back(vs);
		tci.ShaderStages.push_back(ps);

		const bool ok = tmpl.Initialize(m_pDevice, m_pShaderSourceFactory, tci);
		ASSERT(ok, "MaterialTemplate::Initialize failed.");

		auto [insIt, _] = m_TemplateCache.emplace(key, static_cast<MaterialTemplate&&>(tmpl));
		return &insIt->second;
	}

	bool MaterialEditor::rebuildAllMaterialsFromCurrentTemplate()
	{
		if (!m_pRenderScene)
			return false;

		MaterialTemplate* pTemplate = getOrCreateTemplate(getCurrentShaderPreset());
		if (!pTemplate)
			return false;

		auto& objects = m_pRenderScene->GetObjects();

		for (size_t i = 0; i < m_Loaded.size(); ++i)
		{
			LoadedMesh& entry = m_Loaded[i];
			if (!entry.MeshPtr)
				continue;

			if (entry.SceneObjectIndex < 0 || entry.SceneObjectIndex >= (int32)objects.size())
				continue;

			const StaticMeshAsset* cpuMesh = entry.MeshPtr.Get();
			if (!cpuMesh)
				continue;

			std::vector<MaterialInstance> mats = buildMaterialsForCpuMeshSlots(*cpuMesh);
			objects[(size_t)entry.SceneObjectIndex].Materials = static_cast<std::vector<MaterialInstance>&&>(mats);
		}

		return true;
	}

	// ------------------------------------------------------------
	// Preview scene / mesh
	// ------------------------------------------------------------

	void MaterialEditor::loadPreviewMesh(const char* path, float3 position, float3 rotation, float3 scale, bool bUniformScale)
	{
		LoadedMesh entry = {};
		entry.Path = path;

		entry.MeshRef = registerStaticMeshPath(entry.Path);
		entry.MeshID = makeAssetIDFromPath(AssetTypeTraits<StaticMeshAsset>::TypeID, entry.Path);
		entry.MeshPtr = loadStaticMeshBlocking(entry.MeshRef);

		const StaticMeshAsset* cpuMesh = entry.MeshPtr.Get();
		if (!cpuMesh)
		{
			std::cout << "Load Failed: " << entry.Path << std::endl;
			ASSERT(false, "");
			return;
		}

		entry.Scale = float3(1, 1, 1);
		if (bUniformScale)
		{
			const float uniform = computeUniformScaleToFitUnitCube(cpuMesh->GetBounds(), 1.0f);
			entry.Scale = float3(uniform, uniform, uniform);
		}

		entry.MeshHandle = m_pRenderer->CreateStaticMesh(*cpuMesh);
		if (!entry.MeshHandle.IsValid())
		{
			std::cout << "CreateStaticMesh failed: " << entry.Path << std::endl;
			ASSERT(false, "");
			return;
		}

		entry.Position = position;
		entry.BaseRotation = rotation;
		entry.Scale *= scale;

		std::vector<MaterialInstance> materials = buildMaterialsForCpuMeshSlots(*cpuMesh);

		entry.ObjectId = m_pRenderScene->AddObject(
			entry.MeshHandle,
			std::move(materials),
			Matrix4x4::TRS(entry.Position, entry.BaseRotation, entry.Scale));

		entry.SceneObjectIndex = (int32)m_pRenderScene->GetObjects().size() - 1;
		m_Loaded.push_back(static_cast<LoadedMesh&&>(entry));
	}

	// ------------------------------------------------------------
	// Material build/apply
	// ------------------------------------------------------------

	void MaterialEditor::seedFromImportedAsset(MaterialInstance& mat, const MaterialInstanceAsset& matAsset)
	{
		const auto& p = matAsset.GetParams();

		float bc[4] = { p.BaseColor.x, p.BaseColor.y, p.BaseColor.z, p.BaseColor.w };
		mat.SetFloat4("g_BaseColorFactor", bc);

		mat.SetFloat("g_RoughnessFactor", p.Roughness);
		mat.SetFloat("g_MetallicFactor", p.Metallic);
		mat.SetFloat("g_OcclusionStrength", p.Occlusion);

		float ec[3] = { p.EmissiveColor.x, p.EmissiveColor.y, p.EmissiveColor.z };
		mat.SetFloat3("g_EmissiveFactor", ec);
		mat.SetFloat("g_EmissiveIntensity", p.EmissiveIntensity);

		mat.SetFloat("g_AlphaCutoff", p.AlphaCutoff);
		mat.SetFloat("g_NormalScale", p.NormalScale);
	}

	void MaterialEditor::applyPipelineOverrides(MaterialInstance& mat, const EditorMaterialOverrides& ov)
	{
		if (mat.GetPipelineType() != MATERIAL_PIPELINE_TYPE_GRAPHICS)
			return;

		IRenderPass* rp = nullptr;
		if (m_pRenderer && !ov.RenderPassName.empty())
			rp = m_pRenderer->GetRenderPassOrNull(ov.RenderPassName.c_str());

		mat.SetRenderPass(rp, ov.SubpassIndex);

		mat.SetCullMode(ov.CullMode);
		mat.SetFrontCounterClockwise(ov.FrontCounterClockwise);

		mat.SetDepthEnable(ov.DepthEnable);
		mat.SetDepthWriteEnable(ov.DepthWriteEnable);
		mat.SetDepthFunc(ov.DepthFunc);

		mat.SetTextureBindingMode(ov.TextureBindingMode);

		mat.SetLinearWrapSamplerName(ov.LinearWrapSamplerName);
		mat.SetLinearWrapSamplerDesc(ov.LinearWrapSamplerDesc);
	}

	void MaterialEditor::applyMaterialOverrides(MaterialInstance& mat, const EditorMaterialOverrides& ov)
	{
		// Factors
		{
			float bc[4] = { ov.BaseColor.x, ov.BaseColor.y, ov.BaseColor.z, ov.BaseColor.w };
			mat.SetFloat4("g_BaseColorFactor", bc);
		}

		mat.SetFloat("g_RoughnessFactor", ov.Roughness);
		mat.SetFloat("g_MetallicFactor", ov.Metallic);
		mat.SetFloat("g_OcclusionStrength", ov.Occlusion);

		{
			float ec[3] = { ov.EmissiveColor.x, ov.EmissiveColor.y, ov.EmissiveColor.z };
			mat.SetFloat3("g_EmissiveFactor", ec);
			mat.SetFloat("g_EmissiveIntensity", ov.EmissiveIntensity);
		}

		mat.SetFloat("g_AlphaCutoff", ov.AlphaCutoff);
		mat.SetFloat("g_NormalScale", ov.NormalScale);

		// Textures (runtime override)
		uint32 flags = ov.MaterialFlags;

		auto BindOrDefault = [&](const std::string& inPath, const char* varName, const RefCntAutoPtr<ITexture>& defaultTex, bool isSRGB, uint32 flagBit)
		{
			const std::string path = sanitizeFilePath(inPath);

			if (!path.empty())
			{
				if (ITextureView* srv = getOrCreateTextureSRV(path, isSRGB))
				{
					mat.SetTextureRuntimeView(varName, srv);
					flags |= flagBit;
					return;
				}
			}

			mat.SetTextureRuntimeView(varName, defaultTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			flags &= ~flagBit;
		};

		BindOrDefault(ov.BaseColorPath, "g_BaseColorTex", m_DefaultTextures.White, true, MAT_HAS_BASECOLOR);
		BindOrDefault(ov.NormalPath, "g_NormalTex", m_DefaultTextures.Normal, false, MAT_HAS_NORMAL);
		BindOrDefault(ov.MetallicRoughnessPath, "g_MetallicRoughnessTex", m_DefaultTextures.MetallicRoughness, false, MAT_HAS_MR);
		BindOrDefault(ov.AOPath, "g_AOTex", m_DefaultTextures.AO, false, MAT_HAS_AO);
		BindOrDefault(ov.EmissivePath, "g_EmissiveTex", m_DefaultTextures.Emissive, true, MAT_HAS_EMISSIVE);
		BindOrDefault(ov.HeightPath, "g_HeightTex", m_DefaultTextures.Black, false, MAT_HAS_HEIGHT);

		mat.SetUint("g_MaterialFlags", flags);

		// NOTE: when you swap textures often, this matters
		mat.MarkAllDirty();
	}

	std::vector<MaterialInstance> MaterialEditor::buildMaterialsForCpuMeshSlots(const StaticMeshAsset& cpuMesh)
	{
		MaterialTemplate* pTemplate = getOrCreateTemplate(getCurrentShaderPreset());
		ASSERT(pTemplate, "Template is null.");

		std::vector<MaterialInstance> materials;
		materials.reserve(cpuMesh.GetMaterialSlots().size());

		for (const MaterialInstanceAsset& matAsset : cpuMesh.GetMaterialSlots())
		{
			MaterialInstance mat = {};
			{
				const bool ok = mat.Initialize(pTemplate, "MaterialEditor Instance");
				ASSERT(ok, "MaterialInstance::Initialize failed.");
			}

			// Pipeline knobs default (from current UI overrides)
			applyPipelineOverrides(mat, m_Overrides);

			// Seed params from imported asset first
			seedFromImportedAsset(mat, matAsset);

			// Bind textures from imported asset (best-effort), else default
			uint32 materialFlags = 0;

			auto BindFromAssetOrDefault = [&](MATERIAL_TEXTURE_SLOT slot,
				const char* shaderVar,
				const RefCntAutoPtr<ITexture>& defaultTex,
				bool isSRGB,
				uint32 flagBit)
			{
				if (matAsset.GetTexture(slot).IsValid())
				{
					const std::string texPath = matAsset.GetTexture(slot).GetSourcePath();
					if (ITextureView* srv = getOrCreateTextureSRV(texPath, isSRGB))
					{
						mat.SetTextureRuntimeView(shaderVar, srv);
						materialFlags |= flagBit;
						return;
					}
				}

				mat.SetTextureRuntimeView(shaderVar, defaultTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			};

			BindFromAssetOrDefault(MATERIAL_TEX_ALBEDO, "g_BaseColorTex", m_DefaultTextures.White, true, MAT_HAS_BASECOLOR);
			BindFromAssetOrDefault(MATERIAL_TEX_NORMAL, "g_NormalTex", m_DefaultTextures.Normal, false, MAT_HAS_NORMAL);
			BindFromAssetOrDefault(MATERIAL_TEX_ORM, "g_MetallicRoughnessTex", m_DefaultTextures.MetallicRoughness, false, MAT_HAS_MR);
			BindFromAssetOrDefault(MATERIAL_TEX_AO, "g_AOTex", m_DefaultTextures.AO, false, MAT_HAS_AO);
			BindFromAssetOrDefault(MATERIAL_TEX_EMISSIVE, "g_EmissiveTex", m_DefaultTextures.Emissive, true, MAT_HAS_EMISSIVE);
			BindFromAssetOrDefault(MATERIAL_TEX_HEIGHT, "g_HeightTex", m_DefaultTextures.Black, false, MAT_HAS_HEIGHT);

			mat.SetUint("g_MaterialFlags", materialFlags);
			mat.MarkAllDirty();

			materials.push_back(static_cast<MaterialInstance&&>(mat));
		}

		return materials;
	}

	// ------------------------------------------------------------
	// UI helpers
	// ------------------------------------------------------------

	void MaterialEditor::drawTextureSlotUI(const char* label, std::string& path, const char* dialogFilter, const char* dialogTitle)
	{
		ImGui::TextUnformatted(label);

		ImGui::PushID(label);
		{
			char buf[1024] = {};
			if (!path.empty())
				strncpy_s(buf, path.c_str(), _TRUNCATE);

			ImGui::InputText("##Path", buf, sizeof(buf));
			path = buf;

			ImGui::SameLine();
			if (ImGui::Button("..."))
			{
				std::string picked;
				if (openFileDialog(picked, dialogFilter, dialogTitle))
					path = picked;
			}

			ImGui::SameLine();
			if (ImGui::Button("Clear"))
				path.clear();

			// Drag&drop: you can drop a file onto this widget
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH"))
				{
					const char* dropped = (const char*)payload->Data;
					if (dropped && dropped[0] != '\0')
						path = sanitizeFilePath(dropped);
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::PopID();
	}

	// ------------------------------------------------------------
	// Initialize
	// ------------------------------------------------------------

	void MaterialEditor::Initialize(const SampleInitInfo& InitInfo)
	{
		SampleBase::Initialize(InitInfo);

		m_pAssetManager = std::make_unique<AssetManager>();
		registerAssetLoaders();

		m_pRenderer = std::make_unique<Renderer>();

		m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(
			"C:/Dev/ShizenEngine/Shaders",
			&m_pShaderSourceFactory);

		RendererCreateInfo rendererCreateInfo = {};
		rendererCreateInfo.pEngineFactory = m_pEngineFactory;
		rendererCreateInfo.pShaderSourceFactory = m_pShaderSourceFactory;
		rendererCreateInfo.pDevice = m_pDevice;
		rendererCreateInfo.pImmediateContext = m_pImmediateContext;
		rendererCreateInfo.pDeferredContexts = m_pDeferredContexts;
		rendererCreateInfo.pSwapChain = m_pSwapChain;
		rendererCreateInfo.pImGui = m_pImGui;
		rendererCreateInfo.BackBufferWidth = m_pSwapChain->GetDesc().Width;
		rendererCreateInfo.BackBufferHeight = m_pSwapChain->GetDesc().Height;
		rendererCreateInfo.pAssetManager = m_pAssetManager.get();

		m_pRenderer->Initialize(rendererCreateInfo);

		m_pRenderScene = std::make_unique<RenderScene>();

		auto Create1x1Texture = [&](const char* name, uint32 rgba, RefCntAutoPtr<ITexture>& outTex) -> bool
		{
			TextureDesc desc = {};
			desc.Name = name;
			desc.Type = RESOURCE_DIM_TEX_2D;
			desc.Width = 1;
			desc.Height = 1;
			desc.MipLevels = 1;
			desc.Format = TEX_FORMAT_RGBA8_UNORM;
			desc.Usage = USAGE_IMMUTABLE;
			desc.BindFlags = BIND_SHADER_RESOURCE;

			TextureSubResData sub = {};
			sub.pData = &rgba;
			sub.Stride = sizeof(uint32);

			TextureData data = {};
			data.pSubResources = &sub;
			data.NumSubresources = 1;

			m_pDevice->CreateTexture(desc, &data, &outTex);
			return (outTex != nullptr);
		};

		Create1x1Texture("DefaultWhite1x1", PackRGBA8(255, 255, 255, 255), m_DefaultTextures.White);
		Create1x1Texture("DefaultBlack1x1", PackRGBA8(0, 0, 0, 255), m_DefaultTextures.Black);
		Create1x1Texture("DefaultNormal1x1", PackRGBA8(128, 128, 255, 255), m_DefaultTextures.Normal);
		Create1x1Texture("DefaultMR1x1", PackRGBA8(0, 255, 0, 255), m_DefaultTextures.MetallicRoughness);
		Create1x1Texture("DefaultAO1x1", PackRGBA8(255, 255, 255, 255), m_DefaultTextures.AO);
		Create1x1Texture("DefaultEmissive1x1", PackRGBA8(0, 0, 0, 255), m_DefaultTextures.Emissive);

		ensureResourceStateSRV(m_DefaultTextures.White.RawPtr());
		ensureResourceStateSRV(m_DefaultTextures.Black.RawPtr());
		ensureResourceStateSRV(m_DefaultTextures.Normal.RawPtr());
		ensureResourceStateSRV(m_DefaultTextures.MetallicRoughness.RawPtr());
		ensureResourceStateSRV(m_DefaultTextures.AO.RawPtr());
		ensureResourceStateSRV(m_DefaultTextures.Emissive.RawPtr());

		// Camera/ViewFamily
		m_Camera.SetProjAttribs(
			0.1f,
			100.0f,
			static_cast<float>(rendererCreateInfo.BackBufferWidth) / rendererCreateInfo.BackBufferHeight,
			PI / 4.0f,
			SURFACE_TRANSFORM_IDENTITY);

		m_ViewFamily.Views.clear();
		m_ViewFamily.Views.push_back({});

		(void)getOrCreateTemplate(getCurrentShaderPreset());

		// Preview models
		loadPreviewMesh(
			"C:/Dev/ShizenEngine/Assets/Basic/floor/FbxFloor.fbx",
			{ -2.0f, -0.5f, 3.0f }, { 0, 0, 0 }, { 1, 1, 1 }, false);

		loadPreviewMesh(
			"C:/Dev/ShizenEngine/Assets/Grass/chinese-fountain-grass/source/untitled/Grass.fbx",
			{ 0.0f, 0.0f, 3.0f }, { 0, 0, 0 }, { 1, 1, 1 }, true);

		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);

		m_SelectedObject = (m_Loaded.empty() ? -1 : 0);
		m_SelectedMaterialSlot = 0;

		// Default overrides
		m_Overrides = {};
		m_Overrides.RenderPassName = "GBuffer";
		m_Overrides.SubpassIndex = 0;

		// If possible, wire Win32 drag&drop here
		enableWindowDragDrop();
	}

	// ------------------------------------------------------------
	// Render / Update
	// ------------------------------------------------------------

	void MaterialEditor::Render()
	{
		m_ViewFamily.FrameIndex++;

		m_pRenderer->BeginFrame();
		m_pRenderer->Render(*m_pRenderScene, m_ViewFamily);
		m_pRenderer->EndFrame();
	}

	void MaterialEditor::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
	{
		SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);

		const float dt = static_cast<float>(ElapsedTime);
		const float currTime = static_cast<float>(CurrTime);

		m_Camera.Update(m_InputController, dt);

		m_ViewFamily.DeltaTime = dt;
		m_ViewFamily.CurrentTime = currTime;
		m_ViewFamily.Views[0].CameraPosition = m_Camera.GetPos();
		m_ViewFamily.Views[0].ViewMatrix = m_Camera.GetViewMatrix();
		m_ViewFamily.Views[0].ProjMatrix = m_Camera.GetProjMatrix();
		m_ViewFamily.Views[0].NearPlane = m_Camera.GetProjAttribs().NearClipPlane;
		m_ViewFamily.Views[0].FarPlane = m_Camera.GetProjAttribs().FarClipPlane;

		m_pRenderScene->UpdateLight(m_GlobalLightHandle, m_GlobalLight);
	}

	void MaterialEditor::ReleaseSwapChainBuffers()
	{
		SampleBase::ReleaseSwapChainBuffers();
		if (m_pRenderer)
			m_pRenderer->ReleaseSwapChainBuffers();
	}

	void MaterialEditor::WindowResize(uint32 Width, uint32 Height)
	{
		SampleBase::WindowResize(Width, Height);

		m_Camera.SetProjAttribs(
			m_Camera.GetProjAttribs().NearClipPlane,
			m_Camera.GetProjAttribs().FarClipPlane,
			static_cast<float>(Width) / Height,
			PI / 4.0f,
			SURFACE_TRANSFORM_IDENTITY);

		if (m_pRenderer)
			m_pRenderer->OnResize(Width, Height);
	}

	// ------------------------------------------------------------
	// UI
	// ------------------------------------------------------------

	void MaterialEditor::UpdateUI()
	{
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("MaterialEditor - Light", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::gizmo3D("##LightDirection", m_GlobalLight.Direction, ImGui::GetTextLineHeight() * 10);
			ImGui::ColorEdit3("##LightColor", reinterpret_cast<float*>(&m_GlobalLight.Color));
			ImGui::SliderFloat("Intensity", &m_GlobalLight.Intensity, 0.01f, 10.0f);
		}
		ImGui::End();

		ImGui::SetNextWindowPos(ImVec2(10, 220), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("MaterialEditor", nullptr))
		{
			// ------------------------------------------------------------
			// 1) Shader preset -> Template
			// ------------------------------------------------------------
			ImGui::TextUnformatted("1) Shader Preset -> Build Template");
			{
				const char* current = g_ShaderPresets[m_SelectedShaderPreset].Label;

				if (ImGui::BeginCombo("Shader Preset", current))
				{
					for (int i = 0; i < static_cast<int>(_countof(g_ShaderPresets)); ++i)
					{
						const bool isSelected = (i == m_SelectedShaderPreset);
						if (ImGui::Selectable(g_ShaderPresets[i].Label, isSelected))
						{
							m_SelectedShaderPreset = i;
							(void)getOrCreateTemplate(getCurrentShaderPreset());
							(void)rebuildAllMaterialsFromCurrentTemplate();
						}
						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}

			ImGui::Separator();

			// ------------------------------------------------------------
			// 2) Object/slot selection
			// ------------------------------------------------------------
			ImGui::TextUnformatted("2) Select Object / Slot");
			if (m_Loaded.empty())
			{
				ImGui::TextUnformatted("No preview objects loaded.");
				ImGui::End();
				return;
			}

			for (int i = 0; i < static_cast<int>(m_Loaded.size()); ++i)
			{
				const bool selected = (i == m_SelectedObject);
				const std::string label = "[" + std::to_string(i) + "] " + m_Loaded[(size_t)i].Path;

				if (ImGui::Selectable(label.c_str(), selected))
				{
					m_SelectedObject = i;
					m_SelectedMaterialSlot = 0;
				}
			}

			if (m_SelectedObject < 0 || m_SelectedObject >= static_cast<int>(m_Loaded.size()))
			{
				ImGui::TextUnformatted("Invalid selection.");
				ImGui::End();
				return;
			}

			LoadedMesh& sel = m_Loaded[(size_t)m_SelectedObject];
			if (!sel.MeshPtr)
			{
				ImGui::TextUnformatted("CPU mesh is not loaded.");
				ImGui::End();
				return;
			}

			const StaticMeshAsset* cpuMesh = sel.MeshPtr.Get();
			const int slotCount = static_cast<int>(cpuMesh->GetMaterialSlots().size());

			ImGui::Text("Material Slot: %d / %d", m_SelectedMaterialSlot, slotCount);
			ImGui::SliderInt("##Slot", &m_SelectedMaterialSlot, 0, (slotCount > 0 ? slotCount - 1 : 0));

			ImGui::Separator();

			// Access material
			if (sel.SceneObjectIndex < 0 || sel.SceneObjectIndex >= (int32)m_pRenderScene->GetObjects().size())
			{
				ImGui::TextUnformatted("Scene object index invalid.");
				ImGui::End();
				return;
			}

			auto& obj = m_pRenderScene->GetObjects()[(size_t)sel.SceneObjectIndex];
			if (m_SelectedMaterialSlot < 0 || m_SelectedMaterialSlot >= (int32)obj.Materials.size())
			{
				ImGui::TextUnformatted("Material slot invalid.");
				ImGui::End();
				return;
			}

			MaterialInstance& mat = obj.Materials[(size_t)m_SelectedMaterialSlot];

			// ------------------------------------------------------------
			// 3) Pipeline knobs (Set*)
			// ------------------------------------------------------------
			ImGui::TextUnformatted("3) Pipeline Overrides (Set*)");

			// RenderPass
			{
				int rpIndex = 0;
				for (int i = 0; i < (int)_countof(g_RenderPassNames); ++i)
				{
					if (m_Overrides.RenderPassName == g_RenderPassNames[i])
					{
						rpIndex = i;
						break;
					}
				}

				if (ImGui::Combo("RenderPass", &rpIndex, g_RenderPassNames, (int)_countof(g_RenderPassNames)))
				{
					m_Overrides.RenderPassName = g_RenderPassNames[rpIndex];
				}

				ImGui::InputInt("Subpass", (int*)&m_Overrides.SubpassIndex);
				if ((int)m_Overrides.SubpassIndex < 0) m_Overrides.SubpassIndex = 0;
			}

			// Cull/FrontCCW
			{
				int cull = (int)m_Overrides.CullMode - 1;
				const char* items[] = { "None", "Front", "Back" };
				if (ImGui::Combo("Cull", &cull, items, 3))
				{
					if (cull == 0) m_Overrides.CullMode = CULL_MODE_NONE;
					if (cull == 1) m_Overrides.CullMode = CULL_MODE_FRONT;
					if (cull == 2) m_Overrides.CullMode = CULL_MODE_BACK;
				}

				ImGui::Checkbox("FrontCCW", &m_Overrides.FrontCounterClockwise);
			}

			// Depth
			{
				ImGui::Checkbox("DepthEnable", &m_Overrides.DepthEnable);
				ImGui::Checkbox("DepthWrite", &m_Overrides.DepthWriteEnable);

				int df = 3;
				const char* dfs[] =
				{
					"NEVER","LESS","EQUAL","LEQUAL","GREATER","NOT_EQUAL","GEQUAL","ALWAYS"
				};

				auto ToIndex = [](COMPARISON_FUNCTION f) -> int
				{
					switch (f)
					{
					case COMPARISON_FUNC_NEVER:         return 0;
					case COMPARISON_FUNC_LESS:          return 1;
					case COMPARISON_FUNC_EQUAL:         return 2;
					case COMPARISON_FUNC_LESS_EQUAL:    return 3;
					case COMPARISON_FUNC_GREATER:       return 4;
					case COMPARISON_FUNC_NOT_EQUAL:     return 5;
					case COMPARISON_FUNC_GREATER_EQUAL: return 6;
					case COMPARISON_FUNC_ALWAYS:        return 7;
					default:                            return 3;
					}
				};

				auto FromIndex = [](int i) -> COMPARISON_FUNCTION
				{
					switch (i)
					{
					case 0: return COMPARISON_FUNC_NEVER;
					case 1: return COMPARISON_FUNC_LESS;
					case 2: return COMPARISON_FUNC_EQUAL;
					case 3: return COMPARISON_FUNC_LESS_EQUAL;
					case 4: return COMPARISON_FUNC_GREATER;
					case 5: return COMPARISON_FUNC_NOT_EQUAL;
					case 6: return COMPARISON_FUNC_GREATER_EQUAL;
					case 7: return COMPARISON_FUNC_ALWAYS;
					default:return COMPARISON_FUNC_LESS_EQUAL;
					}
				};

				df = ToIndex(m_Overrides.DepthFunc);
				if (ImGui::Combo("DepthFunc", &df, dfs, 8))
				{
					m_Overrides.DepthFunc = FromIndex(df);
				}
			}

			// Binding mode
			{
				int bm = (int)m_Overrides.TextureBindingMode;
				const char* bms[] = { "DYNAMIC", "MUTABLE" };
				if (ImGui::Combo("TextureBindMode", &bm, bms, 2))
				{
					m_Overrides.TextureBindingMode = (bm == 0) ? MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC : MATERIAL_TEXTURE_BINDING_MODE_MUTABLE;
				}
				ImGui::TextDisabled("%s", TexBindModeLabel(m_Overrides.TextureBindingMode));
			}

			ImGui::Separator();

			// ------------------------------------------------------------
			// 4) Material params + textures
			// ------------------------------------------------------------
			ImGui::TextUnformatted("4) Material Overrides (Set*)");

			ImGui::ColorEdit4("BaseColor", reinterpret_cast<float*>(&m_Overrides.BaseColor));
			ImGui::SliderFloat("Roughness", &m_Overrides.Roughness, 0.0f, 1.0f);
			ImGui::SliderFloat("Metallic", &m_Overrides.Metallic, 0.0f, 1.0f);
			ImGui::SliderFloat("Occlusion", &m_Overrides.Occlusion, 0.0f, 1.0f);

			ImGui::ColorEdit3("EmissiveColor", reinterpret_cast<float*>(&m_Overrides.EmissiveColor));
			ImGui::SliderFloat("EmissiveIntensity", &m_Overrides.EmissiveIntensity, 0.0f, 50.0f);

			ImGui::SliderFloat("AlphaCutoff", &m_Overrides.AlphaCutoff, 0.0f, 1.0f);
			ImGui::SliderFloat("NormalScale", &m_Overrides.NormalScale, 0.0f, 4.0f);

			ImGui::Separator();
			ImGui::TextUnformatted("Texture Overrides (runtime path)");

			drawTextureSlotUI("BaseColor (sRGB)", m_Overrides.BaseColorPath,
				"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
				"Pick BaseColor");
			drawTextureSlotUI("Normal (Linear)", m_Overrides.NormalPath,
				"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
				"Pick Normal");

			drawTextureSlotUI("MetallicRoughness (Linear)", m_Overrides.MetallicRoughnessPath,
				"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
				"Pick MetallicRoughness");

			drawTextureSlotUI("AO (Linear)", m_Overrides.AOPath,
				"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
				"Pick AO");

			drawTextureSlotUI("Emissive (sRGB)", m_Overrides.EmissivePath,
				"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
				"Pick Emissive");

			drawTextureSlotUI("Height (Linear)", m_Overrides.HeightPath,
				"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
				"Pick Height");

			ImGui::Separator();

			// ------------------------------------------------------------
			// 5) Apply / Clear / Rebuild
			// ------------------------------------------------------------
			if (ImGui::Button("Apply Overrides"))
			{
				// Pipeline + params
				applyPipelineOverrides(mat, m_Overrides);
				applyMaterialOverrides(mat, m_Overrides);

				// If your runtime rebuild path depends on PSO/SRB dirty flags,
				// MaterialRenderData should see these changes and rebuild as needed.
			}

			ImGui::SameLine();
			if (ImGui::Button("Clear Overrides"))
			{
				EditorMaterialOverrides defaults = {};
				defaults.RenderPassName = "GBuffer";
				defaults.SubpassIndex = 0;

				defaults.CullMode = CULL_MODE_BACK;
				defaults.FrontCounterClockwise = true;

				defaults.DepthEnable = true;
				defaults.DepthWriteEnable = true;
				defaults.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

				defaults.TextureBindingMode = MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC;

				defaults.LinearWrapSamplerName = "g_LinearWrapSampler";
				defaults.LinearWrapSamplerDesc =
				{
					FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
					TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
				};

				defaults.BaseColor = { 1, 1, 1, 1 };
				defaults.Roughness = 1.0f;
				defaults.Metallic = 0.0f;
				defaults.Occlusion = 1.0f;

				defaults.EmissiveColor = { 0, 0, 0 };
				defaults.EmissiveIntensity = 0.0f;

				defaults.AlphaCutoff = 0.5f;
				defaults.NormalScale = 1.0f;

				m_Overrides = defaults;
			}

			ImGui::SameLine();
			if (ImGui::Button("Rebuild Materials (Template -> Instances)"))
			{
				(void)rebuildAllMaterialsFromCurrentTemplate();
			}

			ImGui::Separator();

			// ------------------------------------------------------------
			// Drag&drop helper: show dropped files list (if wired)
			// ------------------------------------------------------------
			if (!m_DroppedFiles.empty())
			{
				ImGui::TextUnformatted("Dropped files:");
				for (size_t i = 0; i < m_DroppedFiles.size(); ++i)
				{
					ImGui::BulletText("%s", m_DroppedFiles[i].c_str());
				}
				ImGui::TextDisabled("Tip: drop a file on any texture path field to set it.");
			}
		}
		ImGui::End();
	}

} // namespace shz

