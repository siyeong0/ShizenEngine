#include "GrassViewer.h"

#include <algorithm>
#include <random>
#include <cmath>
#include <unordered_set>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/RuntimeData/Public/StaticMeshImporter.h"
#include "Engine/RuntimeData/Public/TextureImporter.h"
#include "Engine/RuntimeData/Public/MaterialImporter.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"
#include "Engine/AssetManager/Public/AssimpImporter.h"

#include "Engine/RenderSystem/Public/DeferredSystem.h"
#include "Engine/RenderSystem/Public/ForwardSystem.h"
#include "Engine/RenderSystem/Public/ShadowSystem.h"
#include "Engine/RenderSystem/Public/PostProcessSystem.h"

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

			v.Viewport.left = 0;
			v.Viewport.top = 0;
			v.Viewport.right = vp.Width;
			v.Viewport.bottom = vp.Height;

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
		// Create common resources for passes
		// -----------------------------------------------------------------
		{
			// GBuffer textures
			static constexpr uint32 NUM_GBUFFERS = 4;
			{
				auto createGBufferTextureDesc = [&](uint32 w, uint32 h, TEXTURE_FORMAT fmt, const char* name) -> TextureDesc
				{
					TextureDesc td = {};
					td.Name = name;
					td.Type = RESOURCE_DIM_TEX_2D;
					td.Width = w;
					td.Height = h;
					td.MipLevels = 1;
					td.Format = fmt;
					td.SampleCount = 1;
					td.Usage = USAGE_DEFAULT;
					td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
					return td;
				};
				m_pRenderer->AddTexture(STRING_HASH("GBuffer0_Albedo"), createGBufferTextureDesc(m_Viewport.Width, m_Viewport.Height, TEX_FORMAT_RGBA8_UNORM, "GBuffer0_Albedo"));
				m_pRenderer->AddTexture(STRING_HASH("GBuffer1_Normal"), createGBufferTextureDesc(m_Viewport.Width, m_Viewport.Height, TEX_FORMAT_RGBA16_FLOAT, "GBuffer1_Normal"));
				m_pRenderer->AddTexture(STRING_HASH("GBuffer2_MRAO"), createGBufferTextureDesc(m_Viewport.Width, m_Viewport.Height, TEX_FORMAT_RGBA8_UNORM, "GBuffer2_MRAO"));
				m_pRenderer->AddTexture(STRING_HASH("GBuffer3_Emissive"), createGBufferTextureDesc(m_Viewport.Width, m_Viewport.Height, TEX_FORMAT_RGBA16_FLOAT, "GBuffer3_Emissive"));

				// Velocity
				{
					TextureDesc td = {};
					td.Name = "Velocity";
					td.Type = RESOURCE_DIM_TEX_2D;
					td.Width = m_Viewport.Width;
					td.Height = m_Viewport.Height;
					td.MipLevels = 1;
					td.Format = TEX_FORMAT_RG16_FLOAT;
					td.SampleCount = 1;
					td.Usage = USAGE_DEFAULT;
					td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

					m_pRenderer->AddTexture(STRING_HASH("Velocity"), td);
				}

				// Depth
				{
					TextureDesc td = {};
					td.Name = "GBufferDepth";
					td.Type = RESOURCE_DIM_TEX_2D;
					td.Width = m_Viewport.Width;
					td.Height = m_Viewport.Height;
					td.MipLevels = 1;
					td.SampleCount = 1;
					td.Usage = USAGE_DEFAULT;
					td.Format = TEX_FORMAT_R32_TYPELESS;
					td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

					m_pRenderer->AddTexture(STRING_HASH("GBufferDepth"), td);

					TextureViewDesc vd = {};
					vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
					vd.Format = TEX_FORMAT_D32_FLOAT;

					m_pRenderer->AddTextureView(STRING_HASH("GBufferDepth"), vd);

					vd = {};
					vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
					vd.Format = TEX_FORMAT_R32_FLOAT;
					m_pRenderer->AddTextureView(STRING_HASH("GBufferDepth"), vd);
				}
			}

			// Lighting
			{
				TextureDesc td = {};
				td.Name = "LightingScene";
				td.Type = RESOURCE_DIM_TEX_2D;
				td.Width = m_Viewport.Width;
				td.Height = m_Viewport.Height;
				td.MipLevels = 1;
				td.Format = m_pSwapChain->GetDesc().ColorBufferFormat;
				td.SampleCount = 1;
				td.Usage = USAGE_DEFAULT;
				td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

				m_pRenderer->AddTexture(STRING_HASH("LightingScene"), td);
			}

			// TAA History Ping-Pong
			{
				TextureDesc td = {};
				td.Type = RESOURCE_DIM_TEX_2D;
				td.Width = m_Viewport.Width;
				td.Height = m_Viewport.Height;
				td.MipLevels = 1;
				td.SampleCount = 1;
				td.Usage = USAGE_DEFAULT;
				td.Format = TEX_FORMAT_RGBA16_FLOAT;
				td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

				td.Name = "TAA_History0";
				m_pRenderer->AddTexture(STRING_HASH("TAA_History0"), td);

				td.Name = "TAA_History1";
				m_pRenderer->AddTexture(STRING_HASH("TAA_History1"), td);
			}

			{
				BufferDesc bd = {};
				bd.Name = "GrassRenderConstantsCB";
				bd.Usage = USAGE_DYNAMIC;
				bd.BindFlags = BIND_UNIFORM_BUFFER;
				bd.CPUAccessFlags = CPU_ACCESS_WRITE;
				bd.Size = sizeof(hlsl::GrassRenderConstants);

				m_pRenderer->AddBuffer(STRING_HASH("GrassRenderConstantsCB"), bd);
				m_pRenderer->RegisterStaticBufferCBV("GRASS_RENDER_CONSTANTS", STRING_HASH("GrassRenderConstantsCB"));
			}
		}

		// -----------------------------------------------------------------
		// Terrain system
		// -----------------------------------------------------------------
		{
			m_pTerrainSystem = std::make_unique<TerrainSystem>();

			TerrainSystem::CreateInfo tci = {};

			//tci.HeightMapPath = "C:/Dev/ShizenEngine/Assets/Terrain/Canyon/Height.png";
			//tci.DiffusePath = "C:/Dev/ShizenEngine/Assets/Terrain/Canyon/Diffuse.png";
			//tci.WorldSpacingX = 1.0f;
			//tci.WorldSpacingZ = 1.0f;
			//tci.HeightScale = 300.0f;
			//tci.HeightOffset = -80.0f;
			//tci.bCenterXZ = true;

			//tci.HeightMapPath = "C:/Dev/ShizenEngine/Assets/Terrain/Mountain001/height.png";
			//tci.DiffusePath = "C:/Dev/ShizenEngine/Assets/Terrain/Mountain001/diffuse.png";
			//tci.SoilPath = "C:/Dev/ShizenEngine/Assets/Terrain/Mountain001/soil.png";

			tci.HeightPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/height.png";
			tci.DiffusePath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/diffuse.png";
			tci.NormalPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/normal.png";
			tci.SlopePath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/slope.png";
			tci.FlowPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/flow.png";
			tci.RockyPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/rocky.png";
			tci.SoilPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/grass.png";
			tci.VegetationPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/vegetation.png";
			tci.TreesPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/trees.png";

			tci.WorldSpacingX = 1.0f;
			tci.WorldSpacingZ = 1.0f;
			tci.HeightScale = 3000.0f;
			tci.HeightOffset = -1000.0f;
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
			m_pIndirectArgsSystem = std::make_unique<IndirectArgsSystem>();
			m_pDeferredSystem = std::make_unique<DeferredSystem>();
			m_pPostProcessSystem = std::make_unique<PostProcessSystem>();
			m_pShadowSystem = std::make_unique<ShadowSystem>();
			m_pInteractionSystem = std::make_unique<InteractionSystem>();
			m_pGrassSystem = std::make_unique<GrassSystem>();

			// ------------------------------------------------------------
			// Grass model
			// ------------------------------------------------------------
			{
				m_pRenderer->RegisterMaterialTemplate("GrassMesh", "GrassMesh.vsh", "GBuffer.psh", MATERIAL_BLEND_MODE_MASKED);
				m_pRenderer->RegisterMaterialTemplate("GrassCrossPlane", "GrassCrossPlane.vsh", "GBuffer.psh", MATERIAL_BLEND_MODE_MASKED);
				m_pRenderer->RegisterMaterialTemplate("GrassBillboard", "GrassBillboard.vsh", "GBuffer.psh", MATERIAL_BLEND_MODE_MASKED);

				auto uniform01 = [](StaticMesh& mesh)
				{
					mesh.RecomputeBounds();
					const Box& b = mesh.GetBounds();
					float yScale01 = 1.0f / (b.Max.y - b.Min.y);
					mesh.ApplyUniformScale(yScale01);
					mesh.MoveBottomToOrigin(true);
				};

				{
					GrassDesc gd = {};
					// LOD0 : Mesh
					{
						AssetRef<AssimpAsset> grassMeshRef = m_pAssetManager->RegisterAsset<AssimpAsset>("C:/Dev/ShizenEngine/Assets/Grass/basic/GrassBasic_default.fbx");
						const AssimpAsset& grassAssimp = *m_pAssetManager->LoadBlocking(grassMeshRef);
						StaticMesh grassMesh;
						BuildStaticMeshAsset(grassAssimp, &grassMesh, {}, "GrassMesh", nullptr, m_pAssetManager.get());
						uniform01(grassMesh);
						for (auto matId : grassMesh.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD0"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);
						}
						gd.pMeshLod0 = &m_pRenderer->CreateStaticMeshRenderData(grassMesh);
					}
					// LOD1 : Cross-plane
					{
						AssetRef<AssimpAsset> grassCrossRef = m_pAssetManager->RegisterAsset<AssimpAsset>("C:/Dev/ShizenEngine/Assets/Grass/basic/GrassBasic_cross4r.fbx");
						const AssimpAsset& grassCrossAssimp = *m_pAssetManager->LoadBlocking(grassCrossRef);
						StaticMesh grassCrossMesh;
						BuildStaticMeshAsset(grassCrossAssimp, &grassCrossMesh, {}, "GrassCrossPlane", nullptr, m_pAssetManager.get());
						uniform01(grassCrossMesh);
						for (auto matId : grassCrossMesh.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD1"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);
						}
						gd.pCrossMeshLod1 = &m_pRenderer->CreateStaticMeshRenderData(grassCrossMesh);
					}
					// LOD2 : Billboard
					{
						AssetRef<Texture> grassBillboardTexRef = m_pAssetManager->RegisterAsset<Texture>("C:/Dev/ShizenEngine/Assets/Grass/basic/clips/v1.png");
						StaticMesh grassBiilboardMesh = CreateBillboard(grassBillboardTexRef, "GrassBillboard", MATERIAL_BLEND_MODE_MASKED, { 1.0f, 1.0f }, { 0.5f, 0.0f });
						for (auto matId : grassBiilboardMesh.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD2"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);
						}
						gd.pBillboardMeshLod2 = &m_pRenderer->CreateStaticMeshRenderData(grassBiilboardMesh);
					}
					m_pGrassSystem->AddGrassDesc(gd);
				}

				{
					GrassDesc gd = {};
					// LOD0 : Mesh
					{
						AssetRef<AssimpAsset> grassMeshRef = m_pAssetManager->RegisterAsset<AssimpAsset>("C:/Dev/ShizenEngine/Assets/Grass/white-flower/white_flower.fbx");
						const AssimpAsset& grassAssimp = *m_pAssetManager->LoadBlocking(grassMeshRef);
						StaticMesh grassMesh;
						BuildStaticMeshAsset(grassAssimp, &grassMesh, {}, "GrassMesh", nullptr, m_pAssetManager.get());
						uniform01(grassMesh);
						for (auto matId : grassMesh.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD0"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
						}
						gd.pMeshLod0 = &m_pRenderer->CreateStaticMeshRenderData(grassMesh);
					}
					// LOD1 : Cross-plane
					{
						AssetRef<AssimpAsset> grassCrossRef = m_pAssetManager->RegisterAsset<AssimpAsset>("C:/Dev/ShizenEngine/Assets/Grass/white-flower/white_flower_cross.fbx");
						const AssimpAsset& grassCrossAssimp = *m_pAssetManager->LoadBlocking(grassCrossRef);
						StaticMesh grassCrossMesh;
						BuildStaticMeshAsset(grassCrossAssimp, &grassCrossMesh, {}, "GrassCrossPlane", nullptr, m_pAssetManager.get());
						uniform01(grassCrossMesh);
						for (auto matId : grassCrossMesh.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD1"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);
						}
						gd.pCrossMeshLod1 = &m_pRenderer->CreateStaticMeshRenderData(grassCrossMesh);
					}
					// LOD2 : Billboard
					{
						AssetRef<Texture> grassBillboardTexRef = m_pAssetManager->RegisterAsset<Texture>("C:/Dev/ShizenEngine/Assets/Grass/white-flower/clips/l.png");
						StaticMesh grassBiilboardMesh = CreateBillboard(grassBillboardTexRef, "GrassBillboard", MATERIAL_BLEND_MODE_MASKED, { 1.0f, 1.0f }, { 0.5f, 0.0f });
						for (auto matId : grassBiilboardMesh.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD2"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);
						}
						gd.pBillboardMeshLod2 = &m_pRenderer->CreateStaticMeshRenderData(grassBiilboardMesh);
					}
					m_pGrassSystem->AddGrassDesc(gd);
				}
			}

			m_pDeferredSystem->InstallPasses(*m_pRenderer);
			m_pPostProcessSystem->InstallPasses(*m_pRenderer);
			m_pShadowSystem->InstallPasses(*m_pRenderer);
			m_pIndirectArgsSystem->InstallPasses(*m_pRenderer);
			m_pInteractionSystem->InstallPasses(*m_pRenderer, *m_pTerrainSystem);
			m_pGrassSystem->InstallPasses(*m_pRenderer, *m_pRenderScene, *m_pIndirectArgsSystem, *m_pInteractionSystem);
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
				auto sys = m_pEcs->World().system<CTransform, CMeshRenderer>("Render.SyncTransforms")
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

		// Update TERRAIN_CONSTANTS
		{
			hlsl::TerrainConstants hfc = {};
			hfc.WorldOriginXZ = float2{ m_pTerrainSystem->GetWorldOriginX(), m_pTerrainSystem->GetWorldOriginZ() };
			hfc.WorldSizeXZ = float2{ m_pTerrainSystem->GetWorldSizeX(), m_pTerrainSystem->GetWorldSizeZ() };
			hfc.WorldSpacingXZ = float2{ m_pTerrainSystem->GetWorldSpacingX(), m_pTerrainSystem->GetWorldSpacingZ() };
			hfc.HeightScale = m_pTerrainSystem->GetHeightScale();
			hfc.HeightOffset = m_pTerrainSystem->GetHeightOffset();
			hfc.NormalUpBias = 2.0f;

			const uint32 w = m_pTerrainSystem->GetWidth();
			const uint32 h = m_pTerrainSystem->GetHeight();
			hfc.HeightTexelSize = float2{ 1.0f / float(w) , 1.0f / float(h) };

			m_pRenderer->UpdateBuffer<hlsl::TerrainConstants>(STRING_HASH("TerrainCB"), hfc);
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
	}

	// ------------------------------------------------------------
	// Scene build (same behavior, but physics now via ECS components)
	// ------------------------------------------------------------

	void GrassViewer::BuildSceneOnce()
	{
		ASSERT(m_pAssetManager && m_pRenderer && m_pRenderScene && m_pEcs && m_pPhysicsSystem, "Subsystem missing.");

		auto& ecs = m_pEcs->World();

		// ------------------------------------------------------------
		// Physics Terrain: HeightFieldCollider + Static Rigidbody
		// ------------------------------------------------------------
		{
			const uint32 W = m_pTerrainSystem->GetWidth();
			const uint32 H = m_pTerrainSystem->GetHeight();

			ASSERT(W == H, "HeightField collider: width/height must be equal (square) for your current pipeline.");

			// Convert to float heights (world meters)
			std::vector<float> samples;
			m_pTerrainSystem->BuildPhysicsHeightSamples(samples);
			ASSERT(samples.size() == size_t(W) * size_t(H), "HeightField samples size mismatch.");

			const float spacingX = m_pTerrainSystem->GetWorldSpacingX();
			const float spacingZ = m_pTerrainSystem->GetWorldSpacingZ();

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
			const StaticMeshRenderData* pTreeMeshes[] =
			{
				&(m_pRenderer->CreateStaticMeshRenderData(m_pAssetManager->RegisterAsset<AssimpAsset>("C:/Dev/ShizenEngine/Assets/Tree/beech_tree/scene.gltf"))),
			};

			constexpr uint TREE_MESH_COUNT = sizeof(pTreeMeshes) / sizeof(pTreeMeshes[0]);

			constexpr float4 SPAWN_RANGE = { -500.0f, -500.0f, 500.0f, 500.0f };
			constexpr uint  NUM_TREES = 10000;

			float yOffset = 0.0f;
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
				const float y = m_pTerrainSystem->SampleWorldHeight(x, z) + yOffset;

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
				mr.MeshRef = {};
				mr.bCastShadow = true;

				mr.RenderObjectHandle = m_pRenderScene->AddObject(
					*pTreeMeshes[meshIdx],
					Matrix4x4::TRS(tr.Position, tr.Rotation, tr.Scale),
					mr.bCastShadow);
				e.set<CMeshRenderer>(mr);
			}
		}

		// ------------------------------------------------------------
		// Helmets: render + dynamic physics box collider
		// ------------------------------------------------------------
		{
			AssetRef<StaticMesh> helmetRef = m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/DamagedHelmet.shzmesh.json");
			const StaticMeshRenderData& helmetMeshRD = m_pRenderer->CreateStaticMeshRenderData(helmetRef);
			//StaticMesh& helmetMesh = *m_pAssetManager->LoadBlocking(helmetRef);
			//auto uniform01 = [](StaticMesh& mesh)
			//{
			//	mesh.RecomputeBounds();
			//	const Box& b = mesh.GetBounds();
			//	float yScale01 = 1.0f / (b.Max.y - b.Min.y);
			//	mesh.ApplyUniformScale(yScale01);
			//	mesh.MoveBottomToOrigin(true);
			//};
			//uniform01(helmetMesh);
			//const StaticMeshRenderData& helmetMeshRD = m_pRenderer->CreateStaticMeshRenderData(helmetMesh);

			constexpr uint32 kHelmetCount = 100;
			constexpr float  kMinY = 10.0f;
			constexpr float  kMaxY = 20.0f;

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

				const float y = m_pTerrainSystem->SampleWorldHeight(x, z) + distY(rng);
				const float yaw = distYaw(rng);

				flecs::entity e = ecs.entity();
				e.set<CName>({ "Helmet" });

				CTransform tr = {};
				tr.Position = { x, y, z };
				tr.Rotation = { 0.0f, yaw, 0.0f };
				tr.Scale = { 1.0f, 1.0f, 1.0f };
				e.set<CTransform>(tr);

				CMeshRenderer mr = {};
				mr.MeshRef = helmetRef;
				mr.bCastShadow = true;
				mr.RenderObjectHandle = m_pRenderScene->AddObject(
					helmetMeshRD,
					Matrix4x4::TRS(tr.Position, tr.Rotation, tr.Scale),
					true);
				e.set<CMeshRenderer>(mr);

				CBoxCollider box = {};
				box.Box = helmetMeshRD.LocalBounds;
				box.bIsSensor = false;
				e.set<CBoxCollider>(box);

				CRigidbody rb = {};
				rb.BodyType = ERigidbodyType::Dynamic;
				rb.Layer = 1; // Moving
				rb.Mass = 1.0f;
				rb.LinearDamping = 0.0f;
				rb.AngularDamping = 0.0f;
				rb.bEnableGravity = true;
				rb.bAllowSleeping = false;
				rb.bStartActive = true;
				e.set<CRigidbody>(rb);
			}
		}
	}

} // namespace shz
