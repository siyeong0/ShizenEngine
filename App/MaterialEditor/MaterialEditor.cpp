// ============================================================================
// MaterialEditor.cpp
// ============================================================================

#include "MaterialEditor.h"

#include <algorithm>
#include <cstring>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/imGuIZMO.h"

// Importers
#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/TextureImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/MaterialImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/AssimpImporter.h"

#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshExporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/MaterialExporter.h"

namespace shz
{
	namespace
	{
#include "Shaders/HLSL_Structures.hlsli"

		static constexpr const char* kShaderRoot = "C:/Dev/ShizenEngine/Shaders";

		// -----------------------------
		// Safe std::string InputText
		// -----------------------------
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

		static const char* BlendModeLabel(MATERIAL_BLEND_MODE m)
		{
			switch (m)
			{
			case MATERIAL_BLEND_MODE_OPAQUE:       return "OPAQUE";
			case MATERIAL_BLEND_MODE_MASKED:       return "MASKED";
			case MATERIAL_BLEND_MODE_TRANSLUCENT:  return "TRANSLUCENT";
			case MATERIAL_BLEND_MODE_ADDITIVE:     return "ADDITIVE";
			case MATERIAL_BLEND_MODE_PREMULTIPLIED:return "PREMULTIPLIED";
			default:                               return "UNKNOWN";
			}
		}

		static bool IsColorNameLike(const std::string& name)
		{
			return (name.find("Color") != std::string::npos) ||
				(name.find("Albedo") != std::string::npos) ||
				(name.find("BaseColor") != std::string::npos);
		}

		static float ComputeFitToUnitCubeUniformScale(const shz::StaticMeshAsset& mesh, float unitSize)
		{
			const Box& bounds = mesh.GetBounds();
			float3 bmin = bounds.Min;
			float3 bmax = bounds.Max;

			const float3 extent = bmax - bmin;
			const float maxDim = std::max(extent.x, std::max(extent.y, extent.z));
			if (maxDim <= 0.000001f)
				return 1.0f;

			return unitSize / maxDim;
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
	} // namespace

	SampleBase* CreateSample()
	{
		return new MaterialEditor();
	}

	void MaterialEditor::clearTemplateCache()
	{
		// mark all slot caches dirty (reflection may change)
		for (auto& kv : m_SlotUi)
			kv.second.Dirty = true;
	}

	// ------------------------------------------------------------
	// Main object access
	// ------------------------------------------------------------

	RenderScene::RenderObject* MaterialEditor::getMainRenderObjectOrNull()
	{
		if (!m_pRenderScene)
			return nullptr;

		if (!m_Main.ObjectId.IsValid())
			return nullptr;

		// NOTE:
		// Your RenderScene likely has a "GetObject(handle)" API.
		// If not, add one. Below is a placeholder pattern you should adapt.
		return m_pRenderScene->GetObjectOrNull(m_Main.ObjectId);
	}

	// ------------------------------------------------------------
	// Load / rebuild flow
	// ------------------------------------------------------------

