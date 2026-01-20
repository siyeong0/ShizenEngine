#include "MaterialEditor.h"

#include <iostream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/ImGuiUtils.hpp"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/TextureImporter.h"
#include "Engine/AssetRuntime/Common/AssetTypeTraits.h"

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

		static uint32 packRGBA8(uint8 r, uint8 g, uint8 b, uint8 a) noexcept
		{
			return (uint32(r) << 0) | (uint32(g) << 8) | (uint32(b) << 16) | (uint32(a) << 24);
		}

		// Preset tables (no ShaderPreset struct)
		static const char* g_PresetLabels[] =
		{
			"PBR GBuffer (GBuffer.vsh / GBuffer.psh)",
			"GBuffer Masked (GBufferMasked.vsh / GBufferMasked.psh)",
			"Custom (type paths)",
		};

		static const char* g_PresetVS[] =
		{
			"GBuffer.vsh",
			"GBufferMasked.vsh",
			nullptr, // custom
		};

		static const char* g_PresetPS[] =
		{
			"GBuffer.psh",
			"GBufferMasked.psh",
			nullptr, // custom
		};

		static const char* g_PresetVSEntry[] =
		{
			"main",
			"main",
			nullptr, // custom
		};

		static const char* g_PresetPSEntry[] =
		{
			"main",
			"main",
			nullptr, // custom
		};

		static const char* g_RenderPassNames[] =
		{
			"GBuffer",
			"Forward",
			"Shadow",
			"Post",
		};

		static const char* cullModeLabel(CULL_MODE m)
		{
			switch (m)
			{
			case CULL_MODE_NONE:  return "None";
			case CULL_MODE_FRONT: return "Front";
			case CULL_MODE_BACK:  return "Back";
			default:              return "Unknown";
			}
		}

		static const char* depthFuncLabel(COMPARISON_FUNCTION f)
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

		static const char* texBindModeLabel(MATERIAL_TEXTURE_BINDING_MODE m)
		{
			switch (m)
			{
			case MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC: return "DYNAMIC (Editor friendly)";
			case MATERIAL_TEXTURE_BINDING_MODE_MUTABLE: return "MUTABLE (No overwrite by default)";
			default: return "UNKNOWN";
			}
		}

		static void imGuiInputString(const char* label, std::string& s, size_t maxLen = 512)
		{
			std::vector<char> buf;
			buf.resize(maxLen);
			memset(buf.data(), 0, buf.size());

			if (!s.empty())
				strncpy_s(buf.data(), buf.size(), s.c_str(), _TRUNCATE);

			if (ImGui::InputText(label, buf.data(), buf.size()))
				s = buf.data();
		}

		static std::string makeTextureCacheKey(const std::string& path, bool isSRGB, bool flipVertically)
		{
			std::string k;
			k.reserve(path.size() + 32);
			k += path;
			k += "|SRGB:";
			k += (isSRGB ? '1' : '0');
			k += "|FLIPV:";
			k += (flipVertically ? '1' : '0');
			return k;
		}

		static bool readFloatFromOverride(const MaterialAsset::ValueOverride* v, float& outV)
		{
			if (!v || v->Type != MATERIAL_VALUE_TYPE_FLOAT || v->Data.size() != sizeof(float))
				return false;

			memcpy(&outV, v->Data.data(), sizeof(float));
			return true;
		}

		static bool readFloat3FromOverride(const MaterialAsset::ValueOverride* v, float outV[3])
		{
			if (!v || v->Type != MATERIAL_VALUE_TYPE_FLOAT3 || v->Data.size() != sizeof(float) * 3)
				return false;

			memcpy(outV, v->Data.data(), sizeof(float) * 3);
			return true;
		}

		static bool readFloat4FromOverride(const MaterialAsset::ValueOverride* v, float outV[4])
		{
			if (!v || v->Type != MATERIAL_VALUE_TYPE_FLOAT4 || v->Data.size() != sizeof(float) * 4)
				return false;

			memcpy(outV, v->Data.data(), sizeof(float) * 4);
			return true;
		}

		static bool readUintFromOverride(const MaterialAsset::ValueOverride* v, uint32& outV)
		{
			if (!v || v->Type != MATERIAL_VALUE_TYPE_UINT || v->Data.size() != sizeof(uint32))
				return false;

			memcpy(&outV, v->Data.data(), sizeof(uint32));
			return true;
		}
	}

	SampleBase* CreateSample()
	{
		return new MaterialEditor();
	}

	// ------------------------------------------------------------
