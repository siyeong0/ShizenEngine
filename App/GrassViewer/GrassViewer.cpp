// ============================================================================
// GrassViewer.cpp
// ============================================================================

#include "GrassViewer.h"

#include <algorithm>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshExporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/TextureImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/MaterialImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/MaterialExporter.h"

namespace shz
{
	namespace
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	SampleBase* CreateSample()
	{
		return new GrassViewer();
	}

	// ------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------

	void GrassViewer::Initialize(const SampleInitInfo& initInfo)
	{
		SampleBase::Initialize(initInfo);

		m_pAssetManager = std::make_unique<AssetManager>();
		{
			ASSERT(m_pAssetManager, "AssetManager is null.");
			m_pAssetManager->RegisterImporter(AssetTypeTraits<StaticMeshAsset>::TypeID, StaticMeshAssetImporter{});
			m_pAssetManager->RegisterExporter(AssetTypeTraits<StaticMeshAsset>::TypeID, StaticMeshAssetExporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<TextureAsset>::TypeID, TextureImporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<MaterialAsset>::TypeID, MaterialAssetImporter{});
			m_pAssetManager->RegisterExporter(AssetTypeTraits<MaterialAsset>::TypeID, MaterialAssetExporter{});
		}

		m_pRenderer = std::make_unique<Renderer>();

		m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(
			"C:/Dev/ShizenEngine/Shaders",
			&m_pShaderSourceFactory);

		RendererCreateInfo rendererCI = {};
		rendererCI.pEngineFactory = m_pEngineFactory;
		rendererCI.pShaderSourceFactory = m_pShaderSourceFactory;
		rendererCI.pDevice = m_pDevice;
		rendererCI.pImmediateContext = m_pImmediateContext;
		rendererCI.pDeferredContexts = m_pDeferredContexts;
		rendererCI.pSwapChain = m_pSwapChain;
		rendererCI.pImGui = m_pImGui;
		rendererCI.BackBufferWidth = m_pSwapChain->GetDesc().Width;
		rendererCI.BackBufferHeight = m_pSwapChain->GetDesc().Height;
		rendererCI.pAssetManager = m_pAssetManager.get();

		m_pRenderer->Initialize(rendererCI);

		m_pRenderScene = std::make_unique<RenderScene>();

		// Build fixed templates + prepare cache map
		(void)buildInitialTemplateCache();

		// Camera
		m_Camera.SetPos(float3(0.0f, 0.6f, -0.8f));
		m_Camera.SetRotation(0.0f, 0.0f);
		m_Camera.SetMoveSpeed(3.0f);
		m_Camera.SetRotationSpeed(0.01f);

		m_Camera.SetProjAttribs(
			0.1f,
			300.0f,
			(float)rendererCI.BackBufferWidth / (float)rendererCI.BackBufferHeight,
			PI / 4.0f,
			SURFACE_TRANSFORM_IDENTITY);

		m_ViewFamily.Views.clear();
		m_ViewFamily.Views.push_back({});

		// Global light
		m_GlobalLight.Direction = float3(0.4f, -1.0f, 0.3f);
		m_GlobalLight.Color = float3(1.0f, 1.0f, 1.0f);
		m_GlobalLight.Intensity = 2.0f;

		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);

		// Hard-coded objects
		{
			const char* floorPath = "C:/Dev/ShizenEngine/Assets/Exported/Terrain.shzmesh.json";
			(void)loadStaticMeshObject(
				m_Floor,
				floorPath,
				{ 0.0f, 0.0f, 0.0f },
				{ 0.0f, 0.0f, 0.0f },
				{ 1.0f, 1.0f, 1.0f },
				true,
				false);
		}

		const char* kGrassPaths[] =
		{
			"C:/Dev/ShizenEngine/Assets/Exported/Grass00.shzmesh.json",
			// "C:/Dev/ShizenEngine/Assets/Exported/Grass01.shzmesh.json",
		};

		const int32 countX = 100;
		const int32 countZ = 100;
		const float spacing = 0.35f;
		const float3 origin = { -10.0f, -0.1f, -10.0f };

		m_Grasses.clear();
		m_Grasses.reserve((size_t)countX * (size_t)countZ);

		int32 assetPick = 0;

		for (int32 z = 0; z < countZ; ++z)
		{
			for (int32 x = 0; x < countX; ++x)
			{
				LoadedStaticMesh g = {};

				const char* p = kGrassPaths[assetPick];
				assetPick = (assetPick + 1) % (int32)std::size(kGrassPaths);

				const float3 pos =
				{
					origin.x + (float)x * spacing,
					origin.y,
					origin.z + (float)z * spacing
				};

				const float yaw = (float)((x * 131 + z * 911) % 360) * (PI / 180.0f);
				const float3 rot = { 0.0f, yaw, 0.0f };
				const float3 scl = { 0.01f, 0.01f, 0.01f };

				const bool ok = loadStaticMeshObject(
					g,
					p,
					pos,
					rot,
					scl,
					true,
					true);

				if (ok)
					m_Grasses.push_back(std::move(g));
			}
		}