	static inline std::string ToLowerCopy(std::string s)
	{
		for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

	bool MaterialEditor::loadOrReplaceMainObject(
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

		// Remove old
		if (m_Main.ObjectId.IsValid())
		{
			m_pRenderScene->RemoveObject(m_Main.ObjectId);
			m_Main.ObjectId = {};
		}

		if (m_Main.ImportedCpuMesh != nullptr)
		{
			m_Main.MeshRD = {};
		}

		m_SelectedSlot = 0;
		m_Main = {}; // reset state

		m_Main.Path = path;
		m_Main.Position = position;
		m_Main.Rotation = rotation;
		m_Main.Scale = scale;
		m_Main.bCastShadow = bCastShadow;

		StaticMeshAsset* cpu = nullptr;

		// ------------------------------------------------------------
		// 1) Native mesh: *.shzmesh.json -> StaticMeshAsset
		// ------------------------------------------------------------
		if (IsShzMeshJsonPath(m_Main.Path))
		{
			m_Main.MeshRef = m_pAssetManager->RegisterAsset<StaticMeshAsset>(m_Main.Path);
			m_Main.MeshPtr = m_pAssetManager->LoadBlocking(m_Main.MeshRef);

			cpu = m_Main.MeshPtr.Get();
			m_Main.ImportedCpuMesh = cpu;

			if (!cpu)
				return false;
		}
		// ------------------------------------------------------------
		// 2) Imported mesh: fbx/gltf/glb/... -> AssimpAsset -> BuildStaticMeshAsset
		// ------------------------------------------------------------
		else
		{
			m_Main.AssimpRef = m_pAssetManager->RegisterAsset<AssimpAsset>(m_Main.Path);
			m_Main.AssimpPtr = m_pAssetManager->LoadBlocking(m_Main.AssimpRef);

			const AssimpAsset* assimp = m_Main.AssimpPtr.Get();
			if (!assimp)
				return false;

			m_Main.ImportedCpuMesh = new StaticMeshAsset();
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
			ASSERT(cpu, "Imported CPU mesh is null.");
		}

		if (m_bFitToUnitCube && cpu)
		{
			const float s = ComputeFitToUnitCubeUniformScale(*cpu, m_FitUnitCubeSize);
			m_Main.Scale = m_Main.Scale * float3(s, s, s); // uniform
		}

		// ------------------------------------------------------------
		// Authoring policy: default template names (TemplateKey X, TemplateName O)
		// - default: DefaultLit
		// - if BlendMode == MASKED => DefaultLitMasked
		// ------------------------------------------------------------
		for (uint32 i = 0; i < cpu->GetMaterialSlotCount(); ++i)
		{
			MaterialAsset& mat = cpu->GetMaterialSlot(i);

			if (mat.GetTemplateName().empty())
			{
				const MATERIAL_BLEND_MODE bm = mat.GetOptions().BlendMode;
				mat.SetTemplateName((bm == MATERIAL_BLEND_MODE_MASKED) ? "DefaultLitMasked" : "DefaultLit");
			}
		}

		// Build GPU render data
		m_Main.MeshRD = m_pRenderer->CreateStaticMesh(*cpu, m_Main.RebuildKey++, "MaterialEditor Main Mesh");

		RenderScene::RenderObject obj = {};
		obj.Mesh = m_Main.MeshRD;
		obj.Transform = Matrix4x4::TRS(m_Main.Position, m_Main.Rotation, m_Main.Scale);
		obj.bCastShadow = m_Main.bCastShadow;

		m_Main.ObjectId = m_pRenderScene->AddObject(std::move(obj));
		ASSERT(m_Main.ObjectId.IsValid(), "Failed to add RenderObject.");

		return true;
	}

	bool MaterialEditor::rebuildMainMeshRenderData()
	{
		ASSERT(m_pRenderer, "Renderer is null.");
		ASSERT(m_pRenderScene, "RenderScene is null.");

		StaticMeshAsset* cpu = m_Main.ImportedCpuMesh;
		if (!cpu)
			return false;

		RenderScene::RenderObject* obj = getMainRenderObjectOrNull();
		if (!obj)
			return false;

		m_Main.MeshRD = m_pRenderer->CreateStaticMesh(*cpu, m_Main.RebuildKey++, "MaterialEditor Main Mesh (Rebuild)");
		obj->Mesh = m_Main.MeshRD;

		return true;
	}

	// ------------------------------------------------------------
	// Slot cache
	// ------------------------------------------------------------

	MaterialEditor::MaterialUiCache& MaterialEditor::getOrCreateSlotCache(uint32 slotIndex)
	{
		auto it = m_SlotUi.find(slotIndex);
		if (it != m_SlotUi.end())
			return it->second;

		MaterialUiCache cache = {};
		cache.Dirty = true;

		auto [insIt, _] = m_SlotUi.emplace(slotIndex, std::move(cache));
		return insIt->second;
	}

	void MaterialEditor::syncCacheFromMaterialAsset(MaterialUiCache& cache, const MaterialAsset& mat, const MaterialTemplate& tmpl)
	{
		cache.TemplateName = mat.GetTemplateName().empty() ? "DefaultLit" : mat.GetTemplateName();
		cache.RenderPassName = mat.GetRenderPassName();

		const auto& opt = mat.GetOptions();

		cache.BlendMode = opt.BlendMode;

		cache.CullMode = opt.CullMode;
		cache.FrontCounterClockwise = opt.FrontCounterClockwise;

		cache.DepthEnable = opt.DepthEnable;
		cache.DepthWriteEnable = opt.DepthWriteEnable;
		cache.DepthFunc = opt.DepthFunc;

		cache.TextureBindingMode = opt.TextureBindingMode;

		cache.LinearWrapSamplerName = opt.LinearWrapSamplerName;
		cache.LinearWrapSamplerDesc = opt.LinearWrapSamplerDesc;

		cache.bTwoSided = opt.bTwoSided;
		cache.bCastShadow = opt.bCastShadow;

		// Value bytes: build reflection list, fill from overrides if exists, else zero
		cache.ValueBytes.clear();
		for (uint32 i = 0; i < tmpl.GetValueParamCount(); ++i)
		{
			const MaterialValueParamDesc& desc = tmpl.GetValueParam(i);
			if (desc.Name.empty())
				continue;

			uint32 sz = desc.ByteSize != 0 ? desc.ByteSize : ValueTypeByteSize(desc.Type);
			if (sz == 0)
				continue;

			std::vector<uint8> bytes(sz);
			std::memset(bytes.data(), 0, bytes.size());

			if (const MaterialAsset::ValueOverride* ov = mat.FindValueOverride(desc.Name.c_str()))
			{
				const size_t copySz = std::min<size_t>(ov->Data.size(), bytes.size());
				if (copySz > 0)
					std::memcpy(bytes.data(), ov->Data.data(), copySz);
			}

			cache.ValueBytes.emplace(desc.Name, std::move(bytes));
		}

		// Resource paths: keep strings (authoring). If you have AssetRef->path query, wire it here.
		cache.TexturePaths.clear();

		for (uint32 i = 0; i < tmpl.GetResourceCount(); ++i)
		{
			const MaterialResourceDesc& res = tmpl.GetResource(i);
			if (res.Name.empty())
				continue;
			if (!IsTextureType(res.Type))
				continue;

			// 항상 key를 만들고, 기본은 empty
			std::string& outPath = cache.TexturePaths[res.Name];
			outPath.clear();

			const MaterialAsset::ResourceBinding* rb = mat.FindResourceBinding(res.Name.c_str());
			if (!rb)
				continue;

			if (rb->TextureRef.IsValid())
			{
				outPath = SanitizeFilePath(rb->TextureRef.GetSourcePath());
			}
		}
	}

	void MaterialEditor::applyCacheToMaterialAsset(MaterialAsset& mat, const MaterialUiCache& cache, const MaterialTemplate& tmpl)
	{
		// Metadata
		mat.SetTemplateName(cache.TemplateName.empty() ? "DefaultLit" : cache.TemplateName);
		mat.SetRenderPassName(cache.RenderPassName);

		// Options
		mat.SetBlendMode(cache.BlendMode);

		mat.SetCullMode(cache.CullMode);
		mat.SetFrontCounterClockwise(cache.FrontCounterClockwise);

		mat.SetDepthEnable(cache.DepthEnable);
		mat.SetDepthWriteEnable(cache.DepthWriteEnable);
		mat.SetDepthFunc(cache.DepthFunc);

		mat.SetTextureBindingMode(cache.TextureBindingMode);

		mat.SetLinearWrapSamplerName(cache.LinearWrapSamplerName);
		mat.SetLinearWrapSamplerDesc(cache.LinearWrapSamplerDesc);

		mat.SetTwoSided(cache.bTwoSided);
		mat.SetCastShadow(cache.bCastShadow);

		// Values: set overrides (only what template exposes)
		for (uint32 i = 0; i < tmpl.GetValueParamCount(); ++i)
		{
			const MaterialValueParamDesc& desc = tmpl.GetValueParam(i);
			if (desc.Name.empty())
				continue;

			auto it = cache.ValueBytes.find(desc.Name);
			if (it == cache.ValueBytes.end())
				continue;

			const auto& bytes = it->second;
			if (bytes.empty())
				continue;

			const uint32 sz = (uint32)bytes.size();
			(void)mat.SetRaw(desc.Name.c_str(), desc.Type, bytes.data(), sz, /*stableId*/0);
		}

		// Resources: set texture AssetRef from paths
		// NOTE: MaterialAsset stores AssetRef<TextureAsset>, so we register by path.
		for (uint32 i = 0; i < tmpl.GetResourceCount(); ++i)
		{
			const MaterialResourceDesc& res = tmpl.GetResource(i);
			if (res.Name.empty())
				continue;

			if (!IsTextureType(res.Type))
				continue;

			auto itPath = cache.TexturePaths.find(res.Name);
			if (itPath == cache.TexturePaths.end())
				continue;

			const std::string p = SanitizeFilePath(itPath->second);
			if (p.empty())
			{
				(void)mat.RemoveResourceBinding(res.Name.c_str());
				continue;
			}

			ASSERT(m_pAssetManager, "AssetManager is null.");
			const AssetRef<TextureAsset> texRef = m_pAssetManager->RegisterAsset<TextureAsset>(p);

			(void)mat.SetTextureAssetRef(
				res.Name.c_str(),
				res.Type,
				texRef,
				/*stableId*/0);

			// ------------------------------------------------------------
			// MaterialFlags (based on texture bindings)
			// ------------------------------------------------------------
			{
				uint32 flags = 0;

				// 텍스처 변수명은 HLSL에서 실제 쓰는 이름으로 맞춰야 함.
				// (아래는 너가 말한 g_BaseColorTex 기준)
				if (HasTexturePath(cache.TexturePaths, "g_BaseColorTex"))          flags |= MAT_HAS_BASECOLOR;
				if (HasTexturePath(cache.TexturePaths, "g_NormalTex"))             flags |= MAT_HAS_NORMAL;

				// MR은 프로젝트마다 이름이 흔들리니 둘 중 하나라도 있으면 ON
				if (HasTexturePath(cache.TexturePaths, "g_MRTex") ||
					HasTexturePath(cache.TexturePaths, "g_MetallicRoughnessTex"))   flags |= MAT_HAS_MR;

				if (HasTexturePath(cache.TexturePaths, "g_AOTex"))                 flags |= MAT_HAS_AO;
				if (HasTexturePath(cache.TexturePaths, "g_EmissiveTex"))           flags |= MAT_HAS_EMISSIVE;
				if (HasTexturePath(cache.TexturePaths, "g_HeightTex"))             flags |= MAT_HAS_HEIGHT;

				if (const char* flagsName = FindMaterialFlagsParamName(tmpl))
				{
					(void)mat.SetUint(flagsName, flags, /*stableId*/0);
				}
			}
		}
	}

	bool MaterialEditor::rebuildMainSaveObjectFromScene(std::string* outError)
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

		if (!m_pRenderer)
		{
			if (outError) *outError = "Renderer is null.";
			return false;
		}

		// 1) CPU Mesh를 통째로 복사해서 "Save용 baked mesh"를 만든다.
		StaticMeshAsset baked = *m_Main.ImportedCpuMesh;

		// 2) 현재 UI cache 상태를 baked mesh의 MaterialSlot에 굽는다.
		const uint32 slotCount = baked.GetMaterialSlotCount();
		for (uint32 slot = 0; slot < slotCount; ++slot)
		{
			MaterialAsset& mat = baked.GetMaterialSlot(slot);

			MaterialUiCache& cache = getOrCreateSlotCache(slot);

			// 현재 slot의 template로 적용해야 하므로, 캐시의 TemplateName을 우선 사용
			const std::string tmplName = cache.TemplateName.empty()
				? (mat.GetTemplateName().empty() ? "DefaultLit" : mat.GetTemplateName())
				: cache.TemplateName;

			const MaterialTemplate& tmpl = m_pRenderer->GetMaterialTemplate(tmplName);

			applyCacheToMaterialAsset(mat, cache, tmpl);

			// template name도 강제
			mat.SetTemplateName(tmplName);
		}

		// 3) Exporter는 AssetObject*를 받으니 TypedAssetObject로 감싼다.
		m_pMainBuiltObjForSave = std::make_unique<TypedAssetObject<StaticMeshAsset>>(std::move(baked));
		return true;
	}

