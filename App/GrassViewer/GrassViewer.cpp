#include "GrassViewer.h"

#include <algorithm>
#include <random>
#include <cmath>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/RuntimeData/Public/StaticMeshImporter.h"
#include "Engine/RuntimeData/Public/TextureImporter.h"
#include "Engine/RuntimeData/Public/MaterialImporter.h"
#include "Engine/RuntimeData/Public/TerrainHeightFieldImporter.h"

#include "Engine/RuntimeData/Public/TerrainMeshBuilder.h"

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
			cam.SetPos(float3(-2.9f, 5.0f, 0.0f));
			cam.SetRotation(-0.8f, 0.0f);
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
	// Helpers
	// ------------------------------------------------------------

	Matrix4x4 GrassViewer::ToMatrixTRS(const CTransform& t)
	{
		return Matrix4x4::TRS(
			t.Position,
			t.Rotation,
			t.Scale);
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
		{
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
		}

		// Render Scene
		{
			m_pRenderScene = std::make_unique<RenderScene>();
			ASSERT(m_pRenderScene, "RenderScene is null.");
		}

		// ECS
		m_pEcs = std::make_unique<EcsWorld>();
		{
			ASSERT(m_pEcs, "ECS world is null.");
			shz::EcsWorld::CreateInfo eci = {};
			eci.FixedDeltaTime = 1.0f / 60.0f;
			eci.MaxFixedStepsPerFrame = 8;

			m_pEcs->Initialize(eci);
			ASSERT(m_pEcs->IsValid(), "EcsWorld is not initialized.");

			auto& ecs = m_pEcs->World();

			// Register components
			ecs.component<CName>();
			ecs.component<CTransform>();
			ecs.component<CMeshRenderer>();

			// New physics components
			ecs.component<CRigidbody>();
			ecs.component<CBoxCollider>();
			ecs.component<CSphereCollider>();
			ecs.component<CHeightFieldCollider>();
		}

		// PhysicsSystem (encapsulated Jolt)
		m_pPhysicsSystem = std::make_unique<PhysicsSystem>();
		{
			ASSERT(m_pPhysicsSystem, "PhysicsSystem is null.");

			PhysicsSystem::CreateInfo pci = {};
			pci.PhysicsCI.MaxBodies = 65536;
			pci.PhysicsCI.MaxBodyPairs = 65536;
			pci.PhysicsCI.MaxContactConstraints = 10240;
			pci.PhysicsCI.TempAllocatorSizeBytes = 16u * 1024u * 1024u;
			pci.PhysicsCI.Gravity = float3(0.0f, -9.81f, 0.0f);

			m_pPhysicsSystem->Initialize(pci);
		}

		// ECS ctx (Renderer/Scene/Asset/PhysicsSystem)
		{
			m_EcsCtx.pRenderer = m_pRenderer.get();
			m_EcsCtx.pRenderScene = m_pRenderScene.get();
			m_EcsCtx.pAssetManager = m_pAssetManager.get();
			m_EcsCtx.pPhysicsSystem = m_pPhysicsSystem.get();

			auto& ecs = m_pEcs->World();
			ecs.set_ctx(&m_EcsCtx);
		}

		// ECS: systems
		{
			auto& ecs = m_pEcs->World();

			// Install Physics <-> ECS systems (CreateBodies / PushTransform / WriteBack / OnRemove)
			auto physHandles = m_pPhysicsSystem->InstallEcsSystemsAndGetHandles(ecs);

			// Fixed: Physics step
			{
				auto sys = ecs.system<>("Physics.Step")
					.each([this]()
						{
							const float dtFixed = m_pEcs->GetFixedDeltaTime();
							m_pPhysicsSystem->Step(dtFixed);
						});
				m_pEcs->RegisterFixedSystem(sys);
			}

			// Register physics systems:
			if (physHandles.CreateBodies_Box.is_valid())        m_pEcs->RegisterUpdateSystem(physHandles.CreateBodies_Box);
			if (physHandles.CreateBodies_Sphere.is_valid())     m_pEcs->RegisterUpdateSystem(physHandles.CreateBodies_Sphere);
			if (physHandles.CreateBodies_HeightField.is_valid())m_pEcs->RegisterUpdateSystem(physHandles.CreateBodies_HeightField);

			if (physHandles.PushTransform.is_valid())
				m_pEcs->RegisterUpdateSystem(physHandles.PushTransform);

			if (physHandles.WriteBack.is_valid())
				m_pEcs->RegisterFixedSystem(physHandles.WriteBack);

			// Update: Transform -> RenderScene sync
			{
				auto sys = ecs.system<CTransform, CMeshRenderer>("Render.SyncTransforms")
					.each([this](CTransform& tr, CMeshRenderer& mr)
						{
							if (!mr.RenderObjectHandle.IsValid())
								return;

							m_EcsCtx.pRenderScene->UpdateObjectTransform(
								mr.RenderObjectHandle,
								GrassViewer::ToMatrixTRS(tr));
						});
				m_pEcs->RegisterUpdateSystem(sys);
			}
		}

		// ViewFamily + Camera
		setupDefaultViewFamily(m_ViewFamily);
		setupCameraDefault(m_Camera, (float)m_Viewport.Width / (float)m_Viewport.Height);

		// Light
		setupDefaultGlobalLight(m_GlobalLight);
		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);
		ASSERT(m_GlobalLightHandle.IsValid(), "Failed to add global light.");

		// Build scene once (ECS-driven objects)
		BuildSceneOnce();

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

		ASSERT(m_pEcs->IsValid(), "ECS world is not valid.");
		if (m_pEcs->IsValid())
		{
			m_pEcs->Tick(dt);
		}
	}

	void GrassViewer::ReleaseSwapChainBuffers()
	{
		SampleBase::ReleaseSwapChainBuffers();
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

		m_pRenderer->OnResize(m_Viewport.Width, m_Viewport.Height);

		updatePrimaryView(m_ViewFamily, m_Viewport, m_Camera);
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
	// Scene build (same behavior, but physics now via ECS components)
	// ------------------------------------------------------------

	void GrassViewer::BuildSceneOnce()
	{
		ASSERT(m_pAssetManager && m_pRenderer && m_pRenderScene && m_pEcs && m_pPhysicsSystem, "Subsystem missing.");

		auto& ecs = m_pEcs->World();

		// Load terrain (RenderScene는 기존대로 유지)
		const std::string heightPath = "C:/Dev/ShizenEngine/Assets/Terrain/RollingHills/RollingHillsHeightMap.png";

		AssetRef<TerrainHeightField> terrainRef = m_pAssetManager->RegisterAsset<TerrainHeightField>(heightPath);
		AssetPtr<TerrainHeightField> terrainPtr = m_pAssetManager->LoadBlocking<TerrainHeightField>(terrainRef);
		ASSERT(terrainPtr && terrainPtr->IsValid(), "Failed to load terrain height field.");

		// Build terrain mesh + set RenderScene terrain
		{
			StaticMesh terrainMesh;

			Material tm("TerrainMaterial", "DefaultLit");
			tm.SetFloat4("g_BaseColorFactor", float4(150.f, 200.f, 100.f, 255.f) / 255.f);
			tm.SetFloat3("g_EmissiveFactor", float3(0.f, 0.f, 0.f));
			tm.SetFloat("g_EmissiveIntensity", 0.0f);
			tm.SetFloat("g_RoughnessFactor", 0.85f);
			tm.SetFloat("g_NormalScale", 1.0f);
			tm.SetFloat("g_OcclusionStrength", 1.0f);
			tm.SetFloat("g_AlphaCutoff", 0.5f);
			tm.SetFloat("g_MetallicFactor", 0.0f);
			tm.SetUint("g_MaterialFlags", 0);

			TerrainMeshBuilder meshBuilder;
			TerrainMeshBuildSettings buildSettings = {};
			meshBuilder.BuildStaticMesh(&terrainMesh, *terrainPtr, std::move(tm), buildSettings);

			m_pRenderScene->SetTerrain(
				m_pRenderer->CreateTextureFromHeightField(*terrainPtr),
				m_pRenderer->CreateStaticMesh(terrainMesh));
		}

		// ------------------------------------------------------------
		// Physics Terrain: HeightFieldCollider + Static Rigidbody
		// ------------------------------------------------------------
		{
			const uint32 W = terrainPtr->GetWidth();
			const uint32 H = terrainPtr->GetHeight();

			// 기존 코드에서도 "Jolt heightfield는 square 기대"라고 ASSERT 했었음.
			// 새 Physics 래퍼는 square 강제는 안 하지만, 데이터가 square일 때 가장 안전.
			ASSERT(W == H, "HeightField collider: width/height must be equal (square) for your current pipeline.");

			// Convert to float heights (world meters)
			const auto& src = terrainPtr->GetDataU16(); // 네 코드에 있었던 API 가정
			ASSERT(src.size() == size_t(W) * size_t(H), "HeightField data size mismatch.");

			std::vector<float> samples;
			samples.resize(src.size());

			const float heightScale = terrainPtr->GetHeightScale();
			const float heightOffset = terrainPtr->GetHeightOffset();

			for (size_t i = 0; i < src.size(); ++i)
			{
				const float n = float(src[i]) / 65535.0f;
				samples[i] = n * heightScale + heightOffset;
			}

			const float spacingX = terrainPtr->GetWorldSpacingX();
			const float spacingZ = terrainPtr->GetWorldSpacingZ();

			// 기존 코드가 worldOrigin을 shape offset으로 넣었는데,
			// 새 래퍼에서는 shape offset을 따로 안 받으므로 Transform 위치로 맞춰줌.
			const float worldOriginX = -terrainPtr->GetWorldSizeX() * 0.5f;
			const float worldOriginZ = -terrainPtr->GetWorldSizeZ() * 0.5f;

			flecs::entity e = ecs.entity();
			e.set<CName>({ "TerrainPhysics" });

			CTransform tr = {};
			tr.Position = { worldOriginX, 0.0f, worldOriginZ };
			tr.Rotation = { 0.0f, 0.0f, 0.0f };
			tr.Scale = { 1.0f, 1.0f, 1.0f };
			e.set<CTransform>(tr);

			CRigidbody rb = {};
			rb.Motion = ERigidBodyMotion::Static;
			rb.Layer = 0; // NonMoving
			rb.bEnableGravity = false;
			rb.bStartActive = false;
			e.set<CRigidbody>(rb);

			CHeightFieldCollider hf = {};
			hf.Width = W;
			hf.Height = H;
			hf.CellSizeX = spacingX;
			hf.CellSizeZ = spacingZ;
			hf.HeightScale = 1.0f;   // samples already in world meters
			hf.HeightOffset = 0.0f;
			hf.Heights = std::move(samples);
			e.set<CHeightFieldCollider>(hf);
		}

		// ------------------------------------------------------------
		// Trees: render-only ECS entities (same as before)
		// ------------------------------------------------------------
		{
			AssetRef<StaticMesh> treeAssets[] =
			{
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree1.shzmesh.json"),
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree2.shzmesh.json"),
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree3.shzmesh.json"),
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree4.shzmesh.json"),
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree5.shzmesh.json"),
			};

			StaticMeshRenderData treeMeshes[] =
			{
				m_pRenderer->CreateStaticMesh(treeAssets[0]),
				m_pRenderer->CreateStaticMesh(treeAssets[1]),
				m_pRenderer->CreateStaticMesh(treeAssets[2]),
				m_pRenderer->CreateStaticMesh(treeAssets[3]),
				m_pRenderer->CreateStaticMesh(treeAssets[4]),
			};

			constexpr uint TREE_MESH_COUNT = sizeof(treeMeshes) / sizeof(treeMeshes[0]);

			constexpr float4 SPAWN_RANGE = { -500.0f, -500.0f, 500.0f, 500.0f };
			constexpr uint  NUM_TREES = 10000;

			std::mt19937 rng(1337);
			std::uniform_real_distribution<float> distX(SPAWN_RANGE.x, SPAWN_RANGE.z);
			std::uniform_real_distribution<float> distZ(SPAWN_RANGE.y, SPAWN_RANGE.w);
			std::uniform_real_distribution<float> distYaw(0.0f, TWO_PI);
			std::uniform_real_distribution<float> distScale(0.85f, 1.15f);
			std::uniform_int_distribution<uint>  distMesh(0, TREE_MESH_COUNT - 1);

			for (uint i = 0; i < NUM_TREES; ++i)
			{
				const float x = distX(rng);
				const float z = distZ(rng);
				const float y = terrainPtr->SampleWorldHeight(x, z);

				const float yaw = distYaw(rng);
				const float scale = distScale(rng);
				const uint meshIdx = distMesh(rng);

				flecs::entity e = ecs.entity();
				e.set<CName>({ "Tree" });

				CTransform tr = {};
				tr.Position = { x, y, z };
				tr.Rotation = { 0.0f, yaw, 0.0f };
				tr.Scale = { scale, scale, scale };
				e.set<CTransform>(tr);

				CMeshRenderer mr = {};
				mr.MeshRef = treeAssets[meshIdx];
				mr.bCastShadow = true;

				RenderScene::RenderObject obj;
				obj.Mesh = treeMeshes[meshIdx];
				obj.Transform = GrassViewer::ToMatrixTRS(tr);
				obj.bCastShadow = mr.bCastShadow;

				mr.RenderObjectHandle = m_pRenderScene->AddObject(std::move(obj));
				e.set<CMeshRenderer>(mr);
			}
		}

		// ------------------------------------------------------------
		// Helmets: render + dynamic physics box collider
		// ------------------------------------------------------------
		{
			AssetRef<StaticMesh> helmetRef =
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/DamagedHelmet.shzmesh.json");

			const StaticMeshRenderData helmetMeshRD = m_pRenderer->CreateStaticMesh(helmetRef);

			// Spawn config
			constexpr uint32 kHelmetCount = 300;
			constexpr float  kMinY = 20.0f;
			constexpr float  kMaxY = 50.0f;

			// XZ grid offsets
			constexpr int   kGridX = 6;
			constexpr float kSpacingX = 1.0f;
			constexpr float kSpacingZ = 1.0f;
			constexpr float kBaseX = -10.0f;
			constexpr float kBaseZ = 10.0f;

			std::mt19937 rng(1337);
			std::uniform_real_distribution<float> distY(kMinY, kMaxY);
			std::uniform_real_distribution<float> distYaw(0.0f, TWO_PI);

			for (uint32 i = 0; i < kHelmetCount; ++i)
			{
				const int ix = (int)(i % kGridX);
				const int iz = (int)(i / kGridX);

				const float x = kBaseX + (float)ix * kSpacingX;
				const float z = kBaseZ + (float)iz * kSpacingZ;

				const float y = distY(rng);
				const float yaw = distYaw(rng);

				flecs::entity e = ecs.entity();
				e.set<CName>({ "Helmet" });

				CTransform tr = {};
				tr.Position = { x, y, z };
				tr.Rotation = { 0.0f, yaw, 0.0f };
				tr.Scale = { 1.0f, 1.0f, 1.0f };
				e.set<CTransform>(tr);

				// Render object
				RenderScene::RenderObject obj;
				obj.Mesh = helmetMeshRD;
				obj.Transform = GrassViewer::ToMatrixTRS(tr);
				obj.bCastShadow = true;

				Box bounds = obj.Mesh.LocalBounds;

				CMeshRenderer mr = {};
				mr.MeshRef = helmetRef;
				mr.bCastShadow = true;
				mr.RenderObjectHandle = m_pRenderScene->AddObject(std::move(obj));
				e.set<CMeshRenderer>(mr);

				CBoxCollider box = {};
				box.Box = bounds;
				box.bIsSensor = false;
				e.set<CBoxCollider>(box);

				CRigidbody rb = {};
				rb.Motion = ERigidBodyMotion::Dynamic;
				rb.Layer = 1; // Moving
				rb.Mass = 1.0f;
				rb.LinearDamping = 0.0f;
				rb.AngularDamping = 0.0f;
				rb.bEnableGravity = true;
				rb.bAllowSleeping = true;
				rb.bStartActive = true;
				e.set<CRigidbody>(rb);
			}
		}
	}

} // namespace shz
