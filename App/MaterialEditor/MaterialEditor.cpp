// ============================================================================
// MaterialEditor.cpp
// ============================================================================

#include "MaterialEditor.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/ImGuiUtils.hpp"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/AssetRuntime/Pipeline/Public/AssimpImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/MaterialExporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/MaterialImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshExporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/TextureImporter.h"
#include "Engine/AssetRuntime/Common/AssetTypeTraits.h"

namespace shz
{
	namespace
	{
#include "Shaders/HLSL_Structures.hlsli"

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
			nullptr,
		};

		static const char* g_PresetPS[] =
		{
			"GBuffer.psh",
			"GBufferMasked.psh",
			nullptr,
		};

		static const char* g_PresetVSEntry[] =
		{
			"main",
			"main",
			nullptr,
		};

		static const char* g_PresetPSEntry[] =
		{
			"main",
			"main",
			nullptr,
		};

		// ------------------------------------------------------------
		// Safe std::string InputText
		// - no UB / no direct write into std::string::data() without resize
		// ------------------------------------------------------------
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
			// Ensure we have some capacity and a trailing '\0'.
			if (str.capacity() < 64)
				str.reserve(64);

			return ImGui::InputText(
				label,
				str.data(),
				str.capacity() + 1,
				ImGuiInputTextFlags_CallbackResize,
				InputTextCallback_Resize,
				(void*)&str);
		}

		static uint32 FlagFromTextureName(const std::string& name) noexcept
		{
			if (name == "g_BaseColorTex")         return MAT_HAS_BASECOLOR;
			if (name == "g_NormalTex")            return MAT_HAS_NORMAL;
			if (name == "g_MetallicRoughnessTex") return MAT_HAS_MR;
			if (name == "g_AOTex")                return MAT_HAS_AO;
			if (name == "g_EmissiveTex")          return MAT_HAS_EMISSIVE;
			if (name == "g_HeightTex")            return MAT_HAS_HEIGHT;
			return 0;
		}

		static float ComputeUniformScaleToFitUnitCube(const Box& bounds, float targetSize = 1.0f) noexcept
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
	}

	SampleBase* CreateSample()
	{
		return new MaterialEditor();
	}

	// ------------------------------------------------------------
	// AssetManager integration
	// ------------------------------------------------------------

	void MaterialEditor::RegisterAssetLoaders()
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");

		m_pAssetManager->RegisterImporter(AssetTypeTraits<StaticMeshAsset>::TypeID, StaticMeshAssetImporter{});
		m_pAssetManager->RegisterExporter(AssetTypeTraits<StaticMeshAsset>::TypeID, StaticMeshAssetExporter{});

		m_pAssetManager->RegisterImporter(AssetTypeTraits<TextureAsset>::TypeID, TextureImporter{});
		// (Texture exporter는 필요하면 추가)

		m_pAssetManager->RegisterImporter(AssetTypeTraits<MaterialAsset>::TypeID, MaterialAssetImporter{});
		m_pAssetManager->RegisterExporter(AssetTypeTraits<MaterialAsset>::TypeID, MaterialAssetExporter{});

		m_pAssetManager->RegisterImporter(AssetTypeTraits<AssimpAsset>::TypeID, AssimpImporter{});
	}

	AssetRef<StaticMeshAsset> MaterialEditor::RegisterStaticMeshPath(const std::string& path)
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");
		return m_pAssetManager->RegisterAsset<StaticMeshAsset>(path);
	}

	AssetRef<TextureAsset> MaterialEditor::RegisterTexturePath(const std::string& path)
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");
		return m_pAssetManager->RegisterAsset<TextureAsset>(path);
	}

	AssetPtr<StaticMeshAsset> MaterialEditor::LoadStaticMeshBlocking(AssetRef<StaticMeshAsset> ref, EAssetLoadFlags flags)
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");
		return m_pAssetManager->LoadBlocking(ref, flags);
	}

	AssetPtr<TextureAsset> MaterialEditor::LoadTextureBlocking(AssetRef<TextureAsset> ref, EAssetLoadFlags flags)
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");
		return m_pAssetManager->LoadBlocking(ref, flags);
	}

	// ------------------------------------------------------------
	// Template
	// ------------------------------------------------------------

	std::string MaterialEditor::MakeTemplateKeyFromInputs() const
	{
		auto trim = [](std::string s) -> std::string
			{
				auto isSpace = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
				while (!s.empty() && isSpace((unsigned char)s.front())) s.erase(s.begin());
				while (!s.empty() && isSpace((unsigned char)s.back()))  s.pop_back();
				return s;
			};

		auto stripQuotes = [](std::string s) -> std::string
			{
				if (!s.empty() && (s.front() == '"' || s.front() == '\'')) s.erase(s.begin());
				if (!s.empty() && (s.back() == '"' || s.back() == '\''))  s.pop_back();
				return s;
			};

		auto normalizePathLike = [&](std::string s) -> std::string
			{
				s = trim(stripQuotes(std::move(s)));

				// slash unify
				for (char& c : s)
				{
					if (c == '\\') c = '/';
				}

				// remove redundant "./" segments (simple)
				// NOTE: we keep absolute paths and drive letters as-is; this is just to stabilize relative forms.
				while (s.size() >= 2 && s[0] == '.' && s[1] == '/')
					s.erase(0, 2);

				// to lower (Windows-like stability)
				for (char& c : s)
				{
					if (c >= 'A' && c <= 'Z')
						c = (char)(c - 'A' + 'a');
				}

				return s;
			};

		auto normalizeEntry = [&](std::string s) -> std::string
			{
				s = trim(stripQuotes(std::move(s)));

				// entry names are usually case-sensitive in HLSL, but in practice you use "main".
				// If you want strict behavior, remove this lowercasing.
				for (char& c : s)
				{
					if (c >= 'A' && c <= 'Z')
						c = (char)(c - 'A' + 'a');
				}

				return s;
			};

		const std::string vs = normalizePathLike(m_ShaderVS);
		const std::string ps = normalizePathLike(m_ShaderPS);
		const std::string vse = normalizeEntry(m_VSEntry);
		const std::string pse = normalizeEntry(m_PSEntry);

		// Canonical, stable string
		std::string canonical;
		canonical.reserve(vs.size() + ps.size() + vse.size() + pse.size() + 64);

		canonical += "vs=";  canonical += vs;
		canonical += "|vse="; canonical += vse;
		canonical += "|ps=";  canonical += ps;
		canonical += "|pse="; canonical += pse;

		// Optional: append short stable hash for compactness / debugging
		auto fnv1a64 = [](const char* data, size_t len) -> uint64
			{
				const uint64 FNV_OFFSET = 1469598103934665603ull;
				const uint64 FNV_PRIME = 1099511628211ull;

				uint64 h = FNV_OFFSET;
				for (size_t i = 0; i < len; ++i)
				{
					h ^= (uint64)(uint8)data[i];
					h *= FNV_PRIME;
				}
				return h;
			};

		const uint64 h = fnv1a64(canonical.data(), canonical.size());

		char buf[32] = {};
		std::snprintf(buf, sizeof(buf), "|h=%016llx", (unsigned long long)h);
		canonical += buf;

		return canonical;
	}


	MaterialTemplate* MaterialEditor::GetOrCreateTemplateFromInputs()
	{
		if (m_ShaderVS.empty() || m_ShaderPS.empty() || m_VSEntry.empty() || m_PSEntry.empty())
			return nullptr;

		const std::string key = MakeTemplateKeyFromInputs();

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
		vs.UseCombinedTextureSamplers = false;

		MaterialShaderStageDesc ps = {};
		ps.ShaderType = SHADER_TYPE_PIXEL;
		ps.DebugName = "MaterialEditor PS";
		ps.FilePath = m_ShaderPS.c_str();
		ps.EntryPoint = m_PSEntry.c_str();
		ps.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		ps.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
		ps.UseCombinedTextureSamplers = false;

		tci.ShaderStages.push_back(vs);
		tci.ShaderStages.push_back(ps);

		const bool ok = tmpl.Initialize(m_pDevice, m_pShaderSourceFactory, tci);
		ASSERT(ok, "MaterialTemplate::Initialize failed.");

		auto [insIt, _] = m_TemplateCache.emplace(key, std::move(tmpl));

		// Mark current slot cache dirty (MAIN only)
		const uint64 selKey = MakeSelectionKeyForMain(m_SelectedMaterialSlot);
		if (auto itCache = m_MaterialUi.find(selKey); itCache != m_MaterialUi.end())
			itCache->second.Dirty = true;

		return &insIt->second;
	}

	MaterialTemplate* MaterialEditor::RebuildTemplateFromInputs()
	{
		if (m_ShaderVS.empty() || m_ShaderPS.empty() || m_VSEntry.empty() || m_PSEntry.empty())
			return nullptr;

		const std::string key = MakeTemplateKeyFromInputs();
		m_TemplateCache.erase(key);

		return GetOrCreateTemplateFromInputs();
	}

	void MaterialEditor::RebindMainMaterialToTemplate(MaterialTemplate* pNewTmpl)
	{
		if (!pNewTmpl)
			return;

		RenderScene::RenderObject* obj = GetMainRenderObjectOrNull();
		if (!obj)
			return;

		if (m_SelectedMaterialSlot < 0 || m_SelectedMaterialSlot >= (int32)obj->Materials.size())
			return;

		MaterialUiCache& cache = GetOrCreateMaterialCache(MakeSelectionKeyForMain(m_SelectedMaterialSlot));

		MaterialInstance newInst = {};
		{
			const bool ok = newInst.Initialize(pNewTmpl, "MaterialEditor Instance");
			ASSERT(ok, "MaterialInstance::Initialize failed.");
		}

		ApplyCacheToInstance(newInst, cache, *pNewTmpl);

		obj->Materials[(size_t)m_SelectedMaterialSlot] = std::move(newInst);
		obj->Materials[(size_t)m_SelectedMaterialSlot].MarkAllDirty();
	}

	// ------------------------------------------------------------
	// Scene access helpers (ObjectId first)
	// ------------------------------------------------------------

	RenderScene::RenderObject* MaterialEditor::GetMainRenderObjectOrNull()
	{
		if (!m_pRenderScene)
			return nullptr;

		LoadedMesh* main = GetMainMeshOrNull();
		if (!main)
			return nullptr;

		// If RenderScene has a way to fetch by handle, use it.
		// If not, fallback to index.
		// NOTE: Replace this block with your engine's preferred API if exists.
		{
			// Fallback: index
			if (main->SceneObjectIndex >= 0 && main->SceneObjectIndex < (int32)m_pRenderScene->GetObjects().size())
				return &m_pRenderScene->GetObjects()[(size_t)main->SceneObjectIndex];
		}

		return nullptr;
	}

	const RenderScene::RenderObject* MaterialEditor::GetMainRenderObjectOrNull() const
	{
		if (!m_pRenderScene)
			return nullptr;

		const LoadedMesh* main = GetMainMeshOrNull();
		if (!main)
			return nullptr;

		{
			if (main->SceneObjectIndex >= 0 && main->SceneObjectIndex < (int32)m_pRenderScene->GetObjects().size())
				return &m_pRenderScene->GetObjects()[(size_t)main->SceneObjectIndex];
		}

		return nullptr;
	}

	// ------------------------------------------------------------
	// Initialize / render / update
	// ------------------------------------------------------------

	void MaterialEditor::Initialize(const SampleInitInfo& initInfo)
	{
		SampleBase::Initialize(initInfo);

		m_pAssetManager = std::make_unique<AssetManager>();
		RegisterAssetLoaders();

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

		m_Camera.SetProjAttribs(
			0.1f,
			100.0f,
			(float)rendererCI.BackBufferWidth / (float)rendererCI.BackBufferHeight,
			PI / 4.0f,
			SURFACE_TRANSFORM_IDENTITY);

		m_ViewFamily.Views.clear();
		m_ViewFamily.Views.push_back({});

		m_SelectedPresetIndex = 0;
		m_ShaderVS = g_PresetVS[0];
		m_ShaderPS = g_PresetPS[0];
		m_VSEntry = g_PresetVSEntry[0];
		m_PSEntry = g_PresetPSEntry[0];

		(void)GetOrCreateTemplateFromInputs();

		// Floor default
		LoadPreviewMesh(
			EPreviewObject::Floor,
			"C:/Dev/ShizenEngine/Assets/Basic/floor/FbxFloor.fbx",
			{ -2.0f, -0.5f, 3.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ 1.0f, 1.0f, 1.0f },
			false,
			false,
			false);

		// Main is loaded by UI
		m_MainMeshPath = "C:/Dev/ShizenEngine/Assets/Grass/grass-free-download/source/grass.fbx";
		m_SelectedMaterialSlot = 0;

		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);
	}

	void MaterialEditor::Render()
	{
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

		// Camera
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
		// Renderer resize is driven by viewport window size in UiViewportPanel().
	}

	// ------------------------------------------------------------
	// UI
	// ------------------------------------------------------------

	void MaterialEditor::UpdateUI()
	{
		UiDockspace();
		UiScenePanel();
		UiViewportPanel();
		UiMaterialInspector();
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

				ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.32f, &dockRight, &dockMain);
				ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.22f, &dockLeft, &dockMain);
				ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, &dockBottom, &dockMain);

				ImGui::DockBuilderDockWindow("Viewport", dockMain);
				ImGui::DockBuilderDockWindow("Scene", dockLeft);
				ImGui::DockBuilderDockWindow("Material", dockRight);
				ImGui::DockBuilderDockWindow("Stats", dockBottom);

				ImGui::DockBuilderFinish(dockspaceID);
			}

			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("Material"))
				{
					if (ImGui::MenuItem("Clear Template Cache"))
					{
						m_TemplateCache.clear();
					}
					if (ImGui::MenuItem("Rebuild Current Template"))
					{
						(void)RebuildTemplateFromInputs();
						const uint64 key = MakeSelectionKeyForMain(m_SelectedMaterialSlot);
						m_MaterialUi[key].Dirty = true;
						RebindMainMaterialToTemplate(GetOrCreateTemplateFromInputs());
					}
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

		ImGui::Checkbox("Uniform Scale", &m_MainMeshUniformScale);

		ImGui::DragFloat3("Position", reinterpret_cast<float*>(&m_MainMeshPos), 0.01f);
		ImGui::DragFloat3("Rotation", reinterpret_cast<float*>(&m_MainMeshRot), 0.5f);
		ImGui::DragFloat3("Scale", reinterpret_cast<float*>(&m_MainMeshScale), 0.01f);

		ImGui::Checkbox("Cast Shadow", &m_MainMeshCastShadow);
		ImGui::Checkbox("Alpha Masked", &m_MainMeshAlphaMasked);

		if (ImGui::Button("Load / Replace Main Object"))
		{
			const std::string p = SanitizeFilePath(m_MainMeshPath);
			if (!p.empty())
			{
				const bool ok = LoadPreviewMesh(
					EPreviewObject::Main,
					p.c_str(),
					m_MainMeshPos,
					m_MainMeshRot,
					m_MainMeshScale,
					m_MainMeshUniformScale,
					m_MainMeshCastShadow,
					m_MainMeshAlphaMasked);

				if (ok)
				{
					m_SelectedMaterialSlot = 0;
					m_MaterialUi[MakeSelectionKeyForMain(m_SelectedMaterialSlot)].Dirty = true;
				}
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Save Main Object");
		ImGui::Separator();

		InputTextStdString("Out Path", m_MainMeshSavePath);

		static bool s_Force = false;
		ImGui::Checkbox("Force", &s_Force);

		if (ImGui::Button("Save Main as StaticMeshAsset"))
		{
			const std::string outPath = SanitizeFilePath(m_MainMeshSavePath);

			std::string err;
			const bool ok = SaveMainObject(
				outPath,
				s_Force ? EAssetSaveFlags::Force : EAssetSaveFlags::None,
				&err);

			if (!ok)
			{
				ASSERT(false, err.empty() ? "SaveMainObject failed." : err.c_str());
			}
			else
			{
				const bool importOk = ImportMainFromSavedPath(outPath);
				ASSERT(importOk, "ImportMainFromSavedPath failed.");
			}
		}

		ImGui::Spacing();
		ImGui::Separator();

		if (LoadedMesh* main = GetMainMeshOrNull())
		{
			bool castShadow = main->bCastShadow;
			bool alphaMasked = main->bAlphaMasked;

			if (ImGui::Checkbox("Main: Cast Shadow (Live)", &castShadow))
			{
				main->bCastShadow = castShadow;
				m_MainMeshCastShadow = castShadow;

				if (RenderScene::RenderObject* obj = GetMainRenderObjectOrNull())
				{
					obj->bCastShadow = castShadow;
					for (auto& m : obj->Materials) m.MarkAllDirty();
				}
			}

			if (ImGui::Checkbox("Main: Alpha Masked (Live)", &alphaMasked))
			{
				main->bAlphaMasked = alphaMasked;
				m_MainMeshAlphaMasked = alphaMasked;

				if (RenderScene::RenderObject* obj = GetMainRenderObjectOrNull())
				{
					obj->bAlphaMasked = alphaMasked;
					for (auto& m : obj->Materials) m.MarkAllDirty();
				}
			}
		}
		else
		{
			ImGui::TextDisabled("Main object is not loaded.");
		}

		ImGui::Spacing();
		ImGui::Separator();

		ImGui::Text("Light");
		ImGui::Separator();

		ImGui::gizmo3D("##LightDirection", m_GlobalLight.Direction, ImGui::GetTextLineHeight() * 7);
		ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&m_GlobalLight.Color));
		ImGui::SliderFloat("Intensity", &m_GlobalLight.Intensity, 0.01f, 10.0f);

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
				PI / 4.0f,
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

	void MaterialEditor::UiMaterialInspector()
	{
		if (!ImGui::Begin("Material"))
		{
			ImGui::End();
			return;
		}

		// -----------------------------
		// Template UI
		// -----------------------------
		ImGui::Text("Template");
		ImGui::Separator();

		{
			int preset = m_SelectedPresetIndex;
			if (ImGui::Combo("Preset", &preset, g_PresetLabels, (int)std::size(g_PresetLabels)))
			{
				m_SelectedPresetIndex = preset;

				if (preset >= 0 && preset < 2)
				{
					m_ShaderVS = g_PresetVS[preset];
					m_ShaderPS = g_PresetPS[preset];
					m_VSEntry = g_PresetVSEntry[preset];
					m_PSEntry = g_PresetPSEntry[preset];
				}
			}

			InputTextStdString("VS", m_ShaderVS);
			InputTextStdString("VS Entry", m_VSEntry);
			InputTextStdString("PS", m_ShaderPS);
			InputTextStdString("PS Entry", m_PSEntry);

			if (ImGui::Button("Rebuild Template (and Rebind Slot)"))
			{
				MaterialTemplate* pNew = RebuildTemplateFromInputs();

				const uint64 key = MakeSelectionKeyForMain(m_SelectedMaterialSlot);
				m_MaterialUi[key].Dirty = true;

				RebindMainMaterialToTemplate(pNew);
			}
		}

		ImGui::Spacing();
		ImGui::Separator();

		// -----------------------------
		// Require MAIN object
		// -----------------------------
		RenderScene::RenderObject* obj = GetMainRenderObjectOrNull();
		MaterialTemplate* pTmpl = GetOrCreateTemplateFromInputs();

		if (!obj || !pTmpl || obj->Materials.empty())
		{
			ImGui::TextDisabled("Load Main object to edit materials.");
			ImGui::End();
			return;
		}

		// Slot picker
		{
			int slot = m_SelectedMaterialSlot;
			const int maxSlot = (int)obj->Materials.size();

			if (ImGui::SliderInt("Slot", &slot, 0, std::max(0, maxSlot - 1)))
			{
				m_SelectedMaterialSlot = slot;
				m_MaterialUi[MakeSelectionKeyForMain(m_SelectedMaterialSlot)].Dirty = true;
			}
		}

		MaterialInstance* pInst = GetMainMaterialOrNull();
		if (!pInst)
		{
			ImGui::TextDisabled("Selected slot is invalid.");
			ImGui::End();
			return;
		}

		MaterialUiCache& cache = GetOrCreateMaterialCache(MakeSelectionKeyForMain(m_SelectedMaterialSlot));
		if (cache.Dirty)
		{
			SyncCacheFromInstance(cache, *pInst, *pTmpl);
			cache.Dirty = false;
		}

		if (ImGui::CollapsingHeader("Pipeline", ImGuiTreeNodeFlags_DefaultOpen))
			DrawPipelineEditor(*pInst, cache);

		if (ImGui::CollapsingHeader("Values", ImGuiTreeNodeFlags_DefaultOpen))
			DrawValueEditor(*pInst, cache, *pTmpl);

		if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen))
			DrawResourceEditor(*pInst, cache, *pTmpl);

		if (ImGui::Button("MarkAllDirty"))
			pInst->MarkAllDirty();

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
		ImGui::Text("Main Slot: %d", m_SelectedMaterialSlot);

		ImGuiIO& io = ImGui::GetIO();
		ImGui::Text("ImGui FPS: %.1f", io.Framerate);

		ImGui::End();
	}

	// ------------------------------------------------------------
	// Selection / cache
	// ------------------------------------------------------------

	uint64 MaterialEditor::MakeSelectionKeyForMain(int32 slotIndex) const
	{
		return (uint64)(uint32)slotIndex;
	}

	MaterialEditor::MaterialUiCache& MaterialEditor::GetOrCreateMaterialCache(uint64 key)
	{
		auto it = m_MaterialUi.find(key);
		if (it != m_MaterialUi.end())
			return it->second;

		MaterialUiCache cache = {};
		cache.Dirty = true;

		auto [insIt, _] = m_MaterialUi.emplace(key, std::move(cache));
		return insIt->second;
	}

	MaterialEditor::LoadedMesh* MaterialEditor::GetMainMeshOrNull() noexcept
	{
		return m_Main.IsValid() ? &m_Main : nullptr;
	}

	const MaterialEditor::LoadedMesh* MaterialEditor::GetMainMeshOrNull() const noexcept
	{
		return m_Main.IsValid() ? &m_Main : nullptr;
	}

	MaterialInstance* MaterialEditor::GetMainMaterialOrNull()
	{
		RenderScene::RenderObject* obj = GetMainRenderObjectOrNull();
		if (!obj)
			return nullptr;

		if (m_SelectedMaterialSlot < 0 || m_SelectedMaterialSlot >= (int32)obj->Materials.size())
			return nullptr;

		return &obj->Materials[(size_t)m_SelectedMaterialSlot];
	}

	// ------------------------------------------------------------
	// Reflection-driven cache init / apply
	// ------------------------------------------------------------

	void MaterialEditor::SyncCacheFromInstance(MaterialUiCache& cache, const MaterialInstance& inst, const MaterialTemplate& tmpl)
	{
		// -----------------------------
	   // ? Pipeline / options sync (추가)
	   // -----------------------------
		cache.CullMode = inst.GetCullMode();
		cache.FrontCounterClockwise = inst.GetFrontCounterClockwise();

		cache.DepthEnable = inst.GetDepthEnable();
		cache.DepthWriteEnable = inst.GetDepthWriteEnable();
		cache.DepthFunc = inst.GetDepthFunc();

		cache.TextureBindingMode = inst.GetTextureBindingMode();

		cache.LinearWrapSamplerName = inst.GetLinearWrapSamplerName();
		cache.LinearWrapSamplerDesc = inst.GetLinearWrapSamplerDesc();

		auto oldValues = std::move(cache.ValueBytes);
		auto oldTextures = std::move(cache.TexturePaths);

		cache.ValueBytes.clear();
		cache.TexturePaths.clear();

		for (uint32 i = 0; i < tmpl.GetValueParamCount(); ++i)
		{
			const MaterialValueParamDesc& desc = tmpl.GetValueParam(i);
			if (desc.Name.empty())
				continue;

			uint32 sz = desc.ByteSize;
			if (sz == 0)
			{
				switch (desc.Type)
				{
				case MATERIAL_VALUE_TYPE_FLOAT:    sz = sizeof(float); break;
				case MATERIAL_VALUE_TYPE_FLOAT2:   sz = sizeof(float) * 2; break;
				case MATERIAL_VALUE_TYPE_FLOAT3:   sz = sizeof(float) * 3; break;
				case MATERIAL_VALUE_TYPE_FLOAT4:   sz = sizeof(float) * 4; break;
				case MATERIAL_VALUE_TYPE_INT:      sz = sizeof(int32); break;
				case MATERIAL_VALUE_TYPE_INT2:     sz = sizeof(int32) * 2; break;
				case MATERIAL_VALUE_TYPE_INT3:     sz = sizeof(int32) * 3; break;
				case MATERIAL_VALUE_TYPE_INT4:     sz = sizeof(int32) * 4; break;
				case MATERIAL_VALUE_TYPE_UINT:     sz = sizeof(uint32); break;
				case MATERIAL_VALUE_TYPE_UINT2:    sz = sizeof(uint32) * 2; break;
				case MATERIAL_VALUE_TYPE_UINT3:    sz = sizeof(uint32) * 3; break;
				case MATERIAL_VALUE_TYPE_UINT4:    sz = sizeof(uint32) * 4; break;
				case MATERIAL_VALUE_TYPE_FLOAT4X4: sz = sizeof(float) * 16; break;
				default:                           sz = 0; break;
				}
			}

			std::vector<uint8> bytes;
			bytes.resize(sz);
			if (sz > 0)
				std::memset(bytes.data(), 0, bytes.size());

			bool filled = false;

			if (sz > 0)
			{
				const uint32 cbIndex = desc.CBufferIndex;
				const uint32 cbCount = inst.GetCBufferBlobCount();

				if (cbIndex < cbCount)
				{
					const uint8* pCB = inst.GetCBufferBlobData(cbIndex);
					const uint32 cbSize = inst.GetCBufferBlobSize(cbIndex);

					if (pCB && desc.ByteOffset + sz <= cbSize)
					{
						std::memcpy(bytes.data(), pCB + desc.ByteOffset, sz);
						filled = true;
					}
				}
			}

			if (!filled)
			{
				auto itOld = oldValues.find(desc.Name);
				if (itOld != oldValues.end())
				{
					const auto& old = itOld->second;
					const size_t copySz = std::min(old.size(), bytes.size());
					if (copySz > 0)
						std::memcpy(bytes.data(), old.data(), copySz);
				}
			}

			cache.ValueBytes.emplace(desc.Name, std::move(bytes));
		}

		for (uint32 i = 0; i < tmpl.GetResourceCount(); ++i)
		{
			const MaterialResourceDesc& resDesc = tmpl.GetResource(i);
			if (resDesc.Name.empty())
				continue;

			const bool isTexture =
				(resDesc.Type == MATERIAL_RESOURCE_TYPE_TEXTURE2D) ||
				(resDesc.Type == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ||
				(resDesc.Type == MATERIAL_RESOURCE_TYPE_TEXTURECUBE);

			if (!isTexture)
				continue;

			std::string path = {};
			if (auto itOld = oldTextures.find(resDesc.Name); itOld != oldTextures.end())
				path = itOld->second;

			cache.TexturePaths.emplace(resDesc.Name, std::move(path));
		}
	}

	void MaterialEditor::ApplyCacheToInstance(MaterialInstance& inst, MaterialUiCache& cache, const MaterialTemplate& tmpl)
	{
		if (inst.GetPipelineType() == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			IRenderPass* rp = nullptr;

			inst.SetRenderPass(cache.RenderPassName);

			inst.SetCullMode(cache.CullMode);
			inst.SetFrontCounterClockwise(cache.FrontCounterClockwise);

			inst.SetDepthEnable(cache.DepthEnable);
			inst.SetDepthWriteEnable(cache.DepthWriteEnable);
			inst.SetDepthFunc(cache.DepthFunc);

			inst.SetTextureBindingMode(cache.TextureBindingMode);

			inst.SetLinearWrapSamplerName(cache.LinearWrapSamplerName);
			inst.SetLinearWrapSamplerDesc(cache.LinearWrapSamplerDesc);
		}

		// Values
		for (uint32 i = 0; i < tmpl.GetValueParamCount(); ++i)
		{
			const MaterialValueParamDesc& desc = tmpl.GetValueParam(i);
			if (desc.Name.empty())
				continue;

			auto it = cache.ValueBytes.find(desc.Name);
			if (it == cache.ValueBytes.end() || it->second.empty())
				continue;

			(void)inst.SetValue(desc.Name.c_str(), it->second.data(), desc.Type);
		}

		// Resources + build g_MaterialFlags
		uint32 materialFlags = 0;

		{
			auto& fb = cache.ValueBytes["g_MaterialFlags"];
			if (fb.size() != sizeof(uint32))
			{
				fb.resize(sizeof(uint32));
				std::memset(fb.data(), 0, fb.size());
			}
		}

		for (uint32 i = 0; i < tmpl.GetResourceCount(); ++i)
		{
			const MaterialResourceDesc& res = tmpl.GetResource(i);
			if (res.Name.empty())
				continue;

			const bool isTexture =
				(res.Type == MATERIAL_RESOURCE_TYPE_TEXTURE2D) ||
				(res.Type == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ||
				(res.Type == MATERIAL_RESOURCE_TYPE_TEXTURECUBE);

			if (!isTexture)
				continue;

			const std::string& name = res.Name;
			const uint32 bit = FlagFromTextureName(name);

			std::string path = {};
			if (auto it = cache.TexturePaths.find(name); it != cache.TexturePaths.end())
				path = it->second;

			if (!path.empty())
			{
				const std::string p = SanitizeFilePath(path);
				if (!p.empty())
				{
					AssetRef<TextureAsset> ref = RegisterTexturePath(p);
					inst.SetTextureAssetRef(name.c_str(), ref);

					if (bit != 0)
						materialFlags |= bit;
				}
				else
				{
					inst.ClearTextureAssetRef(name.c_str());
				}
			}
			else
			{
				inst.ClearTextureAssetRef(name.c_str());
			}
		}

		{
			auto& fb = cache.ValueBytes["g_MaterialFlags"];
			std::memcpy(fb.data(), &materialFlags, sizeof(uint32));
			inst.SetUint("g_MaterialFlags", materialFlags);
		}

		inst.MarkAllDirty();
	}

	// ------------------------------------------------------------
	// Editors
	// ------------------------------------------------------------

	void MaterialEditor::DrawPipelineEditor(MaterialInstance& inst, MaterialUiCache& cache)
	{
		if (inst.GetPipelineType() != MATERIAL_PIPELINE_TYPE_GRAPHICS)
			return;

		if (InputTextStdString("RenderPass", cache.RenderPassName))
		{
			// live apply
		}

		int subpass = (int)cache.SubpassIndex;
		if (ImGui::InputInt("Subpass", &subpass))
			cache.SubpassIndex = (uint32)std::max(0, subpass);

		IRenderPass* rp = nullptr;

		inst.SetRenderPass(cache.RenderPassName);

		{
			int idx = 2;
			if (cache.CullMode == CULL_MODE_NONE)  idx = 0;
			if (cache.CullMode == CULL_MODE_FRONT) idx = 1;
			if (cache.CullMode == CULL_MODE_BACK)  idx = 2;

			const char* items[] = { "None", "Front", "Back" };
			if (ImGui::Combo("Cull", &idx, items, 3))
				cache.CullMode = (idx == 0) ? CULL_MODE_NONE : (idx == 1) ? CULL_MODE_FRONT : CULL_MODE_BACK;

			inst.SetCullMode(cache.CullMode);
		}

		if (ImGui::Checkbox("FrontCCW", &cache.FrontCounterClockwise))
			inst.SetFrontCounterClockwise(cache.FrontCounterClockwise);
		else
			inst.SetFrontCounterClockwise(cache.FrontCounterClockwise);

		if (ImGui::Checkbox("DepthEnable", &cache.DepthEnable))
			inst.SetDepthEnable(cache.DepthEnable);
		else
			inst.SetDepthEnable(cache.DepthEnable);

		if (ImGui::Checkbox("DepthWrite", &cache.DepthWriteEnable))
			inst.SetDepthWriteEnable(cache.DepthWriteEnable);
		else
			inst.SetDepthWriteEnable(cache.DepthWriteEnable);

		{
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

			const char* labels[] = { "NEVER","LESS","EQUAL","LEQUAL","GREATER","NOT_EQUAL","GEQUAL","ALWAYS" };

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
			inst.SetDepthFunc(cache.DepthFunc);
		}

		{
			int mode = (cache.TextureBindingMode == MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC) ? 0 : 1;
			const char* items[] = { "DYNAMIC", "MUTABLE" };

			if (ImGui::Combo("TexBinding", &mode, items, 2))
				cache.TextureBindingMode = (mode == 0) ? MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC : MATERIAL_TEXTURE_BINDING_MODE_MUTABLE;

			inst.SetTextureBindingMode(cache.TextureBindingMode);
		}

		InputTextStdString("LinearWrapName", cache.LinearWrapSamplerName);
		inst.SetLinearWrapSamplerName(cache.LinearWrapSamplerName);
		inst.SetLinearWrapSamplerDesc(cache.LinearWrapSamplerDesc);
	}

	bool MaterialEditor::RebuildMainSaveObjectFromScene(std::string* outError)
	{
		if (outError) outError->clear();

		if (!m_pRenderScene)
		{
			if (outError) *outError = "RenderScene is null.";
			return false;
		}

		RenderScene::RenderObject* obj = GetMainRenderObjectOrNull();
		if (!obj)
		{
			if (outError) *outError = "Main RenderObject is null.";
			return false;
		}

		// 베이스 CPU 메시(슬롯/지오메트리 포함)는 기존 save-cache에서 가져온다.
		if (!m_pMainBuiltObjForSave)
		{
			if (outError) *outError = "Save base mesh is missing. Load Main first.";
			return false;
		}

		const StaticMeshAsset* pBaseMesh = AssetObjectCast<StaticMeshAsset>(m_pMainBuiltObjForSave.get());
		if (!pBaseMesh)
		{
			if (outError) *outError = "AssetObjectCast<StaticMeshAsset> failed.";
			return false;
		}

		// 새 메시로 복사 후, MaterialSlot만 현재 인스턴스로 다시 굽기
		StaticMeshAsset bakedMesh = *pBaseMesh;

		MaterialTemplate* pTmpl = GetOrCreateTemplateFromInputs();
		if (!pTmpl)
		{
			if (outError) *outError = "MaterialTemplate is null.";
			return false;
		}

		const uint32 slotCount = (uint32)bakedMesh.GetMaterialSlots().size();
		const uint32 instCount = (uint32)obj->Materials.size();
		const uint32 count = (slotCount < instCount) ? slotCount : instCount;

		// 각 슬롯에 대해: MaterialAsset.Clear() -> Options -> Values -> Resources
		for (uint32 slot = 0; slot < count; ++slot)
		{
			MaterialAsset& dst = bakedMesh.GetMaterialSlot(slot);
			const MaterialInstance& src = obj->Materials[slot];

			dst.Clear();

			// -------------------------
			// Metadata (선택)
			// -------------------------
			dst.SetTemplateKey(MakeTemplateKeyFromInputs());
			dst.SetRenderPassName(src.GetRenderPass());
			// 이름이 필요하면 슬롯 이름/경로 등으로 세팅 가능:
			// dst.SetName(...);

			// -------------------------
			// Options: MaterialInstance의 공통 옵션을 그대로 반영
			// -------------------------
			dst.SetCullMode(src.GetCullMode());
			dst.SetFrontCounterClockwise(src.GetFrontCounterClockwise());

			dst.SetDepthEnable(src.GetDepthEnable());
			dst.SetDepthWriteEnable(src.GetDepthWriteEnable());
			dst.SetDepthFunc(src.GetDepthFunc());

			dst.SetTextureBindingMode(src.GetTextureBindingMode());

			dst.SetLinearWrapSamplerName(src.GetLinearWrapSamplerName());
			dst.SetLinearWrapSamplerDesc(src.GetLinearWrapSamplerDesc());

			// TwoSided / CastShadow는 현재 MaterialInstance에 직접 getter가 없으니
			// (RenderObject 또는 별도 UI 상태에서 굽고 싶다면 여기서 세팅)
			// dst.SetTwoSided(...);
			// dst.SetCastShadow(...);

			// -------------------------
			// Values: 템플릿 리플렉션을 기준으로 CBuffer blob에서 읽어 override 저장
			// - MaterialAsset은 SetRaw로 override를 만들 수 있음
			// -------------------------
			for (uint32 i = 0; i < pTmpl->GetValueParamCount(); ++i)
			{
				const MaterialValueParamDesc& desc = pTmpl->GetValueParam(i);
				if (desc.Name.empty())
					continue;

				uint32 sz = desc.ByteSize;
				if (sz == 0)
				{
					switch (desc.Type)
					{
					case MATERIAL_VALUE_TYPE_FLOAT:    sz = sizeof(float); break;
					case MATERIAL_VALUE_TYPE_FLOAT2:   sz = sizeof(float) * 2; break;
					case MATERIAL_VALUE_TYPE_FLOAT3:   sz = sizeof(float) * 3; break;
					case MATERIAL_VALUE_TYPE_FLOAT4:   sz = sizeof(float) * 4; break;

					case MATERIAL_VALUE_TYPE_INT:      sz = sizeof(int32); break;
					case MATERIAL_VALUE_TYPE_INT2:     sz = sizeof(int32) * 2; break;
					case MATERIAL_VALUE_TYPE_INT3:     sz = sizeof(int32) * 3; break;
					case MATERIAL_VALUE_TYPE_INT4:     sz = sizeof(int32) * 4; break;

					case MATERIAL_VALUE_TYPE_UINT:     sz = sizeof(uint32); break;
					case MATERIAL_VALUE_TYPE_UINT2:    sz = sizeof(uint32) * 2; break;
					case MATERIAL_VALUE_TYPE_UINT3:    sz = sizeof(uint32) * 3; break;
					case MATERIAL_VALUE_TYPE_UINT4:    sz = sizeof(uint32) * 4; break;

					case MATERIAL_VALUE_TYPE_FLOAT4X4: sz = sizeof(float) * 16; break;
					default:                           sz = 0; break;
					}
				}

				if (sz == 0)
					continue;

				const uint32 cbIndex = desc.CBufferIndex;
				if (cbIndex >= src.GetCBufferBlobCount())
					continue;

				const uint8* pCB = src.GetCBufferBlobData(cbIndex);
				const uint32 cbSize = src.GetCBufferBlobSize(cbIndex);
				if (!pCB || desc.ByteOffset + sz > cbSize)
					continue;

				dst.SetRaw(desc.Name.c_str(), desc.Type, pCB + desc.ByteOffset, sz);
			}

			// -------------------------
			// Resources: MaterialInstance의 TextureBinding을 그대로 MaterialAsset에 저장
			// -------------------------
			for (uint32 t = 0; t < src.GetTextureBindingCount(); ++t)
			{
				const TextureBinding& tb = src.GetTextureBinding(t);
				if (tb.Name.empty())
					continue;

				// 템플릿에서 이 리소스의 타입을 찾아 expectedType으로 전달 (정확도/검증용)
				MATERIAL_RESOURCE_TYPE expectedType = MATERIAL_RESOURCE_TYPE_UNKNOWN;
				{
					for (uint32 r = 0; r < pTmpl->GetResourceCount(); ++r)
					{
						const MaterialResourceDesc& resDesc = pTmpl->GetResource(r);
						if (resDesc.Name == tb.Name)
						{
							expectedType = resDesc.Type;
							break;
						}
					}
				}

				if (!tb.TextureRef.has_value() || !tb.TextureRef.value())
				{
					// 현재 바인딩이 비어있으면, 저장에서도 제거 상태 유지
					dst.RemoveResourceBinding(tb.Name.c_str());
					continue;
				}

				dst.SetTextureAssetRef(
					tb.Name.c_str(),
					expectedType,
					tb.TextureRef.value());
				// SamplerOverride는 현재 MaterialInstance에 ISampler*만 있어서
				// desc로 직렬화하려면 별도 매핑/캐시가 필요(지금은 패스)
			}
		}

		// bakedMesh를 새 TypedAssetObject로 감싸서 Save-cache를 최신으로 교체
		m_pMainBuiltObjForSave = std::make_unique<TypedAssetObject<StaticMeshAsset>>(std::move(bakedMesh));
		return true;
	}

	bool MaterialEditor::SaveMainObject(const std::string& outPath, EAssetSaveFlags flags, std::string* outError)
	{
		if (outError) outError->clear();

		ASSERT(m_pAssetManager, "AssetManager is null.");

		if (outPath.empty())
		{
			if (outError) *outError = "Out path is empty.";
			return false;
		}

		// ? Save 직전에 현재 UI/Scene 상태를 CPU mesh에 반영
		{
			std::string err;
			if (!RebuildMainSaveObjectFromScene(&err))
			{
				if (outError) *outError = err;
				return false;
			}
		}

		StaticMeshAssetExporter exporter = {};

		AssetMeta meta = {};
		meta.TypeID = AssetTypeTraits<StaticMeshAsset>::TypeID;
		meta.SourcePath = m_MainMeshPath;

		std::string err;
		const bool ok = exporter(
			*m_pAssetManager,
			meta,
			m_pMainBuiltObjForSave.get(),
			outPath,
			&err);

		if (!ok)
		{
			if (outError) *outError = err;
			return false;
		}

		return true;
	}



	void MaterialEditor::DrawValueEditor(MaterialInstance& inst, MaterialUiCache& cache, const MaterialTemplate& tmpl)
	{
		ImGui::TextDisabled("Reflection-driven (Template -> Values).");
		ImGui::Separator();

		const uint32 count = tmpl.GetValueParamCount();
		if (count == 0)
		{
			ImGui::TextDisabled("No value params.");
			return;
		}

		static char s_Filter[128] = {};
		ImGui::InputTextWithHint("Filter", "name contains...", s_Filter, sizeof(s_Filter));

		auto passFilter = [&](const std::string& name) -> bool
			{
				if (s_Filter[0] == 0)
					return true;
				return name.find(s_Filter) != std::string::npos;
			};

		for (uint32 i = 0; i < count; ++i)
		{
			const MaterialValueParamDesc& desc = tmpl.GetValueParam(i);
			if (desc.Name.empty())
				continue;

			if (!passFilter(desc.Name))
				continue;

			auto& bytes = cache.ValueBytes[desc.Name];

			const uint32 expectedSize = (desc.ByteSize != 0) ? desc.ByteSize : (uint32)bytes.size();
			if (expectedSize != 0 && (uint32)bytes.size() != expectedSize)
			{
				bytes.resize(expectedSize);
				std::memset(bytes.data(), 0, bytes.size());
			}

			ImGui::PushID((int)i);

			bool changed = false;

			switch (desc.Type)
			{
			case MATERIAL_VALUE_TYPE_FLOAT:
			{
				float v = 0.0f;
				if (bytes.size() >= sizeof(float)) std::memcpy(&v, bytes.data(), sizeof(float));
				if (ImGui::DragFloat(desc.Name.c_str(), &v, 0.01f))
				{
					changed = true;
					std::memcpy(bytes.data(), &v, sizeof(float));
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_FLOAT2:
			{
				float v[2] = {};
				if (bytes.size() >= sizeof(float) * 2) std::memcpy(v, bytes.data(), sizeof(float) * 2);
				if (ImGui::DragFloat2(desc.Name.c_str(), v, 0.01f))
				{
					changed = true;
					std::memcpy(bytes.data(), v, sizeof(float) * 2);
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_FLOAT3:
			{
				float v[3] = {};
				if (bytes.size() >= sizeof(float) * 3) std::memcpy(v, bytes.data(), sizeof(float) * 3);

				const bool isColor =
					(desc.Name.find("Color") != std::string::npos) ||
					(desc.Name.find("Albedo") != std::string::npos) ||
					(desc.Name.find("BaseColor") != std::string::npos);

				if (isColor)
				{
					if (ImGui::ColorEdit3(desc.Name.c_str(), v))
					{
						changed = true;
						std::memcpy(bytes.data(), v, sizeof(float) * 3);
					}
				}
				else
				{
					if (ImGui::DragFloat3(desc.Name.c_str(), v, 0.01f))
					{
						changed = true;
						std::memcpy(bytes.data(), v, sizeof(float) * 3);
					}
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_FLOAT4:
			{
				float v[4] = {};
				if (bytes.size() >= sizeof(float) * 4) std::memcpy(v, bytes.data(), sizeof(float) * 4);

				const bool isColor =
					(desc.Name.find("Color") != std::string::npos) ||
					(desc.Name.find("Albedo") != std::string::npos) ||
					(desc.Name.find("BaseColor") != std::string::npos);

				if (isColor)
				{
					if (ImGui::ColorEdit4(desc.Name.c_str(), v))
					{
						changed = true;
						std::memcpy(bytes.data(), v, sizeof(float) * 4);
					}
				}
				else
				{
					if (ImGui::DragFloat4(desc.Name.c_str(), v, 0.01f))
					{
						changed = true;
						std::memcpy(bytes.data(), v, sizeof(float) * 4);
					}
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_INT:
			{
				int32 v = 0;
				if (bytes.size() >= sizeof(int32)) std::memcpy(&v, bytes.data(), sizeof(int32));
				if (ImGui::DragInt(desc.Name.c_str(), &v, 1.0f))
				{
					changed = true;
					std::memcpy(bytes.data(), &v, sizeof(int32));
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_UINT:
			{
				uint32 v = 0;
				if (bytes.size() >= sizeof(uint32)) std::memcpy(&v, bytes.data(), sizeof(uint32));
				int tmp = (int)v;
				if (ImGui::DragInt(desc.Name.c_str(), &tmp, 1.0f, 0, INT32_MAX))
				{
					v = (uint32)std::max(0, tmp);
					changed = true;
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
					changed = true;
					bytes.resize(sizeof(float) * 16);

					const float m[16] =
					{
						1,0,0,0,
						0,1,0,0,
						0,0,1,0,
						0,0,0,1
					};
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

			if (changed)
			{
				inst.SetValue(desc.Name.c_str(), bytes.data(), desc.Type);
				inst.MarkAllDirty();
			}

			ImGui::PopID();
		}
	}

	void MaterialEditor::DrawResourceEditor(MaterialInstance& inst, MaterialUiCache& cache, const MaterialTemplate& tmpl)
	{
		ImGui::TextDisabled("Reflection-driven (Template -> Resources).");
		ImGui::Separator();

		const uint32 count = tmpl.GetResourceCount();
		if (count == 0)
		{
			ImGui::TextDisabled("No resource params.");
			return;
		}

		static char s_Filter[128] = {};
		ImGui::InputTextWithHint("Filter", "name contains...", s_Filter, sizeof(s_Filter));

		auto passFilter = [&](const std::string& name) -> bool
			{
				if (s_Filter[0] == 0)
					return true;
				return name.find(s_Filter) != std::string::npos;
			};

		for (uint32 i = 0; i < count; ++i)
		{
			const MaterialResourceDesc& desc = tmpl.GetResource(i);
			if (desc.Name.empty())
				continue;

			if (!passFilter(desc.Name))
				continue;

			const bool isTexture =
				(desc.Type == MATERIAL_RESOURCE_TYPE_TEXTURE2D) ||
				(desc.Type == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ||
				(desc.Type == MATERIAL_RESOURCE_TYPE_TEXTURECUBE);

			if (!isTexture)
				continue;

			ImGui::PushID((int)i);

			std::string& path = cache.TexturePaths[desc.Name];

			ImGui::Text("%s", desc.Name.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)",
				(desc.Type == MATERIAL_RESOURCE_TYPE_TEXTURECUBE) ? "Cube" :
				(desc.Type == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ? "2DArray" : "2D");

			InputTextStdString("Path", path);

			ImGui::SameLine();
			const bool applyPressed = ImGui::Button("Apply");
			ImGui::SameLine();
			const bool clearPressed = ImGui::Button("Clear");

			const uint32 flagBit = FlagFromTextureName(desc.Name);

			if (clearPressed)
			{
				path.clear();

				if (flagBit != 0)
				{
					auto& fb = cache.ValueBytes["g_MaterialFlags"];
					if (fb.size() != sizeof(uint32))
					{
						fb.resize(sizeof(uint32));
						std::memset(fb.data(), 0, fb.size());
					}

					uint32* pMatFlag = reinterpret_cast<uint32*>(fb.data());
					*pMatFlag &= ~flagBit;
					inst.SetUint("g_MaterialFlags", *pMatFlag);
				}

				inst.ClearTextureAssetRef(desc.Name.c_str());
				inst.MarkAllDirty();
			}

			if (applyPressed && !clearPressed)
			{
				const std::string p = SanitizeFilePath(path);
				if (!p.empty())
				{
					AssetRef<TextureAsset> ref = RegisterTexturePath(p);
					inst.SetTextureAssetRef(desc.Name.c_str(), ref);

					if (flagBit != 0)
					{
						auto& fb = cache.ValueBytes["g_MaterialFlags"];
						if (fb.size() != sizeof(uint32))
						{
							fb.resize(sizeof(uint32));
							std::memset(fb.data(), 0, fb.size());
						}

						uint32* pMatFlag = reinterpret_cast<uint32*>(fb.data());
						*pMatFlag |= flagBit;
						inst.SetUint("g_MaterialFlags", *pMatFlag);
					}

					inst.MarkAllDirty();
				}
			}

			ImGui::PopID();
			ImGui::Separator();
		}
	}

	// ------------------------------------------------------------
	// Scene load
	// ------------------------------------------------------------

	static bool isAssimpSourceExt(const std::string& path)
	{
		std::string lower = path;
		for (char& c : lower) c = (char)tolower(c);

		auto endsWith = [&](const char* ext)
			{
				const size_t n = std::strlen(ext);
				return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
			};

		// Assimp inputs only
		return endsWith(".fbx") || endsWith(".gltf") || endsWith(".glb") || endsWith(".obj");
	}


	bool MaterialEditor::LoadPreviewMesh(
		EPreviewObject which,
		const char* path,
		float3 position,
		float3 rotation,
		float3 scale,
		bool bUniformScale,
		bool bCastShadow,
		bool bAlphaMasked)
	{
		if (!path || path[0] == '\0')
			return false;

		ASSERT(m_pAssetManager, "AssetManager is null.");
		ASSERT(m_pRenderScene, "RenderScene is null.");
		ASSERT(m_pRenderer, "Renderer is null.");

		LoadedMesh* target = (which == EPreviewObject::Floor) ? &m_Floor : &m_Main;

		// Replace existing
		if (m_pRenderScene && target->ObjectId.IsValid())
		{
			m_pRenderScene->RemoveObject(target->ObjectId);
			*target = {};
		}

		LoadedMesh entry = {};
		entry.Path = path;
		entry.bCastShadow = bCastShadow;
		entry.bAlphaMasked = bAlphaMasked;

		// ------------------------------------------------------------
		// CPU mesh 준비 (Assimp source -> BuildStaticMeshAsset / else StaticMeshAsset load)
		// ------------------------------------------------------------
		std::unique_ptr<StaticMeshAsset> builtCpuMesh;
		const StaticMeshAsset* pCpuMesh = nullptr;

		std::string err;

		const std::string srcPath = entry.Path;
		if (isAssimpSourceExt(srcPath))
		{
			// 1) load AssimpAsset
			const AssetRef<AssimpAsset> assimpRef = m_pAssetManager->RegisterAsset<AssimpAsset>(srcPath);
			AssetPtr<AssimpAsset> assimpPtr = m_pAssetManager->LoadBlocking(assimpRef);

			const AssimpAsset* pAssimp = assimpPtr.Get();
			if (!pAssimp)
				return false;

			// 2) build StaticMeshAsset (temporary in-memory)
			builtCpuMesh = std::make_unique<StaticMeshAsset>();

			if (!BuildStaticMeshAsset(*pAssimp, builtCpuMesh.get(), m_MainImportSettings, &err, m_pAssetManager.get()))
			{
				ASSERT(false, err.empty() ? "BuildStaticMeshAsset failed." : err.c_str());
				return false;
			}

			pCpuMesh = builtCpuMesh.get();

			// (Save용) Main일 때만 보관
			if (which == EPreviewObject::Main)
				m_pMainBuiltObjForSave = std::make_unique<TypedAssetObject<StaticMeshAsset>>(*builtCpuMesh);
		}
		else
		{
			// 이미 내 포맷(.json+.bin 등)으로 저장된 StaticMeshAsset을 로드
			entry.MeshRef = m_pAssetManager->RegisterAsset<StaticMeshAsset>(srcPath);
			entry.MeshPtr = m_pAssetManager->LoadBlocking(entry.MeshRef);

			pCpuMesh = entry.MeshPtr.Get();
			if (!pCpuMesh)
				return false;

			// Main이 내 포맷으로 로드된 케이스: Save는 그냥 “재저장”만 하면 되지만
			// 지금 최소 구현은 exporter 직접 호출이라 CPU mesh 확보가 필요 -> 복사 보관
			if (which == EPreviewObject::Main)
			{
				m_pMainBuiltObjForSave = std::make_unique<TypedAssetObject<StaticMeshAsset>>(*pCpuMesh);
			}
		}

		ASSERT(pCpuMesh, "CPU mesh is null.");

		// ------------------------------------------------------------
		// Scale
		// ------------------------------------------------------------
		if (bUniformScale)
		{
			const float s = ComputeUniformScaleToFitUnitCube(pCpuMesh->GetBounds(), 1.0f);
			entry.Scale = float3(s, s, s);
		}
		else
		{
			entry.Scale = scale;
		}

		entry.Position = position;
		entry.BaseRotation = rotation;

		// ------------------------------------------------------------
		// GPU mesh
		// ------------------------------------------------------------
		entry.MeshHandle = m_pRenderer->CreateStaticMesh(*pCpuMesh);
		if (!entry.MeshHandle.IsValid())
			return false;

		// Materials
		std::vector<MaterialInstance> mats = BuildMaterialsForCpuMeshSlots(*pCpuMesh);
		if (mats.empty() && !pCpuMesh->GetMaterialSlots().empty())
			return false;

		RenderScene::RenderObject obj = {};
		obj.MeshHandle = entry.MeshHandle;
		obj.Materials = std::move(mats);
		obj.Transform = Matrix4x4::TRS(entry.Position, entry.BaseRotation, entry.Scale);
		obj.bCastShadow = entry.bCastShadow;
		obj.bAlphaMasked = entry.bAlphaMasked;

		entry.ObjectId = m_pRenderScene->AddObject(std::move(obj));
		if (!entry.ObjectId.IsValid())
			return false;

		entry.SceneObjectIndex = (int32)m_pRenderScene->GetObjects().size() - 1;

		*target = std::move(entry);
		return true;
	}

	bool MaterialEditor::ImportMainFromSavedPath(const std::string& savedPath)
	{
		const std::string p = SanitizeFilePath(savedPath);
		if (p.empty())
			return false;

		// UI에 보이는 경로도 갱신 (다음에 다시 로드하기 쉽게)
		m_MainMeshPath = p;

		// 지금 Main에 설정돼 있는 트랜스폼/옵션 그대로 사용해서 교체 로드
		const bool ok = LoadPreviewMesh(
			EPreviewObject::Main,
			p.c_str(),
			m_MainMeshPos,
			m_MainMeshRot,
			m_MainMeshScale,
			m_MainMeshUniformScale,
			m_MainMeshCastShadow,
			m_MainMeshAlphaMasked);

		if (ok)
		{
			m_SelectedMaterialSlot = 0;
			m_MaterialUi[MakeSelectionKeyForMain(m_SelectedMaterialSlot)].Dirty = true;
		}
		return ok;
	}



	std::vector<MaterialInstance> MaterialEditor::BuildMaterialsForCpuMeshSlots(const StaticMeshAsset& cpuMesh)
	{
		MaterialTemplate* pTemplate = GetOrCreateTemplateFromInputs();
		ASSERT(pTemplate, "Template is null.");

		std::vector<MaterialInstance> materials;
		materials.reserve(cpuMesh.GetMaterialSlots().size());

		for (uint32 i = 0; i < (uint32)cpuMesh.GetMaterialSlots().size(); ++i)
		{
			const MaterialAsset& slot = cpuMesh.GetMaterialSlot(i);

			MaterialInstance mat = {};
			{
				const bool ok = mat.Initialize(pTemplate, "MaterialEditor Instance");
				ASSERT(ok, "MaterialInstance::Initialize failed.");
			}

			// ------------------------------------------------------------
			// 1) 슬롯에 저장된 '진짜' 머터리얼 데이터 적용
			//    - Options (Cull/Depth/BindingMode/SamplerName/Desc, etc)
			//    - ValueOverrides
			//    - ResourceBindings(TextureRef + optional sampler override desc)
			// ------------------------------------------------------------
			{
				const bool ok = slot.ApplyToInstance(&mat);
				ASSERT(ok, "MaterialAsset::ApplyToInstance failed.");
			}

			// ------------------------------------------------------------
			// 2) 에디터/파이프라인 정책 보정
			//    - RenderPass는 Asset에 저장돼있지 않으니 기본값을 지정
			//    - (slot 옵션으로 subpass를 저장하는 구조가 있다면 여기서 반영)
			// ------------------------------------------------------------
			mat.SetRenderPass(slot.GetRenderPassName());

			// RenderScene/MaterialRenderData가 재빌드하도록
			mat.MarkAllDirty();

			materials.push_back(std::move(mat));
		}

		return materials;
	}


	// ------------------------------------------------------------
	// Utils
	// ------------------------------------------------------------

	std::string MaterialEditor::SanitizeFilePath(std::string s)
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
} // namespace shz