	bool MaterialEditor::saveMainObject(const std::string& outPath, EAssetSaveFlags /*flags*/, std::string* outError)
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

		// Save 직전에 "현재 UI 상태"를 baked mesh에 반영
		{
			std::string err;
			if (!rebuildMainSaveObjectFromScene(&err))
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

		StaticMeshAssetExporter exporter = {};

		AssetMeta meta = {};
		meta.TypeID = AssetTypeTraits<StaticMeshAsset>::TypeID;
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
			if (outError) *outError = err.empty() ? "StaticMeshAssetExporter failed." : err;
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
		m_pAssetManager->RegisterImporter(AssetTypeTraits<StaticMeshAsset>::TypeID, StaticMeshAssetImporter{});
		m_pAssetManager->RegisterImporter(AssetTypeTraits<TextureAsset>::TypeID, TextureImporter{});
		m_pAssetManager->RegisterImporter(AssetTypeTraits<MaterialAsset>::TypeID, MaterialAssetImporter{});
		m_pAssetManager->RegisterImporter(AssetTypeTraits<AssimpAsset>::TypeID, AssimpImporter{});

		m_pAssetManager->RegisterExporter(AssetTypeTraits<StaticMeshAsset>::TypeID, StaticMeshAssetExporter{});
		m_pAssetManager->RegisterExporter(AssetTypeTraits<MaterialAsset>::TypeID, MaterialAssetExporter{});

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

		// Light (optional default)
		m_GlobalLight.Direction = float3(0.4f, -1.0f, 0.3f);
		m_GlobalLight.Color = float3(1.0f, 1.0f, 1.0f);
		m_GlobalLight.Intensity = 2.0f;
		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);