// AssetManager integration helpers
// ------------------------------------------------------------

	void MaterialEditor::registerAssetLoaders()
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");

		// ------------------------------------------------------------
		// StaticMeshAsset loader
		// ------------------------------------------------------------
		m_pAssetManager->RegisterLoader(AssetTypeTraits<StaticMeshAsset>::TypeID, StaticMeshImporter{});

		// ------------------------------------------------------------
		// TextureAsset loader (minimum: keep SourcePath)
		// ------------------------------------------------------------
		m_pAssetManager->RegisterLoader(AssetTypeTraits<TextureAsset>::TypeID, TextureImporter{});
	}

	AssetRef<StaticMeshAsset> MaterialEditor::registerStaticMeshPath(const std::string& path)
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");

		return m_pAssetManager->RegisterAsset<StaticMeshAsset>(path);
	}

	AssetRef<TextureAsset> MaterialEditor::registerTexturePath(const std::string& path)
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");

		return m_pAssetManager->RegisterAsset<TextureAsset>(path);
	}

	AssetPtr<StaticMeshAsset> MaterialEditor::loadStaticMeshBlocking(AssetRef<StaticMeshAsset> ref, EAssetLoadFlags flags)
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");
		return m_pAssetManager->LoadBlocking(ref, flags);
	}

	AssetPtr<TextureAsset> MaterialEditor::loadTextureBlocking(AssetRef<TextureAsset> ref, EAssetLoadFlags flags)
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");
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
		m_DroppedFiles = paths;
	}

	// ------------------------------------------------------------
	// Template / Instance workflow
	// ------------------------------------------------------------

	std::string MaterialEditor::makeTemplateKeyFromInputs() const
	{
		std::string k;
		k.reserve(256);
		k += "VS:";   k += m_ShaderVS;
		k += "|VSE:"; k += m_VSEntry;
		k += "|PS:";  k += m_ShaderPS;
		k += "|PSE:"; k += m_PSEntry;
		return k;
	}

	MaterialTemplate* MaterialEditor::getOrCreateTemplateFromInputs()
	{
		// Basic validation
		if (m_ShaderVS.empty() || m_ShaderPS.empty() || m_VSEntry.empty() || m_PSEntry.empty())
			return nullptr;

		const std::string key = makeTemplateKeyFromInputs();

		auto it = m_TemplateCache.find(key);
		if (it != m_TemplateCache.end())
			return &it->second;

		MaterialTemplate tmpl = {};

		MaterialTemplateCreateInfo tci = {};
		tci.PipelineType = MATERIAL_PIPELINE_TYPE_GRAPHICS;
		tci.TemplateName = std::string("MaterialEditor: ") + key;

		tci.ShaderStages.clear();
		tci.ShaderStages.reserve(2);

		MaterialShaderStageDesc vs = {};
		vs.ShaderType = SHADER_TYPE_VERTEX;
		vs.DebugName = "MaterialEditor VS";
		vs.FilePath = m_ShaderVS.c_str();
		vs.EntryPoint = m_VSEntry.c_str();
		vs.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		vs.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
		vs.UseCombinedTextureSamplers = false; // ignored

		MaterialShaderStageDesc ps = {};
		ps.ShaderType = SHADER_TYPE_PIXEL;
		ps.DebugName = "MaterialEditor PS";
		ps.FilePath = m_ShaderPS.c_str();
		ps.EntryPoint = m_PSEntry.c_str();
		ps.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		ps.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
		ps.UseCombinedTextureSamplers = false; // ignored

		tci.ShaderStages.push_back(vs);
		tci.ShaderStages.push_back(ps);

		const bool ok = tmpl.Initialize(m_pDevice, m_pShaderSourceFactory, tci);
		ASSERT(ok, "MaterialTemplate::Initialize failed.");

		auto [insIt, _] = m_TemplateCache.emplace(key, static_cast<MaterialTemplate&&>(tmpl));
		return &insIt->second;
	}

	bool MaterialEditor::rebuildSelectedMaterialSlotFromCurrentTemplate()
	{
		if (!m_pRenderScene)
			return false;

		if (m_SelectedObject < 0 || m_SelectedObject >= (int32)m_Loaded.size())
			return false;

		LoadedMesh& sel = m_Loaded[(size_t)m_SelectedObject];
		if (sel.SceneObjectIndex < 0 || sel.SceneObjectIndex >= (int32)m_pRenderScene->GetObjects().size())
			return false;

		auto& obj = m_pRenderScene->GetObjects()[(size_t)sel.SceneObjectIndex];
		if (m_SelectedMaterialSlot < 0 || m_SelectedMaterialSlot >= (int32)obj.Materials.size())
			return false;

		MaterialTemplate* pTemplate = getOrCreateTemplateFromInputs();
		if (!pTemplate)
			return false;

		MaterialInstance newMat = {};
		{
			const bool ok = newMat.Initialize(pTemplate, "MaterialEditor Instance");
			ASSERT(ok, "MaterialInstance::Initialize failed.");
		}

		// Apply editor overrides onto the new instance.
		applyPipelineOverrides(newMat, m_Overrides);
		applyMaterialOverrides(newMat, m_Overrides);

		obj.Materials[(size_t)m_SelectedMaterialSlot] = static_cast<MaterialInstance&&>(newMat);
		return true;
	}

	bool MaterialEditor::rebuildSelectedObjectFromCurrentTemplate()
	{
		if (!m_pRenderScene)
			return false;

		if (m_SelectedObject < 0 || m_SelectedObject >= (int32)m_Loaded.size())
			return false;

		LoadedMesh& sel = m_Loaded[(size_t)m_SelectedObject];
		if (!sel.MeshPtr)
			return false;

		if (sel.SceneObjectIndex < 0 || sel.SceneObjectIndex >= (int32)m_pRenderScene->GetObjects().size())
			return false;

		MaterialTemplate* pTemplate = getOrCreateTemplateFromInputs();
		if (!pTemplate)
			return false;

		auto& obj = m_pRenderScene->GetObjects()[(size_t)sel.SceneObjectIndex];

		// Recreate each slot instance with the same template + editor overrides.
		for (size_t slot = 0; slot < obj.Materials.size(); ++slot)
		{
			MaterialInstance newMat = {};
			{
				const bool ok = newMat.Initialize(pTemplate, "MaterialEditor Instance");
				ASSERT(ok, "MaterialInstance::Initialize failed.");
			}

			applyPipelineOverrides(newMat, m_Overrides);
			applyMaterialOverrides(newMat, m_Overrides);

			obj.Materials[slot] = static_cast<MaterialInstance&&>(newMat);
		}

		return true;
	}

	bool MaterialEditor::rebuildAllMaterialsFromCurrentTemplate()
	{
		if (!m_pRenderScene)
			return false;

		MaterialTemplate* pTemplate = getOrCreateTemplateFromInputs();
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

			auto& obj = objects[(size_t)entry.SceneObjectIndex];

			for (size_t slot = 0; slot < obj.Materials.size(); ++slot)
			{
				MaterialInstance newMat = {};
				{
					const bool ok = newMat.Initialize(pTemplate, "MaterialEditor Instance");
					ASSERT(ok, "MaterialInstance::Initialize failed.");
				}

				applyPipelineOverrides(newMat, m_Overrides);
				applyMaterialOverrides(newMat, m_Overrides);

				obj.Materials[slot] = static_cast<MaterialInstance&&>(newMat);
			}
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

		// Initial creation uses current template + imported asset seeding.
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

	void MaterialEditor::seedFromImportedAsset(MaterialInstance& mat, const MaterialAsset& matAsset)
	{
		// NOTE:
		// New MaterialAsset stores overrides by name (ValueOverride/ResourceBinding).
		// We read known shader parameter names if present, otherwise leave defaults.

		// BaseColorFactor
		{
			float bc[4] = { 1, 1, 1, 1 };
			if (const auto* v = matAsset.FindValueOverride("g_BaseColorFactor"))
			{
				(void)readFloat4FromOverride(v, bc);
			}
			mat.SetFloat4("g_BaseColorFactor", bc);
		}

		// Roughness/Metallic/Occlusion
		{
			float rough = 1.0f;
			if (const auto* v = matAsset.FindValueOverride("g_RoughnessFactor"))
				(void)readFloatFromOverride(v, rough);
			mat.SetFloat("g_RoughnessFactor", rough);

			float metal = 0.0f;
			if (const auto* v = matAsset.FindValueOverride("g_MetallicFactor"))
				(void)readFloatFromOverride(v, metal);
			mat.SetFloat("g_MetallicFactor", metal);

			float occ = 1.0f;
			if (const auto* v = matAsset.FindValueOverride("g_OcclusionStrength"))
				(void)readFloatFromOverride(v, occ);
			mat.SetFloat("g_OcclusionStrength", occ);
		}

		// EmissiveFactor / EmissiveIntensity
		{
			float ec[3] = { 0, 0, 0 };
			if (const auto* v = matAsset.FindValueOverride("g_EmissiveFactor"))
				(void)readFloat3FromOverride(v, ec);
			mat.SetFloat3("g_EmissiveFactor", ec);

			float ei = 0.0f;
			if (const auto* v = matAsset.FindValueOverride("g_EmissiveIntensity"))
				(void)readFloatFromOverride(v, ei);
			mat.SetFloat("g_EmissiveIntensity", ei);
		}

		// AlphaCutoff / NormalScale
		{
			float ac = 0.5f;
			if (const auto* v = matAsset.FindValueOverride("g_AlphaCutoff"))
				(void)readFloatFromOverride(v, ac);
			mat.SetFloat("g_AlphaCutoff", ac);

			float ns = 1.0f;
			if (const auto* v = matAsset.FindValueOverride("g_NormalScale"))
				(void)readFloatFromOverride(v, ns);
			mat.SetFloat("g_NormalScale", ns);
		}

		// MaterialFlags (optional)
		{
			uint32 flags = 0;
			if (const auto* v = matAsset.FindValueOverride("g_MaterialFlags"))
				(void)readUintFromOverride(v, flags);
			mat.SetUint("g_MaterialFlags", flags);
		}
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

		auto BindOrDefault = [&](
			const std::string& inPath,
			bool bFlipV,
			const char* varName,
			const std::string& defaltPath,
			bool isSRGB, uint32 flagBit)
		{
			const std::string path = sanitizeFilePath(inPath);

			if (!path.empty())
			{
				AssetRef<TextureAsset> texRef = registerTexturePath(path);
				mat.SetTextureAssetRef(varName, texRef);
				flags |= flagBit;
				return;
			}
			AssetRef<TextureAsset> texRef = registerTexturePath(defaltPath);
			mat.SetTextureAssetRef(varName, texRef);
			flags &= ~flagBit;
		};

		const std::string defaultTexPath = "C:/Dev/ShizenEngine/Assets/Error.jpg";

		BindOrDefault(ov.BaseColorPath, ov.BaseColorFlipVertically, "g_BaseColorTex", defaultTexPath, true, MAT_HAS_BASECOLOR);
		BindOrDefault(ov.NormalPath, ov.NormalFlipVertically, "g_NormalTex", defaultTexPath, false, MAT_HAS_NORMAL);
		BindOrDefault(ov.MetallicRoughnessPath, ov.MetallicRoughnessFlipVertically, "g_MetallicRoughnessTex", defaultTexPath, false, MAT_HAS_MR);
		BindOrDefault(ov.AOPath, ov.AOFlipVertically, "g_AOTex", defaultTexPath, false, MAT_HAS_AO);
		BindOrDefault(ov.EmissivePath, ov.EmissiveFlipVertically, "g_EmissiveTex", defaultTexPath, true, MAT_HAS_EMISSIVE);
		BindOrDefault(ov.HeightPath, ov.HeightFlipVertically, "g_HeightTex", defaultTexPath, false, MAT_HAS_HEIGHT);

		mat.SetUint("g_MaterialFlags", flags);

		// NOTE: when you swap textures often, this matters
		mat.MarkAllDirty();
	}

	std::vector<MaterialInstance> MaterialEditor::buildMaterialsForCpuMeshSlots(const StaticMeshAsset& cpuMesh)
	{
		MaterialTemplate* pTemplate = getOrCreateTemplateFromInputs();
		ASSERT(pTemplate, "Template is null.");

		std::vector<MaterialInstance> materials;
		materials.reserve(cpuMesh.GetMaterialSlots().size());

		for (const MaterialAsset& matAsset : cpuMesh.GetMaterialSlots())
		{
			MaterialInstance mat = {};
			{
				const bool ok = mat.Initialize(pTemplate, "MaterialEditor Instance");
				ASSERT(ok, "MaterialInstance::Initialize failed.");
			}

			// Pipeline knobs default (from current UI overrides)
			applyPipelineOverrides(mat, m_Overrides);

			// Seed known numeric params from imported asset (new override model)
			seedFromImportedAsset(mat, matAsset);

			// Bind textures from imported asset (ResourceBinding by shader var name), else default.
			uint32 materialFlags = 0;

			auto BindFromAssetOrDefault = [&](
				const char* shaderVar,
				const std::string& defaultPath,
				bool isSRGB,
				uint32 flagBit,
				bool bFlipV)
			{
				if (const auto* rb = matAsset.FindResourceBinding(shaderVar))
				{
					// Otherwise use TextureRef -> load TextureAsset -> SourcePath -> create SRV
					if (rb->TextureRef)
					{
						mat.SetTextureAssetRef(shaderVar, rb->TextureRef);
						materialFlags |= flagBit;
						return;
					}
				}
				AssetRef<TextureAsset> texRef = registerTexturePath(defaultPath);
				mat.SetTextureAssetRef(shaderVar, texRef);
				materialFlags &= ~flagBit;
			};

			const std::string defaultTexPath = "C:/Dev/ShizenEngine/Assets/Error.jpg";

			BindFromAssetOrDefault("g_BaseColorTex", defaultTexPath, true, MAT_HAS_BASECOLOR, m_Overrides.BaseColorFlipVertically);
			BindFromAssetOrDefault("g_NormalTex", defaultTexPath, false, MAT_HAS_NORMAL, m_Overrides.NormalFlipVertically);
			BindFromAssetOrDefault("g_MetallicRoughnessTex", defaultTexPath, false, MAT_HAS_MR, m_Overrides.MetallicRoughnessFlipVertically);
			BindFromAssetOrDefault("g_AOTex", defaultTexPath, false, MAT_HAS_AO, m_Overrides.AOFlipVertically);
			BindFromAssetOrDefault("g_EmissiveTex", defaultTexPath, true, MAT_HAS_EMISSIVE, m_Overrides.EmissiveFlipVertically);
			BindFromAssetOrDefault("g_HeightTex", defaultTexPath, false, MAT_HAS_HEIGHT, m_Overrides.HeightFlipVertically);

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

		m_pDevice = m_pDevice; // SampleBase likely already has it (keep for clarity)
		m_pImmediateContext = m_pImmediateContext;

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

		// Camera/ViewFamily
		m_Camera.SetProjAttribs(
			0.1f,
			100.0f,
			static_cast<float>(rendererCreateInfo.BackBufferWidth) / rendererCreateInfo.BackBufferHeight,
			PI / 4.0f,
			SURFACE_TRANSFORM_IDENTITY);

		m_ViewFamily.Views.clear();
		m_ViewFamily.Views.push_back({});

		// Seed preset 0
		m_SelectedPresetIndex = 0;
		m_ShaderVS = g_PresetVS[0];
		m_ShaderPS = g_PresetPS[0];
		m_VSEntry = g_PresetVSEntry[0];
		m_PSEntry = g_PresetPSEntry[0];

		// Cache template once
		(void)getOrCreateTemplateFromInputs();

		// Preview models
		loadPreviewMesh(
			"C:/Dev/ShizenEngine/Assets/Basic/floor/FbxFloor.fbx",
			{ -2.0f, -0.5f, 3.0f }, { 0, 0, 0 }, { 1, 1, 1 }, false);

		//loadPreviewMesh(
		//	"C:/Dev/ShizenEngine/Assets/Grass/chinese-fountain-grass/source/untitled/Grass.fbx",
		//	{ 0.0f, 0.0f, 3.0f }, { 0, 0, 0 }, { 1, 1, 1 }, true);

		//loadPreviewMesh(
		//	"C:/Dev/ShizenEngine/Assets/Grass/grass-free-download/source/grass.fbx",
		//	{ 0.0f, -0.5f, 3.0f }, { 0, 0, 0 }, { 1, 1, 1 }, true);

		loadPreviewMesh(
			"C:/Dev/ShizenEngine/Assets/Basic/DamagedHelmet/glTF/DamagedHelmet.gltf",
			{ 0.0f, 0.0f, 3.0f }, { 0, 0, 0 }, { 1, 1, 1 }, true);

		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);

		m_SelectedObject = (m_Loaded.empty() ? -1 : 0);
		m_SelectedMaterialSlot = 0;

		// Default overrides
		m_Overrides = {};
		m_Overrides.RenderPassName = "GBuffer";
		m_Overrides.SubpassIndex = 0;

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

		if (!m_ViewFamily.Views.empty())
		{
			m_ViewFamily.Views[0].CameraPosition = m_Camera.GetPos();
			m_ViewFamily.Views[0].ViewMatrix = m_Camera.GetViewMatrix();
			m_ViewFamily.Views[0].ProjMatrix = m_Camera.GetProjMatrix();
			m_ViewFamily.Views[0].NearPlane = m_Camera.GetProjAttribs().NearClipPlane;
			m_ViewFamily.Views[0].FarPlane = m_Camera.GetProjAttribs().FarClipPlane;
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

		if (!ImGui::Begin("MaterialEditor", nullptr))
		{
			ImGui::End();
			return;
		}

		// ------------------------------------------------------------
		// 1) Shader inputs -> Template (cache only)
		// ------------------------------------------------------------
		ImGui::TextUnformatted("1) Shader Inputs -> Build Template (cache)");
		{
			const char* current = g_PresetLabels[m_SelectedPresetIndex];

			if (ImGui::BeginCombo("Preset", current))
			{
				for (int i = 0; i < (int)_countof(g_PresetLabels); ++i)
				{
					const bool isSelected = (i == m_SelectedPresetIndex);
					if (ImGui::Selectable(g_PresetLabels[i], isSelected))
					{
						m_SelectedPresetIndex = i;

						// Apply non-custom preset to input fields (do NOT auto-rebuild materials)
						if (g_PresetVS[i] && g_PresetPS[i] && g_PresetVSEntry[i] && g_PresetPSEntry[i])
						{
							m_ShaderVS = g_PresetVS[i];
							m_ShaderPS = g_PresetPS[i];
							m_VSEntry = g_PresetVSEntry[i];
							m_PSEntry = g_PresetPSEntry[i];
						}

						(void)getOrCreateTemplateFromInputs();
					}
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::SeparatorText("Shader Paths / Entry Points");
			imGuiInputString("VS Path", m_ShaderVS);
			imGuiInputString("PS Path", m_ShaderPS);
			imGuiInputString("VS Entry", m_VSEntry);
			imGuiInputString("PS Entry", m_PSEntry);

			ImGui::TextDisabled("Combined samplers: ignored");

			if (ImGui::Button("Build/Update Template"))
			{
				(void)getOrCreateTemplateFromInputs();
			}

			ImGui::SameLine();
			if (ImGui::Button("Apply Template -> Selected Slot"))
			{
				(void)getOrCreateTemplateFromInputs();
				(void)rebuildSelectedMaterialSlotFromCurrentTemplate();
			}

			ImGui::SameLine();
			if (ImGui::Button("Apply Template -> Selected Object"))
			{
				(void)getOrCreateTemplateFromInputs();
				(void)rebuildSelectedObjectFromCurrentTemplate();
			}

			ImGui::SameLine();
			if (ImGui::Button("Apply Template -> All"))
			{
				(void)getOrCreateTemplateFromInputs();
				(void)rebuildAllMaterialsFromCurrentTemplate();
			}
		}

		ImGui::Separator();

		// ------------------------------------------------------------
		// 2) Object/slot selection
		// ------------------------------------------------------------
		ImGui::TextUnformatted("2) Select Object / Slot");
		if (m_Loaded.empty() || !m_pRenderScene)
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
			int cull = 2;
			if (m_Overrides.CullMode == CULL_MODE_NONE)  cull = 0;
			if (m_Overrides.CullMode == CULL_MODE_FRONT) cull = 1;
			if (m_Overrides.CullMode == CULL_MODE_BACK)  cull = 2;

			const char* items[] = { "None", "Front", "Back" };
			if (ImGui::Combo("Cull", &cull, items, 3))
			{
				if (cull == 0) m_Overrides.CullMode = CULL_MODE_NONE;
				if (cull == 1) m_Overrides.CullMode = CULL_MODE_FRONT;
				if (cull == 2) m_Overrides.CullMode = CULL_MODE_BACK;
			}

			ImGui::Checkbox("FrontCCW", &m_Overrides.FrontCounterClockwise);
			ImGui::TextDisabled("Cull: %s", cullModeLabel(m_Overrides.CullMode));
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

			ImGui::TextDisabled("DepthFunc: %s", depthFuncLabel(m_Overrides.DepthFunc));
		}

		// Binding mode
		{
			int bm = (m_Overrides.TextureBindingMode == MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC) ? 0 : 1;
			const char* bms[] = { "DYNAMIC", "MUTABLE" };
			if (ImGui::Combo("TextureBindMode", &bm, bms, 2))
			{
				m_Overrides.TextureBindingMode = (bm == 0) ? MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC : MATERIAL_TEXTURE_BINDING_MODE_MUTABLE;
			}
			ImGui::TextDisabled("%s", texBindModeLabel(m_Overrides.TextureBindingMode));
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
		ImGui::Checkbox("FlipV##BaseColor", &m_Overrides.BaseColorFlipVertically);

		drawTextureSlotUI("Normal (Linear)", m_Overrides.NormalPath,
			"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
			"Pick Normal");
		ImGui::Checkbox("FlipV##Normal", &m_Overrides.NormalFlipVertically);

		drawTextureSlotUI("MetallicRoughness (Linear)", m_Overrides.MetallicRoughnessPath,
			"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
			"Pick MetallicRoughness");
		ImGui::Checkbox("FlipV##MetallicRoughness", &m_Overrides.MetallicRoughnessFlipVertically);

		drawTextureSlotUI("AO (Linear)", m_Overrides.AOPath,
			"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
			"Pick AO");
		ImGui::Checkbox("FlipV##AO", &m_Overrides.AOFlipVertically);

		drawTextureSlotUI("Emissive (sRGB)", m_Overrides.EmissivePath,
			"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
			"Pick Emissive");
		ImGui::Checkbox("FlipV##Emissive", &m_Overrides.EmissiveFlipVertically);

		drawTextureSlotUI("Height (Linear)", m_Overrides.HeightPath,
			"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.dds;*.hdr\0All Files\0*.*\0",
			"Pick Height");
		ImGui::Checkbox("FlipV##Height", &m_Overrides.HeightFlipVertically);

		ImGui::Separator();

		// ------------------------------------------------------------
		// 5) Apply / Clear
		// ------------------------------------------------------------
		if (ImGui::Button("Apply Overrides"))
		{
			applyPipelineOverrides(mat, m_Overrides);
			applyMaterialOverrides(mat, m_Overrides);
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

			defaults.BaseColorFlipVertically = false;
			defaults.NormalFlipVertically = false;
			defaults.MetallicRoughnessFlipVertically = false;
			defaults.AOFlipVertically = false;
			defaults.EmissiveFlipVertically = false;
			defaults.HeightFlipVertically = false;


			m_Overrides = defaults;
		}

		ImGui::Separator();

		// ------------------------------------------------------------
		// Drag&drop helper
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

		ImGui::End();
	}

} // namespace shz
