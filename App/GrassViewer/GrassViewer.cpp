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
#include "Engine/RuntimeData/Public/TerrainHeightFieldImporter.h"
#include "Engine/RuntimeData/Public/TerrainMeshBuilder.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/RenderPass/Public/ShadowRenderPass.h"
#include "Engine/RenderPass/Public/GBufferRenderPass.h"
#include "Engine/RenderPass/Public/LightingRenderPass.h"
#include "Engine/RenderPass/Public/PostRenderPass.h"
#include "Engine/RenderPass/Public/GrassRenderPass.h"
#include "Engine/RenderPass/Public/GrassInteractionPass.h"
#include "Engine/RenderPass/Public/GrassBuildInstancesPass.h"

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

			// Lighting
			{
				TextureDesc td = {};
				td.Name = "Lighting";
				td.Type = RESOURCE_DIM_TEX_2D;
				td.Width = m_Viewport.Width;
				td.Height = m_Viewport.Height;
				td.MipLevels = 1;
				td.Format = m_pSwapChain->GetDesc().ColorBufferFormat;
				td.SampleCount = 1;
				td.Usage = USAGE_DEFAULT;
				td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

				m_pRenderer->AddTexture(STRING_HASH("Lighting"), td);
			}

			// Grass
			{
				constexpr uint32 MAX_NUM_GRASS_INSTANCES = 1u << 24;
				constexpr uint32 INTERACTION_FIELD_SIZE = 1025;
				constexpr uint32 MAX_NUM_INTERACTION_STAMPS = 256;
				// GrassInstanceBuffer
				{
					BufferDesc bd = {};
					bd.Name = "GrassInstanceBuffer";
					bd.Usage = USAGE_DEFAULT;
					bd.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
					bd.Mode = BUFFER_MODE_STRUCTURED;
					bd.ElementByteStride = sizeof(hlsl::GrassInstance);
					bd.Size = uint64{ MAX_NUM_GRASS_INSTANCES } *uint64{ sizeof(hlsl::GrassInstance) };

					m_pRenderer->AddBuffer(STRING_HASH("GrassInstanceBuffer"), bd);
				}

				// Indirect args (RAW 20 bytes)
				{
					BufferDesc bd = {};
					bd.Name = "GrassIndirectArgs";
					bd.Usage = USAGE_DEFAULT;
					bd.BindFlags = BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS;
					bd.Mode = BUFFER_MODE_RAW;
					bd.Size = 20;

					m_pRenderer->AddBuffer(STRING_HASH("GrassIndirectArgs"), bd);
				}

				// Counter (RAW 4 bytes)
				{
					BufferDesc bd = {};
					bd.Name = "GrassCounter";
					bd.Usage = USAGE_DEFAULT;
					bd.BindFlags = BIND_UNORDERED_ACCESS;
					bd.Mode = BUFFER_MODE_RAW;
					bd.Size = 4;

					m_pRenderer->AddBuffer(STRING_HASH("GrassCounter"), bd);
				}

				// GrassGenConstantsCB (CS)
				{
					BufferDesc bd = {};
					bd.Name = "GrassGenConstantsCB";
					bd.Usage = USAGE_DYNAMIC;
					bd.BindFlags = BIND_UNIFORM_BUFFER;
					bd.CPUAccessFlags = CPU_ACCESS_WRITE;
					bd.Size = sizeof(hlsl::GrassGenConstants);

					m_pRenderer->AddBuffer(STRING_HASH("GrassGenConstantsCB"), bd);
				}

				// GrassRenderConstantsCB (VS/PS)
				{
					BufferDesc bd = {};
					bd.Name = "GrassRenderConstantsCB";
					bd.Usage = USAGE_DYNAMIC;
					bd.BindFlags = BIND_UNIFORM_BUFFER;
					bd.CPUAccessFlags = CPU_ACCESS_WRITE;
					bd.Size = sizeof(hlsl::GrassRenderConstants);

					m_pRenderer->AddBuffer(STRING_HASH("GrassRenderConstantsCB"), bd);
				}

				// Interaction field texture (R16_FLOAT SRV/UAV)
				{
					TextureDesc td = {};
					td.Name = "InteractionField";
					td.Type = RESOURCE_DIM_TEX_2D;
					td.Width = INTERACTION_FIELD_SIZE;
					td.Height = INTERACTION_FIELD_SIZE;
					td.Format = TEX_FORMAT_R16_FLOAT;
					td.MipLevels = 1;
					td.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
					td.Usage = USAGE_DEFAULT;

					m_pRenderer->AddTexture(STRING_HASH("InteractionField"), td);
				}

				// Interaction stamps (Structured, dynamic CPU write)
				{
					BufferDesc bd = {};
					bd.Name = "InteractionStampBuffer";
					bd.Usage = USAGE_DYNAMIC;
					bd.BindFlags = BIND_SHADER_RESOURCE;
					bd.Mode = BUFFER_MODE_STRUCTURED;
					bd.ElementByteStride = sizeof(hlsl::InteractionStamp);
					bd.Size = uint64(MAX_NUM_INTERACTION_STAMPS) * uint64(sizeof(hlsl::InteractionStamp));
					bd.CPUAccessFlags = CPU_ACCESS_WRITE;

					m_pRenderer->AddBuffer(STRING_HASH("InteractionStampBuffer"), bd);
				}

				// Interaction constants
				{
					BufferDesc bd = {};
					bd.Name = "InteractionConstantsCB";
					bd.Usage = USAGE_DYNAMIC;
					bd.BindFlags = BIND_UNIFORM_BUFFER;
					bd.CPUAccessFlags = CPU_ACCESS_WRITE;
					bd.Size = uint64(sizeof(hlsl::InteractionConstants));

					m_pRenderer->AddBuffer(STRING_HASH("InteractionConstantsCB"), bd);
				}

				// Density texture for grass placement
				{
					AssetRef<Texture> perlinRef = m_pAssetManager->RegisterAsset<Texture>("C:/Dev/ShizenEngine/Assets/Terrain/RollingHills/Worley.jpg");
					AssetPtr<Texture> perlinPtr = m_pAssetManager->LoadBlocking(perlinRef);
					Texture perlin = Texture::ConvertGrayScale(*perlinPtr);

					RefCntAutoPtr<ITexture> perlinTex;

					TextureDesc desc = {};
					desc.Name = "GrassDensityField";
					desc.Type = RESOURCE_DIM_TEX_2D;
					desc.Width = perlin.GetWidth();
					desc.Height = perlin.GetHeight();
					desc.MipLevels = 1;
					desc.ArraySize = 1;
					desc.Format = TEX_FORMAT_R8_UNORM;
					desc.Usage = USAGE_DEFAULT;
					desc.BindFlags = BIND_SHADER_RESOURCE;

					TextureSubResData subres = {};
					subres.pData = perlin.GetData();
					subres.Stride = static_cast<uint64>(perlin.GetWidth()) * GetTextureFormatAttribs(desc.Format).GetElementSize();
					subres.DepthStride = 0;
					TextureData initData = {};
					initData.pSubResources = &subres;
					initData.NumSubresources = 1;

					m_pRenderer->AddTexture(STRING_HASH("GrassDensityField"), desc, &initData);
				}
			}
		}

		// -----------------------------------------------------------------
		// Create render passes
		// -----------------------------------------------------------------
		{
			m_pRenderer->AddPass(std::make_unique<ShadowRenderPass>());
			m_pRenderer->AddPass(std::make_unique<GBufferRenderPass>());
			m_pRenderer->AddPass(std::make_unique<LightingRenderPass>());
			m_pRenderer->AddPass(std::make_unique<GrassInteractionPass>());
			m_pRenderer->AddPass(std::make_unique<GrassBuildInstancesPass>());
			m_pRenderer->AddPass(std::make_unique<GrassRenderPass>());
			m_pRenderer->AddPass(std::make_unique<PostRenderPass>());
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
								stamp.Radius = 1.0f;
								stamp.FalloffPower = 1.0f;
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


		{
			hlsl::GrassGenConstants gen = {};
			gen.HeightScale = 100.0f;
			gen.HeightOffset = 0.0f;
			gen.YOffset = 0.0f;
			gen._padT0 = 0.0f;

			gen.HFWidth = 1025;
			gen.HFHeight = 1025;
			gen.CenterXZ = 1;
			gen._padT1 = 0;

			gen.SpacingX = 1.0f;
			gen.SpacingZ = 1.0f;
			gen._padT2 = 0.0f;
			gen._padT3 = 0.0f;

			// --- Chunk placement ---
			gen.ChunkSize = 4.0f;
			gen.ChunkHalfExtent = 32;
			gen.SamplesPerChunk = 2048;
			gen.Jitter = 0.95f;

			gen.MinScale = 5.7f;
			gen.MaxScale = 11.1f;
			gen.SpawnProb = 0.75f;
			gen.SpawnRadius = 1000.0f;

			gen.BendStrengthMin = 0.95f;
			gen.BendStrengthMax = 1.55f;
			gen.SeedSalt = 0xA53A9E37u;
			gen._padT4 = 0;

			gen.DensityTiling = 0.02f;
			gen.DensityContrast = 0.28f;
			gen.DensityPow = 0.70f;
			gen._padD0 = 0.0f;

			gen.SlopeToDensity = 0.15f;

			gen.HeightMinN = 0.00f;
			gen.HeightMaxN = 1.00f;
			gen.HeightFadeN = 0.03f;

			m_pRenderer->UpdateBuffer<hlsl::GrassGenConstants>(STRING_HASH("GrassGenConstantsCB"), gen);
		}

		{
			hlsl::GrassRenderConstants ren = {};
			ren.BaseColorFactor = float4(150.f, 200.f, 100.f, 255.f) / 255.f;
			ren.Tint = float4{ 1.05f, 1.00f, 0.95f, 1.0f };

			ren.AlphaCut = 0.5f;

			ren.Ambient = 0.30f;
			ren.ShadowStregth = 0.18f;
			ren.DirectLightStrength = 0.22f;

			ren.WindDirXZ = float2{ 0.80f, 0.60f }.Normalized();
			ren.WindStrength = 1.15f;
			ren.WindSpeed = 1.75f;

			ren.WindFreq = 0.155f;
			ren.WindGust = 0.42f;
			ren.MaxBendAngle = 1.50f;
			ren._pad1 = 0.0f;

			ren.InteractionBendAngle = 1.0f;
			ren.InteractionSink = 0.05f;
			ren.InteractionWindFade = 0.95f;

			m_pRenderer->UpdateBuffer<hlsl::GrassRenderConstants>(STRING_HASH("GrassRenderConstantsCB"), ren);
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

		// Load terrain (RenderScene는 기존대로 유지)
		const std::string heightPath = "C:/Dev/ShizenEngine/Assets/Terrain/RollingHills/RollingHillsHeightMap.png";

		AssetRef<TerrainHeightField> terrainRef = m_pAssetManager->RegisterAsset<TerrainHeightField>(heightPath);
		AssetPtr<TerrainHeightField> terrainPtr = m_pAssetManager->LoadBlocking<TerrainHeightField>(terrainRef);
		ASSERT(terrainPtr && terrainPtr->IsValid(), "Failed to load terrain height field.");

		// Build terrain mesh + set RenderScene terrain
		{
			StaticMesh terrainMesh;

			MaterialId tmId = MaterialManager::GetInstance()->CreateMaterial("TerrainMaterial", "DefaultLit");
			Material& tm = MaterialManager::GetInstance()->GetMaterial(tmId);
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
			meshBuilder.BuildStaticMesh(&terrainMesh, *terrainPtr, tmId, buildSettings);

			m_pRenderScene->SetTerrain(
				m_pRenderer->CreateTextureRenderDataFromHeightField(*terrainPtr),
				m_pRenderer->CreateStaticMeshRenderData(terrainMesh));
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
			AssetRef<StaticMesh> treeAssets[] =
			{
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree1.shzmesh.json"),
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree2.shzmesh.json"),
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree3.shzmesh.json"),
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree4.shzmesh.json"),
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/Tree5.shzmesh.json"),
			};

			const StaticMeshRenderData* pTreeMeshes[] =
			{
				&(m_pRenderer->CreateStaticMeshRenderData(treeAssets[0])),
				&(m_pRenderer->CreateStaticMeshRenderData(treeAssets[1])),
				&(m_pRenderer->CreateStaticMeshRenderData(treeAssets[2])),
				&(m_pRenderer->CreateStaticMeshRenderData(treeAssets[3])),
				&(m_pRenderer->CreateStaticMeshRenderData(treeAssets[4])),
			};

			constexpr uint TREE_MESH_COUNT = sizeof(pTreeMeshes) / sizeof(pTreeMeshes[0]);

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
			AssetRef<StaticMesh> helmetRef =
				m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/DamagedHelmet.shzmesh.json");

			const StaticMeshRenderData& helmetMeshRD = m_pRenderer->CreateStaticMeshRenderData(helmetRef);

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