		// Load floor object
		AssetRef<AssimpAsset> floorRef = m_pAssetManager->RegisterAsset<AssimpAsset>(m_FloorMeshPath);
		AssetPtr<AssimpAsset> floorPtr = m_pAssetManager->LoadBlocking(floorRef);
		StaticMeshAsset cpuFloorMesh;
		BuildStaticMeshAsset(
			*floorPtr,
			&cpuFloorMesh,
			AssimpImportSettings{},
			nullptr,
			m_pAssetManager.get());
		RenderScene::RenderObject floorObj = {};
		floorObj.Mesh = m_pRenderer->CreateStaticMesh(cpuFloorMesh);
		floorObj.Transform = Matrix4x4::TRS(
			{ 0.0f, -1.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ 10.0f, 1.0f, 10.0f });
		floorObj.bCastShadow = true;
		m_Floor = m_pRenderScene->AddObject(std::move(floorObj));

		// Load default main object
		(void)loadOrReplaceMainObject(
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
		uiDockspace();
		uiScenePanel();
		uiViewportPanel();
		uiMaterialPanel();
		uiStatsPanel();
	}

	void MaterialEditor::uiDockspace()
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
				if (ImGui::BeginMenu("Templates"))
				{
					if (ImGui::MenuItem("Clear Template Cache"))
						clearTemplateCache();

					// reload defaults quickly
					if (ImGui::MenuItem("Reload DefaultLit / DefaultLitMasked"))
					{
						clearTemplateCache();
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenuBar();
			}
		}
		ImGui::End();
	}

