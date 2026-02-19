#include "GrassViewer.h"

#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <unordered_set>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/RuntimeData/Public/StaticMeshImporter.h"
#include "Engine/RuntimeData/Public/TextureImporter.h"
#include "Engine/RuntimeData/Public/MaterialImporter.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"
#include "Engine/AssetManager/Public/AssimpImporter.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	namespace
	{
		static void updatePrimaryView(
			ViewFamily& vf,
			const GrassViewer::ViewportState& vp,
			const FirstPersonCamera& cam)
		{
			ASSERT(!vf.Views.empty(), "No view.");

			auto& v = vf.Views[0];

			v.PrevViewMatrix = v.ViewMatrix;
			v.PrevProjMatrix = v.ProjMatrix;
			v.PrevViewProjMatrix = v.ViewProjMatrix;

			v.CameraPosition = cam.GetPos();

			v.ViewMatrix = cam.GetViewMatrix();
			v.ProjMatrix = cam.GetProjMatrix();
			v.ViewProjMatrix = v.ViewMatrix * v.ProjMatrix;

			v.FieldOfViewY = cam.GetProjAttribs().FOV;
			v.AspectRatio = cam.GetProjAttribs().AspectRatio;

			v.Viewport.left = 0;
			v.Viewport.top = 0;
			v.Viewport.right = vp.Width;
			v.Viewport.bottom = vp.Height;

			v.NearPlane = cam.GetProjAttribs().NearClipPlane;
			v.FarPlane = cam.GetProjAttribs().FarClipPlane;

			v.bOrthographic = false;
			v.OrthographicSize = 0.0f;
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
			m_pAssetManager->RegisterImporter(AssetTypeTraits<StaticMeshLevel>::TypeID, StaticMeshImporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<Texture>::TypeID, TextureImporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<Material>::TypeID, MaterialImporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<AssimpAsset>::TypeID, AssimpImporter{});
		}

		// Renderer + shader factory
		m_pRenderer = std::make_unique<Renderer>();
		{
			ASSERT(m_pRenderer, "Renderer is null.");

			ASSERT(m_pEngineFactory, "EngineFactory is null.");
			m_pEngineFactory->CreateDefaultShaderSourceStreamFactory("C:/Dev/ShizenEngine/Shaders", &m_pShaderSourceFactory);
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

		// -----------------------------------------------------------------
		// Terrain system
		// -----------------------------------------------------------------
		{
			m_pTerrainSystem = std::make_unique<TerrainSystem>();

			TerrainSystem::CreateInfo tci = {};

			tci.HeightPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/height.png";
			tci.DiffusePath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/diffuse.png";
			tci.NormalPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/normal.png";
			tci.SlopePath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/slope.png";
			tci.FlowPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/flow.png";
			tci.RockyPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/rocky.png";
			tci.SoilPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/soil.png";
			tci.VegetationPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/vegetation.png";

			tci.SoilMaterialPath = "C:/Dev/ShizenEngine/Assets/Materials/Ground037_2K-PNG";
			tci.RockyMaterialPath = "C:/Dev/ShizenEngine/Assets/Materials/Rock030_4K-PNG";

			tci.ChunkSize = 64.0f;
			tci.CellSize = 1.0f;
			tci.WorldSpacing = 1.22f;
			tci.HeightScale = 1500.0f;
			tci.HeightOffset = 0.0f;
			tci.bCenterXZ = true;

			m_pTerrainSystem->Initialize(*m_pRenderer, *m_pAssetManager, tci);
		}

		// Render Scene
		{
			m_pRenderScene = std::make_unique<RenderScene>();
			ASSERT(m_pRenderScene, "RenderScene is null.");
		}

		// -----------------------------------------------------------------
		// Create render systems
		// -----------------------------------------------------------------
		{
			m_pInteractionSystem = std::make_unique<InteractionSystem>();
			m_pGrassSystem = std::make_unique<GrassSystem>();

			m_pInteractionSystem->Initialize(*m_pRenderer);
			m_pGrassSystem->Initialize(*m_pRenderer);

			// ------------------------------------------------------------
			// Grass model
			// ------------------------------------------------------------
			{
				m_pRenderer->RegisterMaterialTemplate("GrassMesh", "GrassMesh.vsh", "Grass.psh", MATERIAL_BLEND_MODE_MASKED);
				m_pRenderer->RegisterMaterialTemplate("GrassCrossPlane", "GrassCrossPlane.vsh", "Grass.psh", MATERIAL_BLEND_MODE_MASKED);
				m_pRenderer->RegisterMaterialTemplate("GrassBillboard", "GrassBillboard.vsh", "Grass.psh", MATERIAL_BLEND_MODE_MASKED);

				auto uniform01 = [](StaticMeshLevel& mesh)
				{
					mesh.RecomputeBounds();
					const Box& b = mesh.GetBoxBounds();
					float yScale01 = 1.0f / (b.Max().y - b.Min().y);
					mesh.ApplyUniformScale(yScale01);
					mesh.MoveBottomToOrigin(true);
				};

				//{
				//	GrassDesc gd = {};
				//	StaticMesh grassMesh;
				//	// LOD0 : Mesh
				//	{
				//		AssetRef<AssimpAsset> grassMeshRef = m_pAssetManager->RegisterAsset<AssimpAsset>("C:/Dev/ShizenEngine/Assets/Grass/basic/GrassBasic_default.fbx");
				//		const AssimpAsset& grassAssimp = *m_pAssetManager->LoadBlocking(grassMeshRef);
				//		StaticMeshLevel grassMeshLevel;
				//		BuildStaticMeshAsset(grassAssimp, &grassMeshLevel, {}, "GrassMesh", nullptr, m_pAssetManager.get());
				//		uniform01(grassMeshLevel);
				//		for (auto matId : grassMeshLevel.GetMaterialSlots())
				//		{
				//			Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
				//			mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD0"));
				//			mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
				//			mat.SetCullMode(CULL_MODE_NONE);
				//		}
				//		grassMesh.AddLevel(std::move(grassMeshLevel), 1.0f);
				//	}
				//	// LOD1 : Cross-plane
				//	{
				//		AssetRef<AssimpAsset> grassCrossRef = m_pAssetManager->RegisterAsset<AssimpAsset>("C:/Dev/ShizenEngine/Assets/Grass/basic/GrassBasic_cross4r.fbx");
				//		const AssimpAsset& grassCrossAssimp = *m_pAssetManager->LoadBlocking(grassCrossRef);
				//		StaticMeshLevel grassCrossMeshLevel;
				//		BuildStaticMeshAsset(grassCrossAssimp, &grassCrossMeshLevel, {}, "GrassCrossPlane", nullptr, m_pAssetManager.get());
				//		uniform01(grassCrossMeshLevel);
				//		for (auto matId : grassCrossMeshLevel.GetMaterialSlots())
				//		{
				//			Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
				//			mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD1"));
				//			mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
				//			mat.SetCullMode(CULL_MODE_NONE);
				//		}
				//		grassMesh.AddLevel(std::move(grassCrossMeshLevel), 0.5f);
				//	}
				//	// LOD2 : Billboard
				//	{
				//		AssetRef<Texture> grassBillboardTexRef = m_pAssetManager->RegisterAsset<Texture>("C:/Dev/ShizenEngine/Assets/Grass/basic/clips/v1.png");
				//		StaticMeshLevel grassBiilboardMeshLevel = StaticMeshLevel::CreateBillboard(grassBillboardTexRef, "GrassBillboard", MATERIAL_BLEND_MODE_MASKED, { 1.0f, 1.0f }, { 0.5f, 0.0f });
				//		for (auto matId : grassBiilboardMeshLevel.GetMaterialSlots())
				//		{
				//			Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
				//			mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD2"));
				//			mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
				//			mat.SetCullMode(CULL_MODE_NONE);
				//		}
				//		grassMesh.AddLevel(std::move(grassBiilboardMeshLevel), 0.25f);
				//	}
				//	gd.pMesh = &m_pRenderer->CreateStaticMeshRenderData(grassMesh);
				//	m_pGrassSystem->AddGrassDesc(gd);
				//}

				//{
				//	GrassDesc gd = {};
				//	StaticMesh grassMesh;
				//	// LOD0 : Mesh
				//	{
				//		AssetRef<AssimpAsset> grassMeshRef = m_pAssetManager->RegisterAsset<AssimpAsset>("C:/Dev/ShizenEngine/Assets/Grass/white-flower/white_flower.fbx");
				//		const AssimpAsset& grassAssimp = *m_pAssetManager->LoadBlocking(grassMeshRef);
				//		StaticMeshLevel grassMeshLevel;
				//		BuildStaticMeshAsset(grassAssimp, &grassMeshLevel, {}, "GrassMesh", nullptr, m_pAssetManager.get());
				//		uniform01(grassMeshLevel);
				//		for (auto matId : grassMeshLevel.GetMaterialSlots())
				//		{
				//			Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
				//			mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD0"));
				//			mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
				//		}
				//		grassMesh.AddLevel(std::move(grassMeshLevel), 1.0f);
				//	}
				//	// LOD1 : Cross-plane
				//	{
				//		AssetRef<AssimpAsset> grassCrossRef = m_pAssetManager->RegisterAsset<AssimpAsset>("C:/Dev/ShizenEngine/Assets/Grass/white-flower/white_flower_cross.fbx");
				//		const AssimpAsset& grassCrossAssimp = *m_pAssetManager->LoadBlocking(grassCrossRef);
				//		StaticMeshLevel grassCrossMeshLevel;
				//		BuildStaticMeshAsset(grassCrossAssimp, &grassCrossMeshLevel, {}, "GrassCrossPlane", nullptr, m_pAssetManager.get());
				//		uniform01(grassCrossMeshLevel);
				//		for (auto matId : grassCrossMeshLevel.GetMaterialSlots())
				//		{
				//			Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
				//			mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD1"));
				//			mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
				//			mat.SetCullMode(CULL_MODE_NONE);
				//		}
				//		grassMesh.AddLevel(std::move(grassCrossMeshLevel), 0.5f);
				//	}
				//	// LOD2 : Billboard
				//	{
				//		AssetRef<Texture> grassBillboardTexRef = m_pAssetManager->RegisterAsset<Texture>("C:/Dev/ShizenEngine/Assets/Grass/white-flower/clips/l.png");
				//		StaticMeshLevel grassBiilboardMeshLevel = StaticMeshLevel::CreateBillboard(grassBillboardTexRef, "GrassBillboard", MATERIAL_BLEND_MODE_MASKED, { 1.0f, 1.0f }, { 0.5f, 0.0f });
				//		for (auto matId : grassBiilboardMeshLevel.GetMaterialSlots())
				//		{
				//			Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
				//			mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD2"));
				//			mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
				//			mat.SetCullMode(CULL_MODE_NONE);
				//		}
				//		grassMesh.AddLevel(std::move(grassBiilboardMeshLevel), 0.25f);
				//	}
				//	gd.pMesh = &m_pRenderer->CreateStaticMeshRenderData(grassMesh);
				//	m_pGrassSystem->AddGrassDesc(gd);
				//}

				auto addGrass = [&](const std::string& path)
				{
					GrassDesc gd = {};
					StaticMesh grassMesh;
					// LOD0 : Mesh
					{
						AssetRef<AssimpAsset> grassMeshRef = m_pAssetManager->RegisterAsset<AssimpAsset>(path);
						const AssimpAsset& grassAssimp = *m_pAssetManager->LoadBlocking(grassMeshRef);
						StaticMeshLevel grassMeshLevel;
						BuildStaticMeshAsset(grassAssimp, &grassMeshLevel, {}, "GrassMesh", nullptr, m_pAssetManager.get());
						uniform01(grassMeshLevel);
						for (auto matId : grassMeshLevel.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD0"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);
						}
						grassMesh.AddLevel(std::move(grassMeshLevel), 1.0f);
					}
					// LOD1 : Cross-plane
					{
						AssetRef<AssimpAsset> grassCrossRef = m_pAssetManager->RegisterAsset<AssimpAsset>(path);
						const AssimpAsset& grassCrossAssimp = *m_pAssetManager->LoadBlocking(grassCrossRef);
						StaticMeshLevel grassCrossMeshLevel;
						BuildStaticMeshAsset(grassCrossAssimp, &grassCrossMeshLevel, {}, "GrassCrossPlane", nullptr, m_pAssetManager.get());
						uniform01(grassCrossMeshLevel);
						for (auto matId : grassCrossMeshLevel.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD1"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);
						}
						grassMesh.AddLevel(std::move(grassCrossMeshLevel), 0.5f);
					}
					// LOD2 : Billboard
					{
						AssetRef<Texture> grassBillboardTexRef = m_pAssetManager->RegisterAsset<Texture>("C:/Dev/ShizenEngine/Assets/Grass/basic/clips/v1.png");
						StaticMeshLevel grassBiilboardMeshLevel = StaticMeshLevel::CreateBillboard(grassBillboardTexRef, "GrassBillboard", MATERIAL_BLEND_MODE_MASKED, { 1.0f, 1.0f }, { 0.5f, 0.0f });
						for (auto matId : grassBiilboardMeshLevel.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD2"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);
						}
						grassMesh.AddLevel(std::move(grassBiilboardMeshLevel), 0.25f);
					}
					gd.pMesh = &m_pRenderer->CreateStaticMeshRenderData(grassMesh);
					m_pGrassSystem->AddGrassDesc(gd);
				};

				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_big_clump_A.fbx");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_big_clump_A1.fbx");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_big_clump_B.fbx");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_big_clump_B1.fbx");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_big_clump_C.fbx");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_big_clump_D.fbx");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_small_clump_A.fbx");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_small_clump_A1.fbx");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_small_clump_A2.fbx");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_small_clump_A3.fbx");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_small_clump_A4.fbx");
			}

			m_pInteractionSystem->InstallPasses(*m_pRenderer, *m_pTerrainSystem);
			m_pGrassSystem->InstallPasses(*m_pRenderer, *m_pRenderScene, *m_pInteractionSystem);
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

		// ECS: systems
		{
			// Install Physics <-> ECS systems (CreateBodies / PushTransform / WriteBack / OnRemove)
			m_pPhysicsSystem->InstallEcsSystems(*m_pEcs);

			// Update: Transform -> RenderScene sync
			{
				auto sys = m_pEcs->World().observer<CTransform, CMeshRenderer>("Render.SyncTransforms")
					.event(flecs::OnSet)
					.each([this](CTransform& tr, CMeshRenderer& mr)
						{
							if (!mr.RenderObjectHandle.IsValid())
								return;

							m_pRenderScene->UpdateObjectTransform(
								mr.RenderObjectHandle,
								Matrix4x4::TRS(tr.Position, tr.Rotation, tr.Scale));
						});
				m_pEcs->RegisterUpdateSystem(sys);
			}

			{
				auto sys = m_pEcs->World().system<CTransform, CRigidbody, CHeightFieldCollider>("World.UpdateInteractionField")
					.each([this](const CTransform& tr, const CRigidbody& rb, const CHeightFieldCollider& hf)
						{
							const PhysicsBodyHandle terrainBody = PhysicsBodyHandle{ rb.BodyHandle };
							const auto& events = m_pPhysicsSystem->GetContactEvents();

							// Prevent stamping same dynamic body multiple times in this terrain iteration.
							// Key: otherBody.Value (packed+1 handle value)
							std::unordered_set<uint32> stampedThisFrame;
							stampedThisFrame.reserve(64);

							for (const ContactEvent& contact : events)
							{
								const bool bInvolved =
									(contact.BodyA.Value == terrainBody.Value) ||
									(contact.BodyB.Value == terrainBody.Value);

								if (!bInvolved)
								{
									continue;
								}

								// Ignore removed for stamping (decay handles recovery).
								if (contact.Type == EContactEventType::Removed)
								{
									continue;
								}

								const PhysicsBodyHandle otherBody =
									(contact.BodyA.Value == terrainBody.Value) ? contact.BodyB : contact.BodyA;

								// Only dynamic bodies create interaction stamps.
								if (m_pPhysicsSystem->GetPhysics().GetBodyMotion(otherBody) != ERigidbodyType::Dynamic)
								{
									continue;
								}

								// Deduplicate per-frame per-body.
								if (!stampedThisFrame.emplace(otherBody.Value).second)
								{
									continue;
								}

								const float3 pWS = m_pPhysicsSystem->GetPhysics().GetBodyPosition(otherBody);

								hlsl::InteractionStamp stamp = {};
								stamp.CenterXZ = float2{ pWS.x, pWS.z };
								stamp.Radius = 1.5f;
								stamp.FalloffPower = 10.0f;
								stamp.TargetId = 0;

								if (contact.Type == EContactEventType::Added)
								{
									stamp.Strength = 1.0f;
									stamp.Flags = hlsl::INTERACTION_STAMP_MAX_BLEND; // still fine on Added
								}
								else // Persisted
								{
									// "Maintain" pressure: choose a slightly lower target than Added.
									stamp.Strength = 0.65f;
									stamp.Flags = hlsl::INTERACTION_STAMP_MAX_BLEND;
								}

								m_pRenderScene->AddInteractionStamp(stamp);
							}
						});

				m_pEcs->RegisterUpdateSystem(sys);
			}
		}

		// ViewFamily + Camera
		{
			m_ViewFamily.Views.clear();
			m_ViewFamily.Views.push_back({});

			m_Camera.SetPos(float3(0.0f, m_pTerrainSystem->SampleWorldHeight(0.0f, 0.0f) + 1.0f, 0.0f));
			m_Camera.SetRotation(0.0f, 0.0f);
			m_Camera.SetRotation(-2.63f, -0.13f);
			m_Camera.SetMoveSpeed(3.0f);
			m_Camera.SetSpeedUpScales(5.0f, 5.0f);
			m_Camera.SetRotationSpeed(0.01f);

			m_Camera.SetProjAttribs(
				0.1f,
				8000.0f,
				(float)m_Viewport.Width / (float)m_Viewport.Height,
				PI / 4.0f,
				SURFACE_TRANSFORM_IDENTITY);
		}

		// Light
		{
			m_GlobalLight.Direction = float3(0.4f, -1.0f, 0.3f);
			m_GlobalLight.Color = float3(1.0f, 1.0f, 1.0f);
			m_GlobalLight.Intensity = 2.0f;
		}
		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);
		ASSERT(m_GlobalLightHandle.IsValid(), "Failed to add global light.");

		// Build scene once (ECS-driven objects)
		BuildSceneOnce();

		// Fill first view immediately
		updatePrimaryView(m_ViewFamily, m_Viewport, m_Camera);
	}

	void GrassViewer::Render()
	{
		bench::Timer timer;

		ASSERT(m_pRenderer, "Renderer is null.");
		ASSERT(m_pRenderScene, "RenderScene is null.");

		m_ViewFamily.FrameIndex++;

		m_pRenderer->BeginFrame();
		m_pRenderer->Render(*m_pRenderScene, m_ViewFamily);
		m_pRenderer->EndFrame();

		m_RenderMs = timer.ElapsedMs();
		const double a = (double)std::clamp(m_TimingEmaAlpha, 0.0f, 1.0f);
		m_RenderMsEMA = a * m_RenderMs + (1.0 - a) * m_RenderMsEMA;
	}

	void GrassViewer::Update(double currTime, double elapsedTime, bool doUpdateUI)
	{
		bench::Timer timer;

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

		m_pTerrainSystem->Update(*m_pRenderer, m_pRenderScene.get(), m_ViewFamily.Views[0]);

		// Update grass render constants
		{
			hlsl::GrassRenderConstants ren = {};
			ren.BaseColorFactor = float4(150.f, 200.f, 100.f, 255.f) / 255.f;
			ren.Tint = float4{ 1.05f, 1.00f, 0.95f, 1.0f };

			ren.WindDirXZ = float2{ 0.80f, 0.60f }.Normalized();
			ren.WindStrength = 0.30f;
			ren.WindSpeed = 2.25f;

			ren.WindFreq = 3.255f;
			ren.WindGust = 0.42f;
			ren.MaxBendAngle = 0.35f;
			ren._pad1 = 0.0f;

			ren.InteractionBendAngle = 1.0f;
			ren.InteractionSink = 0.05f;
			ren.InteractionWindFade = 0.95f;

			m_pRenderer->UpdateBuffer<hlsl::GrassRenderConstants>(STRING_HASH("GrassRenderConstantsCB"), ren);
		}

		m_UpdateMs = timer.ElapsedMs();
		const double a = (double)std::clamp(m_TimingEmaAlpha, 0.0f, 1.0f);
		m_UpdateMsEMA = a * m_UpdateMs + (1.0 - a) * m_UpdateMsEMA;
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
			ImGui::Text("CPU Timings (ms)");
			ImGui::Text("Update: %.3f (EMA %.3f)", (float)m_UpdateMs, (float)m_UpdateMsEMA);
			ImGui::Text("Render:  %.3f (EMA %.3f)", (float)m_RenderMs, (float)m_RenderMsEMA);
			ImGui::Text("Frame:   %.3f (EMA %.3f)",
				(float)(m_UpdateMs + m_RenderMs),
				(float)(m_UpdateMsEMA + m_RenderMsEMA));

			ImGui::SliderFloat("Timing EMA Alpha", &m_TimingEmaAlpha, 0.01f, 0.5f, "%.3f");

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
	}

	// ------------------------------------------------------------
	// Scene build (same behavior, but physics now via ECS components)
	// ------------------------------------------------------------

	void GrassViewer::BuildSceneOnce()
	{
		ASSERT(m_pAssetManager && m_pRenderer && m_pRenderScene && m_pEcs && m_pPhysicsSystem, "Subsystem missing.");

		auto& ecs = m_pEcs->World();

		// -------------------------------------------------------------------------
		// Helpers (lambdas)
		// -------------------------------------------------------------------------
		auto loadTexture = [&](const std::string& path) -> const Texture&
		{
			AssetRef<Texture> ref = m_pAssetManager->RegisterAsset<Texture>(path);
			return *m_pAssetManager->LoadBlocking(ref);
		};

		auto getPixelStrideBytes = [&](const Texture& tex) -> uint32
		{
			const TEXTURE_FORMAT fmt = tex.GetFormat();
			const TextureFormatAttribs& a = GetTextureFormatAttribs(fmt);

			// Diligent: ComponentSize = bytes per component, NumComponents = channels
			const uint32 compSize = a.ComponentSize;
			const uint32 numComp = (a.NumComponents > 0) ? a.NumComponents : 1;
			return compSize * numComp;
		};

		auto getMip0 = [&](const Texture& tex) -> const TextureMip&
		{
			ASSERT(!tex.GetMips().empty(), "Texture has no mips.");
			return tex.GetMips()[0];
		};

		auto valueToBucket = [&](uint8 value) -> uint32
		{
			// 0: >200, 1: >=150, 2: >=100, 3: >=50, 4: >=1, else: skip
			if (value > 200) return 0;
			if (value >= 150) return 1;
			if (value >= 100) return 2;
			if (value >= 50)  return 3;
			if (value >= 1)   return 4;
			return 999;
		};

		enum class ETreeSize : uint8
		{
			Large,
			Big,
			Medium,
			Small,
			Sapling,
			Count
		};

		auto loadTree = [&](const std::string& folderName) -> const StaticMeshRenderData*
		{
			std::vector<AssetRef<AssimpAsset>> treeMeshRefs(4);
			for (uint lod = 0; lod < 4; ++lod)
			{
				std::string path = std::string("C:/Dev/ShizenEngine/Assets/Tree/pine_trees/") + folderName + "/lod" + std::to_string(lod) + ".fbx";
				treeMeshRefs[lod] = m_pAssetManager->RegisterAsset<AssimpAsset>(path);
			}
			return &m_pRenderer->CreateStaticMeshRenderData(treeMeshRefs);
		};

		auto pickSizeByBucket = [&](uint32 bucket, std::mt19937& rng) -> ETreeSize
		{
			std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
			const float r = dist01(rng);

			switch (bucket)
			{
			case 0:
				// Large 25% / Big 30% / Medium 25% / Small 15% / Sapling 5%
				if (r < 0.25f) return ETreeSize::Large;
				if (r < 0.55f) return ETreeSize::Big;
				if (r < 0.80f) return ETreeSize::Medium;
				if (r < 0.95f) return ETreeSize::Small;
				return ETreeSize::Sapling;

			case 1:
				// Large 10% / Big 25% / Medium 40% / Small 20% / Sapling 5%
				if (r < 0.10f) return ETreeSize::Large;
				if (r < 0.35f) return ETreeSize::Big;
				if (r < 0.75f) return ETreeSize::Medium;
				if (r < 0.95f) return ETreeSize::Small;
				return ETreeSize::Sapling;

			case 2:
				// Large 5% / Big 10% / Medium 50% / Small 25% / Sapling 10%
				if (r < 0.05f) return ETreeSize::Large;
				if (r < 0.15f) return ETreeSize::Big;
				if (r < 0.65f) return ETreeSize::Medium;
				if (r < 0.90f) return ETreeSize::Small;
				return ETreeSize::Sapling;

			case 3:
				// Large 5% / Big 5% / Medium 25% / Small 50% / Sapling 15%
				if (r < 0.05f) return ETreeSize::Large;
				if (r < 0.10f) return ETreeSize::Big;
				if (r < 0.35f) return ETreeSize::Medium;
				if (r < 0.85f) return ETreeSize::Small;
				return ETreeSize::Sapling;

			case 4:
				// Large 0% / Big 5% / Medium 15% / Small 30% / Sapling 50%
				if (r < 0.05f) return ETreeSize::Big;
				if (r < 0.20f) return ETreeSize::Medium;
				if (r < 0.50f) return ETreeSize::Small;
				return ETreeSize::Sapling;
			default:
				ASSERT(false, "Invalid bucket value.");
				return ETreeSize::Medium;
			}
		};

		auto pickScaleBySize = [&](ETreeSize size, std::mt19937& rng) -> float
		{
			switch (size)
			{
			case ETreeSize::Large: { std::uniform_real_distribution<float> d(0.90f, 1.15f); return d(rng); }
			case ETreeSize::Big: { std::uniform_real_distribution<float> d(0.90f, 1.12f); return d(rng); }
			case ETreeSize::Medium: { std::uniform_real_distribution<float> d(0.90f, 1.10f); return d(rng); }
			case ETreeSize::Small: { std::uniform_real_distribution<float> d(0.90f, 1.08f); return d(rng); }
			case ETreeSize::Sapling: { std::uniform_real_distribution<float> d(0.90f, 1.06f); return d(rng); }
			default: return 1.0f;
			}
		};

		auto addRenderOnlyStaticMeshEntity = [&](
			const char* name,
			const StaticMeshRenderData& meshRD,
			const float3& pos,
			const float3& rot,
			const float3& scl,
			bool bCastShadow) -> flecs::entity
		{
			flecs::entity e = ecs.entity();
			e.set<CName>({ name });

			CTransform tr = {};
			tr.Position = pos;
			tr.Rotation = rot;
			tr.Scale = scl;
			e.set<CTransform>(tr);

			CMeshRenderer mr = {};
			mr.MeshRef = {};
			mr.bCastShadow = bCastShadow;
			mr.RenderObjectHandle = m_pRenderScene->AddObject(
				meshRD,
				Matrix4x4::TRS(tr.Position, tr.Rotation, tr.Scale),
				bCastShadow);
			e.set<CMeshRenderer>(mr);

			return e;
		};

		// -------------------------------------------------------------------------
		// Physics Terrain: HeightFieldCollider + Static Rigidbody
		// -------------------------------------------------------------------------
		{
			const uint32 W = m_pTerrainSystem->GetWidth();
			const uint32 H = m_pTerrainSystem->GetHeight();

			ASSERT(W == H, "HeightField collider: width/height must be equal (square) for your current pipeline.");

			std::vector<float> samples;
			m_pTerrainSystem->BuildPhysicsHeightSamples(samples);
			ASSERT(samples.size() == size_t(W) * size_t(H), "HeightField samples size mismatch.");

			const float spacing = m_pTerrainSystem->GetWorldSpacing();
			const float worldOriginX = m_pTerrainSystem->GetWorldOriginX();
			const float worldOriginZ = m_pTerrainSystem->GetWorldOriginZ();

			flecs::entity e = ecs.entity();
			e.set<CName>({ "TerrainPhysics" });

			CTransform tr = {};
			tr.Position = { worldOriginX, 0.0f, worldOriginZ };
			tr.Rotation = { 0.0f, 0.0f, 0.0f };
			tr.Scale = { 1.0f, 1.0f, 1.0f };
			e.set<CTransform>(tr);

			CRigidbody rb = {};
			rb.BodyType = ERigidbodyType::Static;
			rb.Layer = 0; // NonMoving
			rb.bEnableGravity = false;
			rb.bStartActive = false;
			e.set<CRigidbody>(rb);

			CHeightFieldCollider hf = {};
			hf.Width = W;
			hf.Height = H;
			hf.CellSizeX = spacing;
			hf.CellSizeZ = spacing;
			hf.HeightScale = 1.0f;   // samples already in world meters
			hf.HeightOffset = 0.0f;
			hf.Heights = std::move(samples);
			e.set<CHeightFieldCollider>(hf);
		}

		// -------------------------------------------------------------------------
		// Trees: placement texture driven, LOD2 fixed, size-by-bucket + variant uniform
		// -------------------------------------------------------------------------
		{
			const std::string treePlacementTexPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/trees.png";
			const Texture& treePlacementTex = loadTexture(treePlacementTexPath);

			const TextureMip& mip0 = getMip0(treePlacementTex);
			const uint32 pixelStrideBytes = getPixelStrideBytes(treePlacementTex);

			// Analyze counts (optional)
			{
				uint32 c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0;

				for (uint32 y = 0; y < mip0.Height; ++y)
				{
					for (uint32 x = 0; x < mip0.Width; ++x)
					{
						const uint32 idx = (y * mip0.Width + x) * pixelStrideBytes;
						const uint8 value = mip0.Data[idx + 0];

						if (value > 200) c0++;
						else if (value >= 150) c1++;
						else if (value >= 100) c2++;
						else if (value >= 50)  c3++;
						else if (value >= 1)   c4++;
					}
				}

				std::cout << "Tree placement tex analysis (pixel count per threshold):\n";
				std::cout << " >200 (bucket0): " << c0 << "\n";
				std::cout << " >=150(bucket1): " << c1 << "\n";
				std::cout << " >=100(bucket2): " << c2 << "\n";
				std::cout << " >=50 (bucket3): " << c3 << "\n";
				std::cout << " >=1  (bucket4): " << c4 << "\n";
			}

			const StaticMeshRenderData* sizeMeshes[(int)ETreeSize::Count] = {};
			sizeMeshes[(int)ETreeSize::Large] = loadTree("large_1");
			sizeMeshes[(int)ETreeSize::Big] = loadTree("big_1");
			sizeMeshes[(int)ETreeSize::Medium] = loadTree("medium_1");
			sizeMeshes[(int)ETreeSize::Small] = loadTree("small_1");
			sizeMeshes[(int)ETreeSize::Sapling] = loadTree("sapling_1");

			constexpr float4 SPAWN_RANGE = { -5000.0f, -5000.0f, 5000.0f, 5000.0f };

			float yOffset = 0.0f;
			std::mt19937 rng(1337);

			std::uniform_real_distribution<float> distYaw(0.0f, TWO_PI);
			std::uniform_real_distribution<float> distJitter(-0.45f, 0.45f);
			std::uniform_int_distribution<int>    distVariant(0, 2);

			uint32 spawned = 0;

			for (uint32 y = 0; y < mip0.Height; ++y)
			{
				for (uint32 x = 0; x < mip0.Width; ++x)
				{
					const uint32 idx = (y * mip0.Width + x) * pixelStrideBytes;

					// Use first channel as grayscale
					const uint8 value = mip0.Data[idx + 0];
					if (value < 1)
					{
						continue;
					}

					const uint32 bucket = valueToBucket(value);
					const ETreeSize size = pickSizeByBucket(bucket, rng);
					const StaticMeshRenderData* pMeshRD = sizeMeshes[(int)size];

					float2 domainUV = float2((float)x / (float)mip0.Width, (float)y / (float)mip0.Height);
					float2 worldXZ = m_pTerrainSystem->DomainUVToWorldXZ(domainUV);

					const float worldY = m_pTerrainSystem->SampleWorldHeight(worldXZ.x, worldXZ.y) + yOffset;

					const float yaw = distYaw(rng);
					const float scl = pickScaleBySize(size, rng);

					addRenderOnlyStaticMeshEntity(
						"Tree",
						*pMeshRD,
						{ worldXZ.x, worldY, worldXZ.y },
						{ 0.0f, yaw, 0.0f },
						{ scl, scl, scl },
						true);

					++spawned;
				}
			}

			std::cout << "Spawned trees: " << spawned << "\n";
		}
	}
} // namespace shz