		m_Viewport.Width = std::max(1u, rendererCI.BackBufferWidth);
		m_Viewport.Height = std::max(1u, rendererCI.BackBufferHeight);
	}

	void GrassViewer::Render()
	{
		m_ViewFamily.FrameIndex++;

		m_pRenderer->BeginFrame();
		m_pRenderer->Render(*m_pRenderScene, m_ViewFamily);
		m_pRenderer->EndFrame();
	}

	void GrassViewer::Update(double currTime, double elapsedTime, bool doUpdateUI)
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

	void GrassViewer::ReleaseSwapChainBuffers()
	{
		SampleBase::ReleaseSwapChainBuffers();

		if (m_pRenderer)
			m_pRenderer->ReleaseSwapChainBuffers();
	}

	void GrassViewer::WindowResize(uint32 width, uint32 height)
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

	void GrassViewer::UpdateUI()
	{
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::gizmo3D("##LightDirection", m_GlobalLight.Direction, ImGui::GetTextLineHeight() * 10);
			ImGui::ColorEdit3("##LightColor", reinterpret_cast<float*>(&m_GlobalLight.Color));
			ImGui::SliderFloat("Intensity", &m_GlobalLight.Intensity, 0.01f, 20.0f);

			ImGui::Separator();
			ImGui::TextDisabled("FPS: %.1f", ImGui::GetIO().Framerate);
		}
		ImGui::End();
	}


	// ------------------------------------------------------------
	// Templates: fixed + cache
	// ------------------------------------------------------------

	bool GrassViewer::buildInitialTemplateCache()
	{
		if (m_TemplatesReady)
			return true;

		ASSERT(m_pDevice, "Device is null.");
		ASSERT(m_pShaderSourceFactory, "ShaderSourceFactory is null.");

		auto makeTemplate = [&](MaterialTemplate& outTmpl, const char* name, const char* vs, const char* ps) -> bool
		{
			MaterialTemplateCreateInfo tci = {};
			tci.PipelineType = MATERIAL_PIPELINE_TYPE_GRAPHICS;
			tci.TemplateName = name;

			tci.ShaderStages.clear();
			tci.ShaderStages.reserve(2);

			MaterialShaderStageDesc sVS = {};
			sVS.ShaderType = SHADER_TYPE_VERTEX;
			sVS.DebugName = "VS";
			sVS.FilePath = vs;
			sVS.EntryPoint = "main";
			sVS.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sVS.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sVS.UseCombinedTextureSamplers = false;

			MaterialShaderStageDesc sPS = {};
			sPS.ShaderType = SHADER_TYPE_PIXEL;
			sPS.DebugName = "PS";
			sPS.FilePath = ps;
			sPS.EntryPoint = "main";
			sPS.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sPS.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			sPS.UseCombinedTextureSamplers = false;

			tci.ShaderStages.push_back(sVS);
			tci.ShaderStages.push_back(sPS);

			return outTmpl.Initialize(m_pDevice, m_pShaderSourceFactory, tci);
		};

		const bool ok0 = makeTemplate(m_TmplGBuffer, "GrassViewer_GBuffer", "GBuffer.vsh", "GBuffer.psh");
		const bool ok1 = makeTemplate(m_TmplGBufferMasked, "GrassViewer_GBufferMasked", "GBufferMasked.vsh", "GBufferMasked.psh");

		ASSERT(ok0 && ok1, "GrassViewer::BuildInitialTemplateCache failed.");

		// Register into TemplateKey cache.
		// IMPORTANT: these keys MUST match what MaterialEditor writes into MaterialAsset::SetTemplateKey().
		//
		// In your MaterialEditor code, you used:
		//   dst.SetTemplateKey(MakeTemplateKeyFromInputs());
		// which looks like: "vs=gbuffer.vsh|vse=main|ps=gbuffer.psh|pse=main|h=...."
		//
		// So to find them at runtime, we do NOT try to reconstruct the hash string here.
		// Instead, we do "lazy bind": when we see an unknown key we map it to one of the fixed templates
		// by inspecting whether it contains "gbuffermasked" or "masked".
		//
		// If you prefer strict mapping, you can also store a shorter canonical key in MaterialEditor
		// (e.g. "GBuffer" / "GBufferMasked") and use that as TemplateKey.
		m_pTemplateCache.clear();
		m_TemplatesReady = true;
		return true;
	}

	MaterialTemplate* GrassViewer::getFallbackTemplate(bool bAlphaMasked)
	{
		return bAlphaMasked ? &m_TmplGBufferMasked : &m_TmplGBuffer;
	}

	MaterialTemplate* GrassViewer::getOrCreateTemplateByKey(const std::string& templateKey)
	{
		if (!m_TemplatesReady)
			(void)buildInitialTemplateCache();

		// Fast path: already cached
		if (auto it = m_pTemplateCache.find(templateKey); it != m_pTemplateCache.end())
			return it->second;

		// ------------------------------------------------------------
		// Minimal policy (per your request):
		// - We do NOT build arbitrary templates here.
		// - We only route to one of the two fixed templates.
		// - We still store a map entry so future lookups are O(1).
		// ------------------------------------------------------------

		// Heuristic mapping (robust to the long "vs=...|ps=...|h=..." key):
		// If key suggests Masked shader, map to GBufferMasked, else GBuffer.
		bool wantMasked = false;
		{
			std::string lower = templateKey;
			for (char& c : lower)
			{
				if (c >= 'A' && c <= 'Z')
					c = (char)(c - 'A' + 'a');
			}

			if (lower.find("gbuffermasked") != std::string::npos ||
				lower.find("masked") != std::string::npos)
			{
				wantMasked = true;
			}
		}

		MaterialTemplate* src = getFallbackTemplate(wantMasked);

		// Copy-construct into cache under this key.
		// If MaterialTemplate is non-copyable in your engine, change this to:
		// - store pointers, or
		// - store "enum kind" mapping, or
		// - store reference_wrapper.
		//
		// Here we assume move/copy is allowed like your MaterialEditor cache used emplace(key, std::move(tmpl)).
		m_pTemplateCache.emplace(templateKey, src);

		return m_pTemplateCache.find(templateKey)->second;
	}

	// ------------------------------------------------------------
	// Build materials for slots (TemplateKey-driven)
	// ------------------------------------------------------------

	std::vector<MaterialInstance> GrassViewer::buildMaterialsForCpuMeshSlots(const StaticMeshAsset& cpuMesh)
	{
		std::vector<MaterialInstance> materials;
		materials.reserve(cpuMesh.GetMaterialSlots().size());

		for (uint32 i = 0; i < (uint32)cpuMesh.GetMaterialSlots().size(); ++i)
		{
			const MaterialAsset& slot = cpuMesh.GetMaterialSlot(i);

			// TemplateKey from asset (authoritative)
			const std::string key = slot.GetTemplateKey(); // <-- MUST exist in MaterialAsset
			MaterialTemplate* pTemplate = nullptr;

			if (!key.empty())
				pTemplate = getOrCreateTemplateByKey(key);

			// If missing key in asset, fallback based on pass name or mesh alpha-masked policy.
			if (!pTemplate)
			{
				// Fallback heuristic: pass name contains "Masked"
				bool wantMasked = false;
				{
					std::string rp = slot.GetRenderPassName();
					for (char& c : rp) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
					if (rp.find("masked") != std::string::npos)
						wantMasked = true;
				}
				pTemplate = getFallbackTemplate(wantMasked);
			}

			MaterialInstance inst = {};
			{
				const bool ok = inst.Initialize(pTemplate, "GrassViewer Instance");
				ASSERT(ok, "MaterialInstance::Initialize failed.");
			}

			{
				const bool ok = slot.ApplyToInstance(&inst);
				ASSERT(ok, "MaterialAsset::ApplyToInstance failed.");
			}

			inst.SetRenderPass(slot.GetRenderPassName());
			inst.MarkAllDirty();

			materials.push_back(std::move(inst));
		}

		return materials;
	}

	// ------------------------------------------------------------
	// Load one StaticMeshAsset and add RenderObject
	// ------------------------------------------------------------

	bool GrassViewer::loadStaticMeshObject(
		LoadedStaticMesh& inout,
		const char* path,
		float3 position,
		float3 rotation,
		float3 scale,
		bool bCastShadow,
		bool bAlphaMasked)
	{
		if (!path || path[0] == '\0')
			return false;

		ASSERT(m_pAssetManager, "AssetManager is null.");
		ASSERT(m_pRenderScene, "RenderScene is null.");
		ASSERT(m_pRenderer, "Renderer is null.");

		if (inout.ObjectId.IsValid())
		{
			m_pRenderScene->RemoveObject(inout.ObjectId);
			inout = {};
		}

		inout.Path = path;
		inout.bCastShadow = bCastShadow;
		inout.bAlphaMasked = bAlphaMasked;

		inout.MeshRef = m_pAssetManager->RegisterAsset<StaticMeshAsset>(inout.Path);
		inout.MeshPtr = m_pAssetManager->LoadBlocking(inout.MeshRef, EAssetLoadFlags::KeepResident);

		const StaticMeshAsset* pCpu = inout.MeshPtr.Get();
		if (!pCpu)
			return false;

		inout.MeshHandle = m_pRenderer->CreateStaticMesh(*pCpu);
		if (!inout.MeshHandle.IsValid())
			return false;

		std::vector<MaterialInstance> mats = buildMaterialsForCpuMeshSlots(*pCpu);

		RenderScene::RenderObject obj = {};
		obj.MeshHandle = inout.MeshHandle;
		obj.Materials = std::move(mats);
		obj.Transform = Matrix4x4::TRS(position, rotation, scale);
		obj.bCastShadow = bCastShadow;
		obj.bAlphaMasked = bAlphaMasked;

		inout.ObjectId = m_pRenderScene->AddObject(std::move(obj));
		if (!inout.ObjectId.IsValid())
			return false;

		inout.SceneObjectIndex = (int32)m_pRenderScene->GetObjects().size() - 1;
		return true;
	}
} // namespace shz
