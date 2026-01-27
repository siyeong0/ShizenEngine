#include "GrassViewer.h"

#include <algorithm>
#include <utility>
#include <random>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/RuntimeData/Public/StaticMeshImporter.h"
#include "Engine/RuntimeData/Public/TextureImporter.h"
#include "Engine/RuntimeData/Public/MaterialImporter.h"
#include "Engine/RuntimeData/Public/TerrainHeightFieldImporter.h"

#include "Engine/RuntimeData/Public/TerrainMeshBuilder.h"

#include "Engine/Image/Public/Image.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	namespace
	{
		static constexpr const char* kShaderRoot = "C:/Dev/ShizenEngine/Shaders";

		static void setupDefaultViewFamily(ViewFamily& vf)
		{
			vf.Views.clear();
			vf.Views.push_back({});
		}

		static void setupCameraDefault(FirstPersonCamera& cam, float aspect)
		{
			cam.SetPos(float3(0.0f, 20.0f, 0.8f));
			cam.SetRotation(0.0f, 0.0f);
			cam.SetMoveSpeed(3.0f);
			cam.SetSpeedUpScales(5.0f, 1.0f);
			cam.SetRotationSpeed(0.01f);

			cam.SetProjAttribs(
				0.1f,
				5000.0f,
				aspect,
				PI / 4.0f,
				SURFACE_TRANSFORM_IDENTITY);
		}

		static void setupDefaultGlobalLight(RenderScene::LightObject& light)
		{
			light.Direction = float3(0.4f, -1.0f, 0.3f);
			light.Color = float3(1.0f, 1.0f, 1.0f);
			light.Intensity = 2.0f;
		}

		static void updatePrimaryView(
			ViewFamily& vf,
			const GrassViewer::ViewportState& vp,
			const FirstPersonCamera& cam)
		{
			ASSERT(!vf.Views.empty(), "No view.");

			auto& v = vf.Views[0];

			v.Viewport.left = 0;
			v.Viewport.top = 0;
			v.Viewport.right = vp.Width;
			v.Viewport.bottom = vp.Height;

			v.CameraPosition = cam.GetPos();
			v.ViewMatrix = cam.GetViewMatrix();
			v.ProjMatrix = cam.GetProjMatrix();

			v.NearPlane = cam.GetProjAttribs().NearClipPlane;
			v.FarPlane = cam.GetProjAttribs().FarClipPlane;
		}
	} // namespace

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

		// Asset
		m_pAssetManager = std::make_unique<AssetManager>();
		{
			ASSERT(m_pAssetManager, "AssetManager is null.");
			m_pAssetManager->Initialize();
			m_pAssetManager->RegisterImporter(AssetTypeTraits<StaticMesh>::TypeID, StaticMeshImporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<Texture>::TypeID, TextureImporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<Material>::TypeID, MaterialImporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<TerrainHeightField>::TypeID, TerrainHeightFieldImporter{});
		}

		// Renderer + shader factory
		m_pRenderer = std::make_unique<Renderer>();
		ASSERT(m_pRenderer, "Renderer is null.");

		ASSERT(m_pEngineFactory, "EngineFactory is null.");
		m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(kShaderRoot, &m_pShaderSourceFactory);
		ASSERT(m_pShaderSourceFactory, "ShaderSourceFactory is null.");

		ASSERT(m_pSwapChain, "SwapChain is null.");

		const auto scDesc = m_pSwapChain->GetDesc();
		m_Viewport.Width = std::max(1u, scDesc.Width);
		m_Viewport.Height = std::max(1u, scDesc.Height);

		RendererCreateInfo rendererCI = {};
		rendererCI.pEngineFactory = m_pEngineFactory;
		rendererCI.pShaderSourceFactory = m_pShaderSourceFactory;
		rendererCI.pDevice = m_pDevice;
		rendererCI.pImmediateContext = m_pImmediateContext;
		rendererCI.pDeferredContexts = m_pDeferredContexts;
		rendererCI.pSwapChain = m_pSwapChain;
		rendererCI.pImGui = m_pImGui;
		rendererCI.BackBufferWidth = m_Viewport.Width;
		rendererCI.BackBufferHeight = m_Viewport.Height;
		rendererCI.pAssetManager = m_pAssetManager.get();

		m_pRenderer->Initialize(rendererCI);

		// Scene
		m_pRenderScene = std::make_unique<RenderScene>();
		ASSERT(m_pRenderScene, "RenderScene is null.");

		// ViewFamily + Camera
		setupDefaultViewFamily(m_ViewFamily);
		setupCameraDefault(m_Camera, (float)m_Viewport.Width / (float)m_Viewport.Height);

		// Light
		setupDefaultGlobalLight(m_GlobalLight);
		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);
		ASSERT(m_GlobalLightHandle.IsValid(), "Failed to add global light.");

		// Load terrain
		{
			const std::string heightPath = "C:/Dev/ShizenEngine/Assets/Terrain/RollingHills/RollingHillsHeightMap.png";
			const std::string diffusePath = "C:/Dev/ShizenEngine/Assets/Terrain/RollingHills/RollingHillsBitmap.png";

			//const std::string heightPath = "C:/Dev/ShizenEngine/Assets/Terrain/Mountain/HeightEXR.exr";
			//const std::string diffusePath = "C:/Dev/ShizenEngine/Assets/Terrain/Mountain/Diffuse.png"; // TODO: exr rgba not works.

			float scale = 0.5f;

			AssetRef<TerrainHeightField> terrainRef = m_pAssetManager->RegisterAsset<TerrainHeightField>(heightPath);
			AssetPtr<TerrainHeightField> terrainPtr = m_pAssetManager->LoadBlocking<TerrainHeightField>(terrainRef);
			ASSERT(terrainPtr && terrainPtr->IsValid(), "Failed to load terrain height field.");


			StaticMesh terrainMesh;
			Material tm("TerrainMaterial", "DefaultLit");
			tm.SetFloat4("g_BaseColorFactor", float4(150.f, 200.f, 100.f, 1.f) / 255.f);
			//tm.SetFloat4("g_BaseColorFactor", float4(180.f, 75.f, 24.f, 1.f) / 255.f);
			tm.SetFloat3("g_EmissiveFactor", float3(0.f, 0.f, 0.f));
			tm.SetFloat("g_EmissiveIntensity", 0.0f);
			tm.SetFloat("g_RoughnessFactor", 0.85f);
			tm.SetFloat("g_NormalScale", 1.0f);
			tm.SetFloat("g_OcclusionStrength", 1.0f);
			tm.SetFloat("g_AlphaCutoff", 0.5f);
			tm.SetFloat("g_MetallicFactor", 0.0f);
			tm.SetUint("g_MaterialFlags", 0); // hlsl::MAT_HAS_BASECOLOR);
			/*tm.SetTextureAssetRef("g_BaseColorTex", MATERIAL_RESOURCE_TYPE_TEXTURE2D,
				m_pAssetManager->RegisterAsset<Texture>(diffusePath));*/

			TerrainMeshBuilder meshBuilder;
			TerrainMeshBuildSettings buildSettings = {};
			meshBuilder.BuildStaticMesh(&terrainMesh, *terrainPtr, std::move(tm), buildSettings);

			m_pRenderScene->SetTerrain(
				m_pRenderer->CreateTextureFromHeightField(*terrainPtr),
				m_pRenderer->CreateStaticMesh(terrainMesh));

			// Trees
			AssetRef<StaticMesh> tree1Asset = m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree1.shzmesh.json");
			AssetRef<StaticMesh> tree2Asset = m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree2.shzmesh.json");
			AssetRef<StaticMesh> tree3Asset = m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree3.shzmesh.json");
			AssetRef<StaticMesh> tree4Asset = m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree4.shzmesh.json");
			AssetRef<StaticMesh> tree5Asset = m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree5.shzmesh.json");

			StaticMeshRenderData treeMeshes[] =
			{
				m_pRenderer->CreateStaticMesh(tree1Asset),
				m_pRenderer->CreateStaticMesh(tree2Asset),
				m_pRenderer->CreateStaticMesh(tree3Asset),
				m_pRenderer->CreateStaticMesh(tree4Asset),
				m_pRenderer->CreateStaticMesh(tree5Asset),
			};

			constexpr uint TREE_MESH_COUNT = sizeof(treeMeshes) / sizeof(treeMeshes[0]);

			constexpr float4 SPAWN_RANGE = { -500.0f, -500.0f, 500.0f, 500.0f };
			constexpr uint NUM_TREES = 10000;

			std::mt19937 rng(1337);
			std::uniform_real_distribution<float> distX(SPAWN_RANGE.x, SPAWN_RANGE.z);
			std::uniform_real_distribution<float> distZ(SPAWN_RANGE.y, SPAWN_RANGE.w);
			std::uniform_real_distribution<float> distYaw(0.0f, TWO_PI);
			std::uniform_real_distribution<float> distScale(0.85f, 1.15f);
			std::uniform_int_distribution<uint> distMesh(0, TREE_MESH_COUNT - 1);

			for (uint i = 0; i < NUM_TREES; ++i)
			{
				const float x = distX(rng);
				const float z = distZ(rng);
				const float y = terrainPtr->SampleWorldHeight(x, z);

				const float yaw = distYaw(rng);
				const float scale = distScale(rng);

				RenderScene::RenderObject treeObject;
				treeObject.Mesh = treeMeshes[distMesh(rng)];
				treeObject.Transform = Matrix4x4::TRS(
					{ x, y, z },
					{ 0.0f, yaw, 0.0f },
					{ scale, scale, scale });

				treeObject.bCastShadow = true;

				m_pRenderScene->AddObject(std::move(treeObject));
			}

			// Helmet
			AssetRef<StaticMesh> helmet = m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/DamagedHelmet.shzmesh.json");
			RenderScene::RenderObject helmetObj;
			helmetObj.Mesh = m_pRenderer->CreateStaticMesh(helmet);
			helmetObj.Transform = Matrix4x4::TRS({ 0.0f, 5.0f, 5.0f }, { 0.0f, 0.15f, 0.0f }, { 1.0f, 1.0f, 1.0f });
			helmetObj.bCastShadow = true;
			m_pRenderScene->AddObject(std::move(helmetObj));
		}

		// Fill first view immediately
		updatePrimaryView(m_ViewFamily, m_Viewport, m_Camera);
	}

	void GrassViewer::Render()
	{
		ASSERT(m_pRenderer, "Renderer is null.");
		ASSERT(m_pRenderScene, "RenderScene is null.");

		m_ViewFamily.FrameIndex++;

		m_pRenderer->BeginFrame();
		m_pRenderer->Render(*m_pRenderScene, m_ViewFamily);
		m_pRenderer->EndFrame();
	}

	void GrassViewer::Update(double currTime, double elapsedTime, bool doUpdateUI)
	{
		SampleBase::Update(currTime, elapsedTime, doUpdateUI);

		ASSERT(m_pRenderScene, "RenderScene is null.");

		const float dt = (float)elapsedTime;
		const float t = (float)currTime;

		m_Camera.Update(m_InputController, dt);

		m_ViewFamily.DeltaTime = dt;
		m_ViewFamily.CurrentTime = t;

		updatePrimaryView(m_ViewFamily, m_Viewport, m_Camera);

		ASSERT(m_GlobalLightHandle.IsValid(), "GlobalLightHandle is invalid.");
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

		updatePrimaryView(m_ViewFamily, m_Viewport, m_Camera);
	}

	void GrassViewer::UpdateUI()
	{
		// ------------------------------------------------------------
		// Settings window
		// ------------------------------------------------------------
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::gizmo3D("##LightDirection", m_GlobalLight.Direction, ImGui::GetTextLineHeight() * 10);
			ImGui::ColorEdit3("##LightColor", reinterpret_cast<float*>(&m_GlobalLight.Color));
			ImGui::SliderFloat("Intensity", &m_GlobalLight.Intensity, 0.01f, 20.0f);

			ImGui::Separator();
			ImGui::TextDisabled("FPS: %.1f", ImGui::GetIO().Framerate);

			ImGui::Separator();

			float prevSpeed = m_Speed;
			if (ImGui::DragFloat("Speed", &m_Speed, 0.05f, 0.01f, 100.0f, "%.3f"))
			{
				if (m_Speed < 0.01f) m_Speed = 0.01f;

				if (m_Speed != prevSpeed)
				{
					m_Camera.SetSpeedUpScales(m_Speed, 1.0f);
				}
			}
		}
		ImGui::End();

		// ------------------------------------------------------------
		// Profiling window (Draw calls per pass)
		// ------------------------------------------------------------
		ImGui::SetNextWindowPos(ImVec2(10, 220), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Profiling", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ASSERT(m_pRenderer, "Renderer is null.");

			const auto passTable = m_pRenderer->GetPassDrawCallCountTable();

			uint64 total = 0;
			for (const auto& kv : passTable)
				total += kv.second;

			ImGui::Text("Total Draw Calls: %llu", (unsigned long long)total);
			ImGui::Separator();

			for (const auto& kv : passTable)
				ImGui::Text("%s: %llu", kv.first.c_str(), (unsigned long long)kv.second);
		}
		ImGui::End();
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
		ASSERT(path && path[0] != '\0', "Invalid mesh path.");
		ASSERT(m_pAssetManager, "AssetManager is null.");
		ASSERT(m_pRenderScene, "RenderScene is null.");
		ASSERT(m_pRenderer, "Renderer is null.");

		// If reloading into same slot, remove previous object.
		if (inout.ObjectId.IsValid())
		{
			m_pRenderScene->RemoveObject(inout.ObjectId);
			inout = {};
		}

		inout.Path = path;
		inout.bCastShadow = bCastShadow;
		inout.bAlphaMasked = bAlphaMasked;

		inout.MeshRef = m_pAssetManager->RegisterAsset<StaticMesh>(inout.Path);
		inout.Mesh = m_pRenderer->CreateStaticMesh(inout.MeshRef);

		RenderScene::RenderObject obj = {};
		obj.Mesh = inout.Mesh;
		obj.Transform = Matrix4x4::TRS(position, rotation, scale);
		obj.bCastShadow = bCastShadow;

		inout.ObjectId = m_pRenderScene->AddObject(std::move(obj));
		ASSERT(inout.ObjectId.IsValid(), "Failed to add RenderObject.");

		return true;
	}
} // namespace shz