	void MaterialEditor::uiScenePanel()
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
		ImGui::Checkbox("Fit To Unit Cube (Uniform)", &m_bFitToUnitCube);
		ImGui::DragFloat("Unit Cube Size", &m_FitUnitCubeSize, 0.01f, 0.01f, 100.0f);

		ImGui::DragFloat3("Position", reinterpret_cast<float*>(&m_Main.Position), 0.01f);
		ImGui::DragFloat3("Rotation", reinterpret_cast<float*>(&m_Main.Rotation), 0.5f);
		ImGui::DragFloat3("Scale", reinterpret_cast<float*>(&m_Main.Scale), 0.01f);

		ImGui::Checkbox("Cast Shadow (Object)", &m_Main.bCastShadow);

		if (ImGui::Button("Load / Replace"))
		{
			const std::string p = SanitizeFilePath(m_MainMeshPath);
			if (!p.empty())
			{
				(void)loadOrReplaceMainObject(
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
			const bool ok = saveMainObject(
				m_MainMeshSavePath,
				EAssetSaveFlags::None,
				&err);

			if (!ok)
			{
				ASSERT(false, err.empty() ? "Save failed." : err.c_str());
			}
		}

		// Apply object transform live
		if (m_Main.ImportedCpuMesh != nullptr)
		{
			if (RenderScene::RenderObject* obj = getMainRenderObjectOrNull())
			{
				obj->Transform = Matrix4x4::TRS(m_Main.Position, m_Main.Rotation, m_Main.Scale);
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

	void MaterialEditor::uiViewportPanel()
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
			ImGui::Image(tid, ImVec2((float)m_Viewport.Width, (float)m_Viewport.Height), ImVec2(0, 1), ImVec2(1, 0));
		}
		else
		{
			ImGui::TextDisabled("No renderer output.");
		}

		ImGui::End();
	}

	void MaterialEditor::uiMaterialPanel()
	{
		if (!ImGui::Begin("Material"))
		{
			ImGui::End();
			return;
		}

		StaticMeshAsset* cpu = m_Main.ImportedCpuMesh;
		if (!cpu)
		{
			ImGui::TextDisabled("Load a StaticMeshAsset first.");
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
				getOrCreateSlotCache((uint32)m_SelectedSlot).Dirty = true;
			}
		}

		const uint32 slotIndex = (uint32)std::clamp<int32>(m_SelectedSlot, 0, (int32)slotCount - 1);
		MaterialAsset& mat = cpu->GetMaterialSlot(slotIndex);

		// Template selection: DefaultLit / DefaultLitMasked + custom
		MaterialUiCache& cache = getOrCreateSlotCache(slotIndex);

		const MaterialTemplate* tmpl = &m_pRenderer->GetMaterialTemplate(mat.GetTemplateName().empty() ? "DefaultLit" : mat.GetTemplateName());
		if (!tmpl)
		{
			ImGui::TextDisabled("Template load failed.");
			ImGui::End();
			return;
		}

		if (cache.Dirty)
		{
			syncCacheFromMaterialAsset(cache, mat, *tmpl);
			cache.Dirty = false;
		}

		// -----------------------------
		// Header (metadata)
		// -----------------------------
		ImGui::Text("Material Slot %u", slotIndex);
		ImGui::Separator();

		{
			// Quick buttons
			if (ImGui::Button("Use DefaultLit"))
			{
				cache.TemplateName = "DefaultLit";
				mat.SetTemplateName(cache.TemplateName);
				cache.Dirty = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Use DefaultLitMasked"))
			{
				cache.TemplateName = "DefaultLitMasked";
				mat.SetTemplateName(cache.TemplateName);
				cache.BlendMode = MATERIAL_BLEND_MODE_MASKED;
				cache.Dirty = true;
			}

			InputTextStdString("TemplateName", cache.TemplateName);

			ImGui::SameLine();
			if (ImGui::Button("Load Template"))
			{
				mat.SetTemplateName(cache.TemplateName);
				cache.Dirty = true;
			}

			InputTextStdString("RenderPass", cache.RenderPassName);
		}

		ImGui::Spacing();

		// -----------------------------
		// Options (pipeline-ish)
		// -----------------------------
		if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Blend mode is now the alpha/masked policy (RenderObject.bAlphaMasked 없음)
			{
				const char* items[] = { "OPAQUE","MASKED","TRANSLUCENT","ADDITIVE","PREMULTIPLIED" };
				int sel = 0;
				switch (cache.BlendMode)
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
					cache.BlendMode = map[sel];
				}
			}

			{
				const char* items[] = { "None", "Front", "Back" };
				int idx = (cache.CullMode == CULL_MODE_NONE) ? 0 : (cache.CullMode == CULL_MODE_FRONT) ? 1 : 2;
				if (ImGui::Combo("Cull", &idx, items, 3))
					cache.CullMode = (idx == 0) ? CULL_MODE_NONE : (idx == 1) ? CULL_MODE_FRONT : CULL_MODE_BACK;
			}

			ImGui::Checkbox("FrontCCW", &cache.FrontCounterClockwise);

			ImGui::Checkbox("DepthEnable", &cache.DepthEnable);
			ImGui::Checkbox("DepthWrite", &cache.DepthWriteEnable);

			{
				const char* labels[] = { "NEVER","LESS","EQUAL","LEQUAL","GREATER","NOT_EQUAL","GEQUAL","ALWAYS" };
				int sel = 3;
				switch (cache.DepthFunc)
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
					cache.DepthFunc = map[sel];
				}
			}

			{
				const char* items[] = { "DYNAMIC", "MUTABLE" };
				int mode = (cache.TextureBindingMode == MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC) ? 0 : 1;
				if (ImGui::Combo("TexBinding", &mode, items, 2))
					cache.TextureBindingMode = (mode == 0) ? MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC : MATERIAL_TEXTURE_BINDING_MODE_MUTABLE;
			}

			InputTextStdString("LinearWrapName", cache.LinearWrapSamplerName);

			ImGui::Checkbox("TwoSided (asset)", &cache.bTwoSided);
			ImGui::Checkbox("CastShadow (asset)", &cache.bCastShadow);
		}

