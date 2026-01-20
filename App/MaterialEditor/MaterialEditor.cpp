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

		loadPreviewMesh(
			"C:/Dev/ShizenEngine/Assets/Basic/DamagedHelmet/glTF/DamagedHelmet.gltf",
			{ 0.0f, 1.0f, 3.0f }, { 0, 0, 0 }, { 1, 1, 1 }, true);

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
		ITextureView* pColor = m_pRenderer->GetLightingSRV();
		ImTextureID tid = reinterpret_cast<ImTextureID>(pColor);
		ImGui::Image(tid, avail);

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
			(void)getOrCreateTemplateFromInputs();

			const uint64 key = makeSelectionKey(m_SelectedObject, m_SelectedMaterialSlot);
			m_MaterialUi[key].Dirty = true;
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
			syncCacheFromTemplateDefaults(cache, *pTmpl);
			applyCacheToInstance(*pInst, cache);
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
	//
	// IMPORTANT:
	// 아래에서 MaterialTemplate 리플렉션 API 이름은 “추정”이야.
	// 네 엔진 실제 API에 맞춰서 함수/필드명만 바꾸면 된다.
	//

	void MaterialEditor::syncCacheFromTemplateDefaults(MaterialUiCache& cache, const MaterialTemplate& tmpl)
	{
		cache.ValueBytes.clear();
		cache.TexturePaths.clear();

		// -------------------------
		// Values
		// -------------------------
		// TODO: Replace with your real reflection getters
		// for (const auto& p : tmpl.GetValueParams()) { ... }

		// 예시 구조:
		// p.Name (std::string)
		// p.Type (MATERIAL_VALUE_TYPE)
		// p.DefaultData (std::vector<uint8> or pointer+size)
		//
		// 여기서는 “기본값 바이트”를 그대로 복사해 cache.ValueBytes[name]에 담는 방식.

		// -------------------------
		// Resources (textures)
		// -------------------------
		// TODO: Replace with your real reflection getters
		// for (const auto& r : tmpl.GetResourceParams()) { if texture -> cache.TexturePaths[r.Name] = "" }
	}

	void MaterialEditor::applyCacheToInstance(MaterialInstance& inst, MaterialUiCache& cache)
	{
		// Pipeline knobs applied
		drawPipelineEditor(inst, cache);

		// Values applied from cache
		for (auto& [name, bytes] : cache.ValueBytes)
		{
			// Minimal support: float/float3/float4/uint (common)
			if (bytes.size() == sizeof(float))
			{
				float v = 0.0f;
				memcpy(&v, bytes.data(), sizeof(float));
				inst.SetFloat(name.c_str(), v);
			}
			else if (bytes.size() == sizeof(float) * 3)
			{
				float v[3] = {};
				memcpy(v, bytes.data(), sizeof(float) * 3);
				inst.SetFloat3(name.c_str(), v);
			}
			else if (bytes.size() == sizeof(float) * 4)
			{
				float v[4] = {};
				memcpy(v, bytes.data(), sizeof(float) * 4);
				inst.SetFloat4(name.c_str(), v);
			}
			else if (bytes.size() == sizeof(uint32))
			{
				uint32 v = 0;
				memcpy(&v, bytes.data(), sizeof(uint32));
				inst.SetUint(name.c_str(), v);
			}
		}

		// Resources applied from cache
		for (auto& [name, path] : cache.TexturePaths)
		{
			const std::string p = sanitizeFilePath(path);
			if (!p.empty())
			{
				AssetRef<TextureAsset> ref = registerTexturePath(p);
				inst.SetTextureAssetRef(name.c_str(), ref);
			}
		}

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

		// TODO: Replace with your real reflection getters
		// for (const auto& p : tmpl.GetValueParams()) { ... }

		ImGui::TextDisabled("TODO: hook tmpl.GetValueParams()");
	}

	void MaterialEditor::drawResourceEditor(MaterialInstance& inst, MaterialUiCache& cache, const MaterialTemplate& tmpl)
	{
		ImGui::TextDisabled("Reflection-driven (Template -> Resources).");
		ImGui::Separator();

		// TODO: Replace with your real reflection getters
		// for (const auto& r : tmpl.GetResourceParams()) { if texture -> edit path + SetTextureAssetRef }

		ImGui::TextDisabled("TODO: hook tmpl.GetResourceParams()");
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
