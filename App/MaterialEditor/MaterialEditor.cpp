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
			case MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC: return "DYNAMIC";
			case MATERIAL_TEXTURE_BINDING_MODE_MUTABLE: return "MUTABLE";
			default: return "UNKNOWN";
			}
		}
	}

	SampleBase* CreateSample()
	{
		return new MaterialEditor();
	}

	// ------------------------------------------------------------
	// AssetManager integration
	// ------------------------------------------------------------

	void MaterialEditor::registerAssetLoaders()
	{
		ASSERT(m_pAssetManager, "AssetManager is null.");
		m_pAssetManager->RegisterLoader(AssetTypeTraits<StaticMeshAsset>::TypeID, StaticMeshImporter{});
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
	// Template
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

		auto [insIt, _] = m_TemplateCache.emplace(key, static_cast<MaterialTemplate&&>(tmpl));

		// Template changed -> mark selected cache dirty so it re-syncs defaults
		const uint64 selKey = makeSelectionKey(m_SelectedObject, m_SelectedMaterialSlot);
		if (auto itCache = m_MaterialUi.find(selKey); itCache != m_MaterialUi.end())
			itCache->second.Dirty = true;

		return &insIt->second;
	}

	// ------------------------------------------------------------
	// Initialize / Render / Update
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

		// Camera
		m_Camera.SetProjAttribs(
			0.1f,
			100.0f,
			static_cast<float>(rendererCreateInfo.BackBufferWidth) / rendererCreateInfo.BackBufferHeight,
			PI / 4.0f,
			SURFACE_TRANSFORM_IDENTITY);

		m_ViewFamily.Views.clear();
		m_ViewFamily.Views.push_back({});

		// Preset 0
		m_SelectedPresetIndex = 0;
		m_ShaderVS = g_PresetVS[0];
		m_ShaderPS = g_PresetPS[0];
		m_VSEntry = g_PresetVSEntry[0];
		m_PSEntry = g_PresetPSEntry[0];

		(void)getOrCreateTemplateFromInputs();

		// Preview models
		loadPreviewMesh(
			"C:/Dev/ShizenEngine/Assets/Basic/floor/FbxFloor.fbx",
			{ -2.0f, -0.5f, 3.0f }, { 0, 0, 0 }, { 1, 1, 1 }, false);

		//loadPreviewMesh(
		//	"C:/Dev/ShizenEngine/Assets/Basic/DamagedHelmet/glTF/DamagedHelmet.gltf",
		//	{ 0.0f, 0.0f, 3.0f }, { 0, 0, 0 }, { 1, 1, 1 }, true);

		loadPreviewMesh(
			"C:/Dev/ShizenEngine/Assets/Grass/grass-free-download/source/grass.fbx",
			{ 0.0f, -0.5f, 3.0f }, { 0, 0, 0 }, { 1, 1, 1 }, true);

		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);

		m_SelectedObject = (m_Loaded.empty() ? -1 : 0);
		m_SelectedMaterialSlot = 0;
	}

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

		// NOTE: viewport-focused camera control (optional)
		// - If you want camera to move only when viewport is hovered/focused:
		//   gate m_Camera.Update() by m_Viewport.Hovered or m_Viewport.Focused.
		m_Camera.Update(m_InputController, dt);

		m_ViewFamily.DeltaTime = dt;
		m_ViewFamily.CurrentTime = currTime;

		if (!m_ViewFamily.Views.empty())
		{
			m_ViewFamily.Views[0].Viewport.left = 0;
			m_ViewFamily.Views[0].Viewport.top = 0;
			m_ViewFamily.Views[0].Viewport.right = m_Viewport.Width;
			m_ViewFamily.Views[0].Viewport.bottom = m_Viewport.Height;

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
		// Swapchain resize는 SampleBase가 처리.
		SampleBase::WindowResize(Width, Height);

		// 여기서 Renderer::OnResize(Width,Height)를 호출하면 “viewport 기반 리사이즈”랑 충돌함.
		// => viewport 창 크기 기반으로 UpdateUI에서만 Renderer::OnResize() 호출.
	}

	// ------------------------------------------------------------
	// UI
	// ------------------------------------------------------------

	void MaterialEditor::UpdateUI()
	{
		uiDockspace();
		uiScenePanel();
		uiViewportPanel();
		uiMaterialInspector();
		uiStatsPanel();
	}

	void MaterialEditor::uiDockspace()
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags hostFlags =
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

			// Build default layout once
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

			// Menu bar
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("Material"))
				{
					if (ImGui::MenuItem("Rebuild Template Cache"))
					{
						m_TemplateCache.clear();
						(void)getOrCreateTemplateFromInputs();
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("View"))
				{
					ImGui::MenuItem("Stats", nullptr, true);
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

		ImGui::Text("Objects");
		ImGui::Separator();

		for (int32 i = 0; i < (int32)m_Loaded.size(); ++i)
		{
			const bool selected = (m_SelectedObject == i);
			const char* label = m_Loaded[(size_t)i].Path.c_str();

			if (ImGui::Selectable(label, selected))
			{
				m_SelectedObject = i;
				m_SelectedMaterialSlot = 0;

				const uint64 key = makeSelectionKey(m_SelectedObject, m_SelectedMaterialSlot);
				m_MaterialUi[key].Dirty = true;
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Light");

		ImGui::gizmo3D("##LightDirection", m_GlobalLight.Direction, ImGui::GetTextLineHeight() * 7);
		ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&m_GlobalLight.Color));
		ImGui::SliderFloat("Intensity", &m_GlobalLight.Intensity, 0.01f, 10.0f);

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

		// Content region size => render size
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const uint32 newW = (uint32)std::max(1.0f, avail.x);
		const uint32 newH = (uint32)std::max(1.0f, avail.y);

		if (newW != m_Viewport.Width || newH != m_Viewport.Height)
		{
			m_Viewport.Width = newW;
			m_Viewport.Height = newH;

			// 1) Update camera aspect
			m_Camera.SetProjAttribs(
				m_Camera.GetProjAttribs().NearClipPlane,
				m_Camera.GetProjAttribs().FarClipPlane,
				(float)newW / (float)newH,
				PI / 4.0f,
				SURFACE_TRANSFORM_IDENTITY);

			// 2) Resize renderer deferred targets to viewport size
			if (m_pRenderer)
				m_pRenderer->OnResize(newW, newH);
		}

		// Draw the final color buffer inside the viewport.
		ITextureView* pColor = m_pRenderer ? m_pRenderer->GetLightingSRV() : nullptr;
		if (pColor)
		{
			ImTextureID tid = reinterpret_cast<ImTextureID>(pColor);

			const ImVec2 imgSize = ImVec2((float)m_Viewport.Width, (float)m_Viewport.Height);
			ImGui::Image(tid, imgSize, ImVec2(0, 1), ImVec2(1, 0));
		}
		else
		{
			ImGui::TextDisabled("No renderer output.");
		}

		ImGui::End();
	}

	void MaterialEditor::uiMaterialInspector()
	{
		if (!ImGui::Begin("Material"))
		{
			ImGui::End();
			return;
		}

		// Shader preset / template
		ImGui::Text("Template");
		ImGui::Separator();

		if (ImGui::Combo("Preset", &m_SelectedPresetIndex, g_PresetLabels, (int)std::size(g_PresetLabels)))
		{
			if (m_SelectedPresetIndex >= 0 && m_SelectedPresetIndex < (int)std::size(g_PresetLabels))
			{
				if (g_PresetVS[m_SelectedPresetIndex])
				{
					m_ShaderVS = g_PresetVS[m_SelectedPresetIndex];
					m_ShaderPS = g_PresetPS[m_SelectedPresetIndex];
					m_VSEntry = g_PresetVSEntry[m_SelectedPresetIndex];
					m_PSEntry = g_PresetPSEntry[m_SelectedPresetIndex];
				}
			}
		}

		imGuiInputString("VS", m_ShaderVS);
		imGuiInputString("VSEntry", m_VSEntry);
		imGuiInputString("PS", m_ShaderPS);
		imGuiInputString("PSEntry", m_PSEntry);

		if (ImGui::Button("Get/Create Template"))
		{
			MaterialTemplate* pNewTmpl = rebuildTemplateFromInputs();

			// 선택 슬롯 UI 캐시 더티 (템플릿 리플렉션 갱신 필요)
			const uint64 key = makeSelectionKey(m_SelectedObject, m_SelectedMaterialSlot);
			m_MaterialUi[key].Dirty = true;

			// “진짜 적용”: 현재 선택된 MaterialInstance를 새 템플릿으로 갈아끼움
			rebindSelectedMaterialToTemplate(pNewTmpl);
		}

		ImGui::Spacing();
		ImGui::Separator();

		MaterialInstance* pInst = getSelectedMaterialOrNull();
		MaterialTemplate* pTmpl = getOrCreateTemplateFromInputs();

		if (!pInst || !pTmpl)
		{
			ImGui::TextDisabled("Select an object/material slot.");
			ImGui::End();
			return;
		}

		// Slot selector
		{
			int slot = m_SelectedMaterialSlot;
			const int maxSlot = (int)m_pRenderScene->GetObjects()[(size_t)m_Loaded[(size_t)m_SelectedObject].SceneObjectIndex].Materials.size();

			if (ImGui::SliderInt("Slot", &slot, 0, std::max(0, maxSlot - 1)))
			{
				m_SelectedMaterialSlot = slot;

				const uint64 key = makeSelectionKey(m_SelectedObject, m_SelectedMaterialSlot);
				m_MaterialUi[key].Dirty = true;
			}
		}

		MaterialUiCache& cache = getOrCreateMaterialCache(makeSelectionKey(m_SelectedObject, m_SelectedMaterialSlot));
		if (cache.Dirty)
		{
			syncCacheFromInstance(cache, *pInst, *pTmpl);
			cache.Dirty = false;
		}

		// Pipeline
		if (ImGui::CollapsingHeader("Pipeline", ImGuiTreeNodeFlags_DefaultOpen))
		{
			drawPipelineEditor(*pInst, cache);
		}

		// Values
		if (ImGui::CollapsingHeader("Values", ImGuiTreeNodeFlags_DefaultOpen))
		{
			drawValueEditor(*pInst, cache, *pTmpl);
		}

		// Resources
		if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen))
		{
			drawResourceEditor(*pInst, cache, *pTmpl);
		}

		if (ImGui::Button("MarkAllDirty"))
		{
			pInst->MarkAllDirty();
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
		ImGui::Text("Selected: obj=%d slot=%d", m_SelectedObject, m_SelectedMaterialSlot);

		ImGuiIO& io = ImGui::GetIO();
		ImGui::Text("ImGui FPS: %.1f", io.Framerate);

		ImGui::End();
	}

	// ------------------------------------------------------------
	// Selection / Cache
	// ------------------------------------------------------------

	uint64 MaterialEditor::makeSelectionKey(int32 objIndex, int32 slotIndex) const
	{
		uint64 k = 0;
		k |= (uint64)(uint32)objIndex;
		k <<= 32;
		k |= (uint64)(uint32)slotIndex;
		return k;
	}

	MaterialInstance* MaterialEditor::getSelectedMaterialOrNull()
	{
		if (!m_pRenderScene)
			return nullptr;

		if (m_SelectedObject < 0 || m_SelectedObject >= (int32)m_Loaded.size())
			return nullptr;

		LoadedMesh& sel = m_Loaded[(size_t)m_SelectedObject];
		if (sel.SceneObjectIndex < 0 || sel.SceneObjectIndex >= (int32)m_pRenderScene->GetObjects().size())
			return nullptr;

		auto& obj = m_pRenderScene->GetObjects()[(size_t)sel.SceneObjectIndex];
		if (m_SelectedMaterialSlot < 0 || m_SelectedMaterialSlot >= (int32)obj.Materials.size())
			return nullptr;

		return &obj.Materials[(size_t)m_SelectedMaterialSlot];
	}

	MaterialEditor::MaterialUiCache& MaterialEditor::getOrCreateMaterialCache(uint64 key)
	{
		auto it = m_MaterialUi.find(key);
		if (it != m_MaterialUi.end())
			return it->second;

		MaterialUiCache cache = {};
		cache.Dirty = true;

		auto [insIt, _] = m_MaterialUi.emplace(key, static_cast<MaterialUiCache&&>(cache));
		return insIt->second;
	}

	// ------------------------------------------------------------
	// Reflection-driven cache init/apply
	// ------------------------------------------------------------

	MaterialTemplate* MaterialEditor::rebuildTemplateFromInputs()
	{
		if (m_ShaderVS.empty() || m_ShaderPS.empty() || m_VSEntry.empty() || m_PSEntry.empty())
			return nullptr;

		const std::string key = makeTemplateKeyFromInputs();
		m_TemplateCache.erase(key); // <- 핵심: 같은 키여도 강제 재생성

		return getOrCreateTemplateFromInputs();
	}

	void MaterialEditor::rebindSelectedMaterialToTemplate(MaterialTemplate* pNewTmpl)
	{
		if (!pNewTmpl || !m_pRenderScene) return;

		if (m_SelectedObject < 0 || m_SelectedObject >= (int32)m_Loaded.size()) return;

		LoadedMesh& sel = m_Loaded[(size_t)m_SelectedObject];
		if (sel.SceneObjectIndex < 0 || sel.SceneObjectIndex >= (int32)m_pRenderScene->GetObjects().size()) return;

		auto& obj = m_pRenderScene->GetObjects()[(size_t)sel.SceneObjectIndex];
		if (m_SelectedMaterialSlot < 0 || m_SelectedMaterialSlot >= (int32)obj.Materials.size()) return;

		MaterialInstance& oldInst = obj.Materials[(size_t)m_SelectedMaterialSlot];

		const uint64 key = makeSelectionKey(m_SelectedObject, m_SelectedMaterialSlot);
		MaterialUiCache& cache = getOrCreateMaterialCache(key);

		MaterialInstance newInst = {};
		{
			const bool ok = newInst.Initialize(pNewTmpl, "MaterialEditor Instance");
			ASSERT(ok, "MaterialInstance::Initialize failed.");
		}

		// ★ 핵심: cache -> newInst 적용
		applyCacheToInstance(newInst, cache, *pNewTmpl);

		// 교체
		oldInst = std::move(newInst);

		// 강제 재빌드
		oldInst.MarkAllDirty();
	}



	void MaterialEditor::syncCacheFromInstance(
		MaterialUiCache& cache,
		const MaterialInstance& inst,
		const MaterialTemplate& tmpl)
	{
		// 기존 cache 값도 최대한 보존(템플릿 변경/누락 대비)
		auto oldValues = std::move(cache.ValueBytes);
		auto oldTextures = std::move(cache.TexturePaths);

		cache.ValueBytes.clear();
		cache.TexturePaths.clear();

		// ---------------------------------------------------------------------
		// Pipeline: inst에서 읽을 getter가 현재 없으니
		// - "기존 cache 값" 우선 유지
		// - cache가 완전 비었을 때만 기본값 채움 (안전)
		// ---------------------------------------------------------------------
		if (cache.RenderPassName.empty())
		{
			// old cache가 있었다면 그대로 유지, 아니면 기본값
			if (!oldValues.empty() || !oldTextures.empty())
			{
				// cache.RenderPassName은 move 이후라 비어있음.
				// old cache 구조에 RenderPassName을 저장했다면 여기서 가져오긴 어려우니
				// "빈 값이면 기본값"으로 두자.
				cache.RenderPassName = "GBuffer";
			}
			else
			{
				cache.RenderPassName = "GBuffer";
			}
		}

		// 나머지 파이프라인 옵션들도 “이전 cache 유지” 전략
		// (MaterialUiCache에 해당 멤버들이 존재한다는 전제)
		// 없으면 아래 블록은 삭제해도 됨.
		{
			// NOTE: move 이후 cache 멤버들은 그대로 남아있을 수도 있고,
			// 네 코드상 cache는 struct라면 기본 생성값일 가능성이 큼.
			// 여기선 "현재 값이 default-like면 기본값 채움" 정도로만 처리.
			// - 정확히 inst 상태를 반영하려면 MaterialInstance에 GetXXX() 추가하는 게 정석.
		}

		// ---------------------------------------------------------------------
		// Values: tmpl param 목록 기준으로 cache 엔트리 만들고,
		//   1) inst CBufferBlob에서 읽을 수 있으면 inst 값으로
		//   2) 아니면 old cache에서 복원
		//   3) 아니면 0
		// ---------------------------------------------------------------------
		for (uint32 i = 0; i < tmpl.GetValueParamCount(); ++i)
		{
			const MaterialValueParamDesc& desc = tmpl.GetValueParam(i);
			if (desc.Name.empty())
				continue;

			uint32 sz = desc.ByteSize;
			if (sz == 0)
			{
				// fallback
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
				memset(bytes.data(), 0, bytes.size());

			bool filled = false;

			// 1) inst -> cache (CBuffer blobs)
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
						memcpy(bytes.data(), pCB + desc.ByteOffset, sz);
						filled = true;
					}
				}
			}

			// 2) old cache -> cache
			if (!filled)
			{
				auto itOld = oldValues.find(desc.Name);
				if (itOld != oldValues.end())
				{
					const auto& old = itOld->second;
					const size_t copySz = std::min(old.size(), bytes.size());
					if (copySz > 0)
						memcpy(bytes.data(), old.data(), copySz);
					filled = true;
				}
			}

			cache.ValueBytes.emplace(desc.Name, std::move(bytes));
		}

		// ---------------------------------------------------------------------
		// Resources (textures):
		//   1) inst.TextureBindings에서 이름 매칭 -> AssetRef 있으면 cache에 기록
		//   2) 없으면 old cache 유지
		//
		// NOTE:
		// - UI에서 "Path"를 보여주려면 AssetRef -> SourcePath 해석이 필요함.
		// - 지금은 cache.TexturePaths에 "경로 문자열"을 저장하는 구조라서
		//   inst에서 ref만 있을 때는 경로를 못 만들 수도 있음.
		//   => 일단 old cache 유지하고, old에 없으면 빈 문자열로 둔다.
		// ---------------------------------------------------------------------
		// inst의 바인딩을 이름->ref로 빠르게 찾기 위한 선형 검색(개수 적으면 OK)
		auto findInstTexRefByName = [&](const std::string& name, std::optional<AssetRef<TextureAsset>>& outRef) -> bool
		{
			const uint32 n = inst.GetTextureBindingCount();
			for (uint32 t = 0; t < n; ++t)
			{
				const TextureBinding& b = inst.GetTextureBinding(t);
				if (b.Name == name)
				{
					outRef = b.TextureRef;
					return true;
				}
			}
			return false;
		};

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

			std::string path;

			// 1) inst -> (optional ref) -> (path?)
			// 현재 공개 API로는 ref->path 변환 불가.
			// 대신 old cache에 path가 있으면 유지, 없으면 빈 문자열.
			{
				std::optional<AssetRef<TextureAsset>> refOpt;
				if (findInstTexRefByName(resDesc.Name, refOpt))
				{
					// ref는 존재할 수 있지만 path는 모름.
					// 네 UI는 path 문자열을 바탕으로 registerTexturePath 하니까
					// 기존에 한번 입력/로딩된 적이 있으면 old cache에 path가 남아있을 것.
					// -> old cache 우선 유지 전략으로 간다.
				}
			}

			// 2) old cache -> path 유지
			{
				auto itOld = oldTextures.find(resDesc.Name);
				if (itOld != oldTextures.end())
					path = itOld->second;
			}

			cache.TexturePaths.emplace(resDesc.Name, std::move(path));
		}
	}

	// MaterialEditor.cpp (MaterialEditor::applyCacheToInstance)

	static uint32 flagFromTextureName(const std::string& n) noexcept
	{
		if (n == "g_BaseColorTex")         return MAT_HAS_BASECOLOR;
		if (n == "g_NormalTex")            return MAT_HAS_NORMAL;
		if (n == "g_MetallicRoughnessTex") return MAT_HAS_MR;
		if (n == "g_AOTex")                return MAT_HAS_AO;
		if (n == "g_EmissiveTex")          return MAT_HAS_EMISSIVE;
		if (n == "g_HeightTex")            return MAT_HAS_HEIGHT;
		return 0;
	}

	void MaterialEditor::applyCacheToInstance(
		MaterialInstance& inst,
		MaterialUiCache& cache,
		const MaterialTemplate& tmpl)
	{
		// ------------------------------------------------------------
		// Pipeline (cache -> inst)
		// ------------------------------------------------------------
		if (inst.GetPipelineType() == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			IRenderPass* rp = nullptr;
			if (m_pRenderer && !cache.RenderPassName.empty())
				rp = m_pRenderer->GetRenderPassOrNull(cache.RenderPassName);

			inst.SetRenderPass(rp, cache.SubpassIndex);

			inst.SetCullMode(cache.CullMode);
			inst.SetFrontCounterClockwise(cache.FrontCounterClockwise);

			inst.SetDepthEnable(cache.DepthEnable);
			inst.SetDepthWriteEnable(cache.DepthWriteEnable);
			inst.SetDepthFunc(cache.DepthFunc);

			inst.SetTextureBindingMode(cache.TextureBindingMode);

			inst.SetLinearWrapSamplerName(cache.LinearWrapSamplerName);
			inst.SetLinearWrapSamplerDesc(cache.LinearWrapSamplerDesc);
		}

		// ------------------------------------------------------------
		// Values (cache.ValueBytes -> inst)
		// - tmpl에 있는 것만 적용 (안전)
		// ------------------------------------------------------------
		for (uint32 i = 0; i < tmpl.GetValueParamCount(); ++i)
		{
			const MaterialValueParamDesc& desc = tmpl.GetValueParam(i);
			if (desc.Name.empty())
				continue;

			auto it = cache.ValueBytes.find(desc.Name);
			if (it == cache.ValueBytes.end())
				continue;

			const std::vector<uint8>& bytes = it->second;
			if (bytes.empty())
				continue;

			(void)inst.SetValue(desc.Name.c_str(), bytes.data(), desc.Type);
		}

		// ------------------------------------------------------------
		// Resources (cache.TexturePaths -> inst) + g_MaterialFlags 재계산
		// ------------------------------------------------------------
		uint32 materialFlags = 0;

		// cache에 g_MaterialFlags 엔트리가 없을 수도 있으니 확보
		{
			auto& flagBytes = cache.ValueBytes["g_MaterialFlags"];
			if (flagBytes.size() != sizeof(uint32))
			{
				flagBytes.resize(sizeof(uint32));
				std::memset(flagBytes.data(), 0, flagBytes.size());
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

			std::string path;
			if (auto it = cache.TexturePaths.find(name); it != cache.TexturePaths.end())
				path = it->second;

			const uint32 bit = flagFromTextureName(name);

			if (!path.empty())
			{
				const std::string p = sanitizeFilePath(path);
				if (!p.empty())
				{
					AssetRef<TextureAsset> ref = registerTexturePath(p);
					inst.SetTextureAssetRef(name.c_str(), ref);

					if (bit != 0)
						materialFlags |= bit;
				}
				else
				{
					// sanitize 결과가 비면 제거 취급
					inst.ClearTextureAssetRef(name.c_str());
				}
			}
			else
			{
				inst.ClearTextureAssetRef(name.c_str());
			}
		}

		// g_MaterialFlags 적용 (cache + inst 둘 다)
		{
			auto& flagBytes = cache.ValueBytes["g_MaterialFlags"];
			std::memcpy(flagBytes.data(), &materialFlags, sizeof(uint32));
			inst.SetUint("g_MaterialFlags", materialFlags);
		}

		// 마지막에 dirty
		inst.MarkAllDirty();
	}



	void MaterialEditor::drawPipelineEditor(MaterialInstance& inst, MaterialUiCache& cache)
	{
		if (inst.GetPipelineType() == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			// Render pass
			imGuiInputString("RenderPass", cache.RenderPassName);

			int subpass = (int)cache.SubpassIndex;
			if (ImGui::InputInt("Subpass", &subpass))
				cache.SubpassIndex = (uint32)std::max(0, subpass);

			IRenderPass* rp = nullptr;
			if (m_pRenderer && !cache.RenderPassName.empty())
				rp = m_pRenderer->GetRenderPassOrNull(cache.RenderPassName);

			inst.SetRenderPass(rp, cache.SubpassIndex);

			// Cull
			{
				int cm = (int)cache.CullMode;
				const char* items[] = { "None", "Front", "Back" };
				int idx = 2;
				if (cache.CullMode == CULL_MODE_NONE) idx = 0;
				else if (cache.CullMode == CULL_MODE_FRONT) idx = 1;
				else if (cache.CullMode == CULL_MODE_BACK) idx = 2;

				if (ImGui::Combo("Cull", &idx, items, 3))
				{
					cache.CullMode = (idx == 0) ? CULL_MODE_NONE : (idx == 1) ? CULL_MODE_FRONT : CULL_MODE_BACK;
					inst.SetCullMode(cache.CullMode);
				}
				else
				{
					inst.SetCullMode(cache.CullMode);
				}
			}

			if (ImGui::Checkbox("FrontCCW", &cache.FrontCounterClockwise))
				inst.SetFrontCounterClockwise(cache.FrontCounterClockwise);
			else
				inst.SetFrontCounterClockwise(cache.FrontCounterClockwise);

			// Depth
			if (ImGui::Checkbox("DepthEnable", &cache.DepthEnable))
				inst.SetDepthEnable(cache.DepthEnable);
			else
				inst.SetDepthEnable(cache.DepthEnable);

			if (ImGui::Checkbox("DepthWrite", &cache.DepthWriteEnable))
				inst.SetDepthWriteEnable(cache.DepthWriteEnable);
			else
				inst.SetDepthWriteEnable(cache.DepthWriteEnable);

			{
				int df = (int)cache.DepthFunc;
				const char* labels[] = { "NEVER","LESS","EQUAL","LEQUAL","GREATER","NOT_EQUAL","GEQUAL","ALWAYS" };
				int sel = 3; // LEQUAL
				switch (cache.DepthFunc)
				{
				case COMPARISON_FUNC_NEVER: sel = 0; break;
				case COMPARISON_FUNC_LESS: sel = 1; break;
				case COMPARISON_FUNC_EQUAL: sel = 2; break;
				case COMPARISON_FUNC_LESS_EQUAL: sel = 3; break;
				case COMPARISON_FUNC_GREATER: sel = 4; break;
				case COMPARISON_FUNC_NOT_EQUAL: sel = 5; break;
				case COMPARISON_FUNC_GREATER_EQUAL: sel = 6; break;
				case COMPARISON_FUNC_ALWAYS: sel = 7; break;
				default: sel = 3; break;
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
					inst.SetDepthFunc(cache.DepthFunc);
				}
				else
				{
					inst.SetDepthFunc(cache.DepthFunc);
				}
			}

			// Texture binding mode
			{
				int mode = (cache.TextureBindingMode == MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC) ? 0 : 1;
				const char* items[] = { "DYNAMIC", "MUTABLE" };
				if (ImGui::Combo("TexBinding", &mode, items, 2))
				{
					cache.TextureBindingMode = (mode == 0) ? MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC : MATERIAL_TEXTURE_BINDING_MODE_MUTABLE;
					inst.SetTextureBindingMode(cache.TextureBindingMode);
				}
				else
				{
					inst.SetTextureBindingMode(cache.TextureBindingMode);
				}
			}

			// Sampler (optional)
			imGuiInputString("LinearWrapName", cache.LinearWrapSamplerName);
			inst.SetLinearWrapSamplerName(cache.LinearWrapSamplerName);
			inst.SetLinearWrapSamplerDesc(cache.LinearWrapSamplerDesc);
		}
	}

	void MaterialEditor::drawValueEditor(MaterialInstance& inst, MaterialUiCache& cache, const MaterialTemplate& tmpl)
	{
		ImGui::TextDisabled("Reflection-driven (Template -> Values).");
		ImGui::Separator();

		const uint32 count = tmpl.GetValueParamCount();
		if (count == 0)
		{
			ImGui::TextDisabled("No value params.");
			return;
		}

		// Optional: search/filter
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

			// Ensure cache has a byte buffer for this param
			auto& bytes = cache.ValueBytes[desc.Name];
			const uint32 expectedSize = (desc.ByteSize != 0) ? desc.ByteSize : (uint32)bytes.size();
			if (expectedSize != 0 && (uint32)bytes.size() != expectedSize)
			{
				bytes.resize(expectedSize);
				memset(bytes.data(), 0, bytes.size());
			}

			ImGui::PushID((int)i);

			bool changed = false;

			// UI per type
			switch (desc.Type)
			{
			case MATERIAL_VALUE_TYPE_FLOAT:
			{
				float v = 0.0f;
				if (bytes.size() >= sizeof(float)) memcpy(&v, bytes.data(), sizeof(float));
				if (ImGui::DragFloat(desc.Name.c_str(), &v, 0.01f))
				{
					changed = true;
					memcpy(bytes.data(), &v, sizeof(float));
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_FLOAT2:
			{
				float v[2] = {};
				if (bytes.size() >= sizeof(float) * 2) memcpy(v, bytes.data(), sizeof(float) * 2);
				if (ImGui::DragFloat2(desc.Name.c_str(), v, 0.01f))
				{
					changed = true;
					memcpy(bytes.data(), v, sizeof(float) * 2);
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_FLOAT3:
			{
				float v[3] = {};
				if (bytes.size() >= sizeof(float) * 3) memcpy(v, bytes.data(), sizeof(float) * 3);

				// color-like heuristic: name contains "Color" or "Albedo" etc.
				const bool isColor =
					(desc.Name.find("Color") != std::string::npos) ||
					(desc.Name.find("Albedo") != std::string::npos) ||
					(desc.Name.find("BaseColor") != std::string::npos);

				if (isColor)
				{
					if (ImGui::ColorEdit3(desc.Name.c_str(), v))
					{
						changed = true;
						memcpy(bytes.data(), v, sizeof(float) * 3);
					}
				}
				else
				{
					if (ImGui::DragFloat3(desc.Name.c_str(), v, 0.01f))
					{
						changed = true;
						memcpy(bytes.data(), v, sizeof(float) * 3);
					}
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_FLOAT4:
			{
				float v[4] = {};
				if (bytes.size() >= sizeof(float) * 4) memcpy(v, bytes.data(), sizeof(float) * 4);

				const bool isColor =
					(desc.Name.find("Color") != std::string::npos) ||
					(desc.Name.find("Albedo") != std::string::npos) ||
					(desc.Name.find("BaseColor") != std::string::npos);

				if (isColor)
				{
					if (ImGui::ColorEdit4(desc.Name.c_str(), v))
					{
						changed = true;
						memcpy(bytes.data(), v, sizeof(float) * 4);
					}
				}
				else
				{
					if (ImGui::DragFloat4(desc.Name.c_str(), v, 0.01f))
					{
						changed = true;
						memcpy(bytes.data(), v, sizeof(float) * 4);
					}
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_INT:
			{
				int32 v = 0;
				if (bytes.size() >= sizeof(int32)) memcpy(&v, bytes.data(), sizeof(int32));
				if (ImGui::DragInt(desc.Name.c_str(), &v, 1.0f))
				{
					changed = true;
					memcpy(bytes.data(), &v, sizeof(int32));
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_INT2:
			{
				int32 v[2] = {};
				if (bytes.size() >= sizeof(int32) * 2) memcpy(v, bytes.data(), sizeof(int32) * 2);
				if (ImGui::DragInt2(desc.Name.c_str(), v, 1.0f))
				{
					changed = true;
					memcpy(bytes.data(), v, sizeof(int32) * 2);
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_INT3:
			{
				int32 v[3] = {};
				if (bytes.size() >= sizeof(int32) * 3) memcpy(v, bytes.data(), sizeof(int32) * 3);
				if (ImGui::DragInt3(desc.Name.c_str(), v, 1.0f))
				{
					changed = true;
					memcpy(bytes.data(), v, sizeof(int32) * 3);
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_INT4:
			{
				int32 v[4] = {};
				if (bytes.size() >= sizeof(int32) * 4) memcpy(v, bytes.data(), sizeof(int32) * 4);
				if (ImGui::DragInt4(desc.Name.c_str(), v, 1.0f))
				{
					changed = true;
					memcpy(bytes.data(), v, sizeof(int32) * 4);
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_UINT:
			{
				uint32 v = 0;
				if (bytes.size() >= sizeof(uint32)) memcpy(&v, bytes.data(), sizeof(uint32));
				int tmp = (int)v;
				if (ImGui::DragInt(desc.Name.c_str(), &tmp, 1.0f, 0, INT32_MAX))
				{
					v = (uint32)std::max(0, tmp);
					changed = true;
					memcpy(bytes.data(), &v, sizeof(uint32));
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_UINT2:
			{
				uint32 v[2] = {};
				if (bytes.size() >= sizeof(uint32) * 2) memcpy(v, bytes.data(), sizeof(uint32) * 2);
				int tmp[2] = { (int)v[0], (int)v[1] };
				if (ImGui::DragInt2(desc.Name.c_str(), tmp, 1.0f, 0, INT32_MAX))
				{
					v[0] = (uint32)std::max(0, tmp[0]);
					v[1] = (uint32)std::max(0, tmp[1]);
					changed = true;
					memcpy(bytes.data(), v, sizeof(uint32) * 2);
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_UINT3:
			{
				uint32 v[3] = {};
				if (bytes.size() >= sizeof(uint32) * 3) memcpy(v, bytes.data(), sizeof(uint32) * 3);
				int tmp[3] = { (int)v[0], (int)v[1], (int)v[2] };
				if (ImGui::DragInt3(desc.Name.c_str(), tmp, 1.0f, 0, INT32_MAX))
				{
					v[0] = (uint32)std::max(0, tmp[0]);
					v[1] = (uint32)std::max(0, tmp[1]);
					v[2] = (uint32)std::max(0, tmp[2]);
					changed = true;
					memcpy(bytes.data(), v, sizeof(uint32) * 3);
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_UINT4:
			{
				uint32 v[4] = {};
				if (bytes.size() >= sizeof(uint32) * 4) memcpy(v, bytes.data(), sizeof(uint32) * 4);
				int tmp[4] = { (int)v[0], (int)v[1], (int)v[2], (int)v[3] };
				if (ImGui::DragInt4(desc.Name.c_str(), tmp, 1.0f, 0, INT32_MAX))
				{
					v[0] = (uint32)std::max(0, tmp[0]);
					v[1] = (uint32)std::max(0, tmp[1]);
					v[2] = (uint32)std::max(0, tmp[2]);
					v[3] = (uint32)std::max(0, tmp[3]);
					changed = true;
					memcpy(bytes.data(), v, sizeof(uint32) * 4);
				}
				break;
			}
			case MATERIAL_VALUE_TYPE_FLOAT4X4:
			{
				// matrix는 UI로 편집하기 복잡하니, 간단히 raw 표시 + Reset만 제공
				ImGui::Text("%s (float4x4)", desc.Name.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Reset Identity"))
				{
					changed = true;
					bytes.resize(sizeof(float) * 16);
					float m[16] =
					{
						1,0,0,0,
						0,1,0,0,
						0,0,1,0,
						0,0,0,1
					};
					memcpy(bytes.data(), m, sizeof(m));
				}
				break;
			}
			default:
			{
				// Unknown -> raw bytes length only
				ImGui::Text("%s (unknown, %u bytes)", desc.Name.c_str(), (uint32)bytes.size());
				break;
			}
			}

			// Apply immediately only when changed
			if (changed)
			{
				inst.SetValue(desc.Name.c_str(), bytes.data(), desc.Type);
				inst.MarkAllDirty();
			}

			ImGui::PopID();
		}
	}

	// ------------------------------------------------------------
	// ImGui std::string InputText helper (fixes duplicated/concatenated path issue)
	// ------------------------------------------------------------
	static int ImGuiInputTextCallback_Resize(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
		{
			auto* str = reinterpret_cast<std::string*>(data->UserData);
			str->resize((size_t)data->BufTextLen);
			data->Buf = str->data();
		}
		return 0;
	}

	static bool InputTextString(const char* label, std::string& str, size_t reserveCap = 512)
	{
		if (str.capacity() < reserveCap)
			str.reserve(reserveCap);

		// NOTE: ImGui expects mutable char buffer; std::string::data() is mutable in C++17+
		return ImGui::InputText(
			label,
			str.data(),
			str.capacity() + 1,
			ImGuiInputTextFlags_CallbackResize,
			ImGuiInputTextCallback_Resize,
			(void*)&str);
	}


	void MaterialEditor::drawResourceEditor(MaterialInstance& inst, MaterialUiCache& cache, const MaterialTemplate& tmpl)
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

			// cache entry (stable storage)
			std::string& path = cache.TexturePaths[desc.Name];

			// Header line
			ImGui::Text("%s", desc.Name.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)",
				(desc.Type == MATERIAL_RESOURCE_TYPE_TEXTURECUBE) ? "Cube" :
				(desc.Type == MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY) ? "2DArray" : "2D");

			// Path editor (NO local buf -> fixes duplicated input)
			bool edited = InputTextString("Path", path, 512);

			ImGui::SameLine();
			const bool applyPressed = ImGui::Button("Apply");

			ImGui::SameLine();
			const bool clearPressed = ImGui::Button("Clear");

			auto flagFromTextureName = [](const std::string& n) -> uint32
			{
				if (n == "g_BaseColorTex")        return MAT_HAS_BASECOLOR;
				if (n == "g_NormalTex")           return MAT_HAS_NORMAL;
				if (n == "g_MetallicRoughnessTex")return MAT_HAS_MR;
				if (n == "g_AOTex")               return MAT_HAS_AO;
				if (n == "g_EmissiveTex")         return MAT_HAS_EMISSIVE;
				if (n == "g_HeightTex")           return MAT_HAS_HEIGHT;
				return 0;
			};

			const uint32 flagBit = flagFromTextureName(desc.Name);

			if (clearPressed)
			{
				path.clear();
				if (flagBit != 0)
				{
					uint32* pMatFlag = reinterpret_cast<uint32*>(cache.ValueBytes["g_MaterialFlags"].data());
					*pMatFlag &= ~flagBit;
					inst.SetUint("g_MaterialFlags", *pMatFlag);
				}
				inst.ClearTextureAssetRef(desc.Name.c_str());
				inst.MarkAllDirty();
			}

			// Apply condition:
			if (applyPressed && !clearPressed)
			{
				const std::string p = sanitizeFilePath(path);
				if (!p.empty())
				{
					AssetRef<TextureAsset> ref = registerTexturePath(p);
					inst.SetTextureAssetRef(desc.Name.c_str(), ref);
					if (flagBit != 0)
					{
						uint32* pMatFlag = reinterpret_cast<uint32*>(cache.ValueBytes["g_MaterialFlags"].data());
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
	// Scene load (kept similar to your previous flow)
	// ------------------------------------------------------------

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
			const float s = computeUniformScaleToFitUnitCube(cpuMesh->GetBounds(), 1.0f);
			entry.Scale = float3(s, s, s);
		}
		else
		{
			entry.Scale = scale;
		}

		entry.Position = position;
		entry.BaseRotation = rotation;

		// Create RD
		entry.MeshHandle = m_pRenderer->CreateStaticMesh(*cpuMesh);

		// Create materials per slot using current template
		std::vector<MaterialInstance> mats = buildMaterialsForCpuMeshSlots(*cpuMesh);

		entry.ObjectId = m_pRenderScene->AddObject(
			entry.MeshHandle,
			static_cast<std::vector<MaterialInstance>&&>(mats),
			Matrix4x4::TRS(entry.Position, entry.BaseRotation, entry.Scale));
		entry.SceneObjectIndex = (int32)m_pRenderScene->GetObjects().size() - 1;

		m_Loaded.push_back(static_cast<LoadedMesh&&>(entry));
	}

	std::vector<MaterialInstance> MaterialEditor::buildMaterialsForCpuMeshSlots(const StaticMeshAsset& cpuMesh)
	{
		MaterialTemplate* pTemplate = getOrCreateTemplateFromInputs();
		ASSERT(pTemplate, "Template is null.");

		std::vector<MaterialInstance> materials;
		materials.reserve(cpuMesh.GetMaterialSlots().size());

		for (uint32 i = 0; i < static_cast<uint32>(cpuMesh.GetMaterialSlots().size()); ++i)
		{
			MaterialInstance mat = {};
			{
				const bool ok = mat.Initialize(pTemplate, "MaterialEditor Instance");
				ASSERT(ok, "MaterialInstance::Initialize failed.");
			}

			// Default pipeline knobs (reasonable defaults)
			IRenderPass* renderPass = m_pRenderer->GetRenderPassOrNull("GBuffer");
			mat.SetRenderPass(renderPass, 0);
			mat.SetCullMode(CULL_MODE_BACK);
			mat.SetFrontCounterClockwise(true);
			mat.SetDepthEnable(true);
			mat.SetDepthWriteEnable(true);
			mat.SetDepthFunc(COMPARISON_FUNC_LESS_EQUAL);
			mat.SetTextureBindingMode(MATERIAL_TEXTURE_BINDING_MODE_DYNAMIC);

			const MaterialAsset& slot = cpuMesh.GetMaterialSlot(i);
			for (uint resIndex = 0; resIndex < pTemplate->GetResourceCount(); ++resIndex)
			{
				const MaterialResourceDesc& resDesc = pTemplate->GetResource(resIndex);
				const std::string& varName = resDesc.Name;

				for (uint32 bindingIndex = 0; bindingIndex < slot.GetResourceBindingCount(); ++bindingIndex)
				{
					const MaterialAsset::ResourceBinding& resBinding = slot.GetResourceBinding(bindingIndex);
					const std::string bindingName = resBinding.Name;

					if (bindingName == varName)
					{
						mat.SetTextureAssetRef(varName.c_str(), resBinding.TextureRef);
						break;
					}
				}
			}

			for (uint valIndex = 0; valIndex < pTemplate->GetValueParamCount(); ++valIndex)
			{
				const MaterialValueParamDesc& valDesc = pTemplate->GetValueParam(valIndex);
				const std::string& varName = valDesc.Name;

				for (uint32 bindingIndex = 0; bindingIndex < slot.GetValueOverrideCount(); ++bindingIndex)
				{
					const MaterialAsset::ValueOverride& valOverride = slot.GetValueOverride(bindingIndex);
					const std::string bindingName = valOverride.Name;

					if (bindingName == varName)
					{
						mat.SetValue(varName.c_str(), valOverride.Data.data(), valOverride.Type);
						break;
					}
				}
			}


			mat.MarkAllDirty();

			materials.push_back(std::move(mat));
		}

		return materials;
	}

	// ------------------------------------------------------------
	// Utils
	// ------------------------------------------------------------

	std::string MaterialEditor::sanitizeFilePath(std::string s)
	{
		if (s.empty())
			return s;

		for (char& c : s)
		{
			if (c == '\\')
				c = '/';
		}

		// Trim quotes
		if (!s.empty() && (s.front() == '\"' || s.front() == '\''))
			s.erase(s.begin());
		if (!s.empty() && (s.back() == '\"' || s.back() == '\''))
			s.pop_back();

		return s;
	}
} // namespace shz