		ImGui::Spacing();

		// IMPORTANT: if template name changed, reload tmpl pointer
		tmpl = &m_pRenderer->GetMaterialTemplate(cache.TemplateName);
		if (!tmpl)
		{
			ImGui::TextDisabled("Template is invalid.");
			ImGui::End();
			return;
		}

		// -----------------------------
		// Values (reflection-driven)
		// -----------------------------
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

			for (uint32 i = 0; i < tmpl->GetValueParamCount(); ++i)
			{
				const MaterialValueParamDesc& desc = tmpl->GetValueParam(i);
				if (desc.Name.empty())
					continue;

				if (!passFilter(desc.Name))
					continue;

				uint32 sz = desc.ByteSize != 0 ? desc.ByteSize : ValueTypeByteSize(desc.Type);
				if (sz == 0)
					continue;

				auto& bytes = cache.ValueBytes[desc.Name];
				if ((uint32)bytes.size() != sz)
				{
					bytes.resize(sz);
					std::memset(bytes.data(), 0, bytes.size());
				}

				ImGui::PushID((int)i);

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

		// -----------------------------
		// Resources (reflection-driven)
		// -----------------------------
		if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static char s_Filter[128] = {};
			ImGui::InputTextWithHint("Filter", "name contains...", s_Filter, sizeof(s_Filter));

			auto passFilter = [&](const std::string& name) -> bool
				{
					if (s_Filter[0] == 0)
						return true;
					return name.find(s_Filter) != std::string::npos;
				};

			for (uint32 i = 0; i < tmpl->GetResourceCount(); ++i)
			{
				const MaterialResourceDesc& res = tmpl->GetResource(i);
				if (res.Name.empty())
					continue;
				if (!IsTextureType(res.Type))
					continue;
				if (!passFilter(res.Name))
					continue;

				ImGui::PushID((int)i);

				std::string& path = cache.TexturePaths[res.Name];

				ImGui::Text("%s", res.Name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("(%s)",
					(res.Type == MATERIAL_RESOURCE_TYPE_TEXTURECUBE) ? "Cube" :
					(res.Type == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ? "2DArray" : "2D");

				InputTextStdString("Path", path);

				ImGui::SameLine();
				if (ImGui::Button("Clear"))
					path.clear();

				ImGui::PopID();
				ImGui::Separator();
			}
		}

		ImGui::Spacing();
		ImGui::Separator();

		// -----------------------------
		// APPLY (authoring -> rebuild mesh render data)
		// -----------------------------
		if (ImGui::Button("Apply To Mesh Slot (Rebuild StaticMeshRenderData)"))
		{
			// apply cache -> asset slot
			applyCacheToMaterialAsset(mat, cache, *tmpl);

			// also enforce template name on asset
			mat.SetTemplateName(cache.TemplateName.empty() ? "DefaultLit" : cache.TemplateName);

			// rebuild GPU mesh render data (includes materials per your policy)
			(void)rebuildMainMeshRenderData();

			syncCacheFromMaterialAsset(cache, mat, *tmpl);
			cache.Dirty = false;
		}

		ImGui::End();
	}

	void MaterialEditor::uiStatsPanel()
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

		// draw call table
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
