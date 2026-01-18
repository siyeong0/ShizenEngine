#include <array>
#include <algorithm>

#include "Tutorial08_DeferredRendering.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Tools/Image/Public/TextureUtilities.h"
#include "Engine/GraphicsUtils/Public/ColorConversion.h"

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/ImGuiUtils.hpp"
#include "Engine/Core/Common/Public/FastRand.hpp"

namespace shz
{
	struct Vertex
	{
		float3 pos;
		float2 uv;
		float3 normal;
	};

	SampleBase* CreateSample()
	{
		return new Tutorial08_DeferredRendering();
	}

	void Tutorial08_DeferredRendering::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& attribs)
	{
		SampleBase::ModifyEngineInitInfo(attribs);

		// 이 샘플은 우리가 별도의 depth를 만들 것이므로 swapchain depth는 필요 없음
		attribs.SCDesc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;
	}

	void Tutorial08_DeferredRendering::Initialize(const SampleInitInfo& InitInfo)
	{
		SampleBase::Initialize(InitInfo);

		// ---------------------------------------------------------------------
		// Constant buffers
		// ---------------------------------------------------------------------
		CreateUniformBuffer(m_pDevice, sizeof(HLSL::ShaderConstants), "Shader constants CB", &m_pShaderConstantsCB);
		CreateUniformBuffer(m_pDevice, sizeof(HLSL::ShadowConstants), "Shadow constants CB", &m_pShadowConstantsCB);
		CreateUniformBuffer(m_pDevice, sizeof(HLSL::ObjectConstants), "Object constants CB", &m_pObjectConstantsCB);

		// ---------------------------------------------------------------------
		// Geometry / texture
		// ---------------------------------------------------------------------

		// Cube
		{
			//      (-1,+1,+1)________________(+1,+1,+1)
			//               /|              /|
			//              / |             / |
			//             /  |            /  |
			//            /   |           /   |
			//(-1,-1,+1) /____|__________/(+1,-1,+1)
			//           |    |__________|____|
			//           |   /(-1,+1,-1) |    /(+1,+1,-1)
			//           |  /            |   /
			//           | /             |  /
			//           |/              | /
			//           /_______________|/
			//        (-1,-1,-1)       (+1,-1,-1)
			//

			constexpr Vertex CubeVerts[] =
			{
				// z = -1 (back)
				{ {-1,-1,-1}, {0,1}, {0,0,-1} },
				{ {-1,+1,-1}, {0,0}, {0,0,-1} },
				{ {+1,+1,-1}, {1,0}, {0,0,-1} },
				{ {+1,-1,-1}, {1,1}, {0,0,-1} },

				// y = -1 (bottom)
				{ {-1,-1,-1}, {0,1}, {0,-1,0} },
				{ {-1,-1,+1}, {0,0}, {0,-1,0} },
				{ {+1,-1,+1}, {1,0}, {0,-1,0} },
				{ {+1,-1,-1}, {1,1}, {0,-1,0} },

				// x = +1 (right)
				{ {+1,-1,-1}, {0,1}, {+1,0,0} },
				{ {+1,-1,+1}, {1,1}, {+1,0,0} },
				{ {+1,+1,+1}, {1,0}, {+1,0,0} },
				{ {+1,+1,-1}, {0,0}, {+1,0,0} },

				// y = +1 (top)
				{ {+1,+1,-1}, {0,1}, {0,+1,0} },
				{ {+1,+1,+1}, {0,0}, {0,+1,0} },
				{ {-1,+1,+1}, {1,0}, {0,+1,0} },
				{ {-1,+1,-1}, {1,1}, {0,+1,0} },

				// x = -1 (left)
				{ {-1,+1,-1}, {1,0}, {-1,0,0} },
				{ {-1,+1,+1}, {0,0}, {-1,0,0} },
				{ {-1,-1,+1}, {0,1}, {-1,0,0} },
				{ {-1,-1,-1}, {1,1}, {-1,0,0} },

				// z = +1 (front)
				{ {-1,-1,+1}, {1,1}, {0,0,+1} },
				{ {+1,-1,+1}, {0,1}, {0,0,+1} },
				{ {+1,+1,+1}, {0,0}, {0,0,+1} },
				{ {-1,+1,+1}, {1,0}, {0,0,+1} },
			};

			BufferDesc VertBuffDesc;
			VertBuffDesc.Name = "Cube vertex buffer";
			VertBuffDesc.Usage = USAGE_IMMUTABLE;
			VertBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
			VertBuffDesc.Size = sizeof(CubeVerts);
			BufferData VBData;
			VBData.pData = CubeVerts;
			VBData.DataSize = sizeof(CubeVerts);
			m_pDevice->CreateBuffer(VertBuffDesc, &VBData, &m_CubeVertexBuffer);

			constexpr uint32 Indices[] =
			{
				2,0,1,    2,3,0,
				4,6,5,    4,7,6,
				8,10,9,   8,11,10,
				12,14,13, 12,15,14,
				16,18,17, 16,19,18,
				20,21,22, 20,22,23
			};


			BufferDesc IndBuffDesc;
			IndBuffDesc.Name = "Cube index buffer";
			IndBuffDesc.Usage = USAGE_IMMUTABLE;
			IndBuffDesc.BindFlags = BIND_INDEX_BUFFER;
			IndBuffDesc.Size = sizeof(Indices);
			BufferData IBData;
			IBData.pData = Indices;
			IBData.DataSize = sizeof(Indices);
			m_pDevice->CreateBuffer(IndBuffDesc, &IBData, &m_CubeIndexBuffer);

			TextureLoadInfo loadInfo;
			loadInfo.IsSRGB = true;
			RefCntAutoPtr<ITexture> pTex;
			CreateTextureFromFile("Assets/pearl_abyss_logo.png", loadInfo, m_pDevice, &pTex);
			m_CubeTextureSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		}
		// Plane
		{
			//  (-1,0,+1) -------- (+1,0,+1)
			//       |                |
			//       |                |
			//  (-1,0,-1) -------- (+1,0,-1)

			constexpr Vertex PlaneVerts[] =
			{
				{ {-1.f,0.f,-1.f}, {0.f,1.f}, {0.f,1.f,0.f} },
				{ {-1.f,0.f,+1.f}, {0.f,0.f}, {0.f,1.f,0.f} },
				{ {+1.f,0.f,+1.f}, {1.f,0.f}, {0.f,1.f,0.f} },
				{ {+1.f,0.f,-1.f}, {1.f,1.f}, {0.f,1.f,0.f} },
			};

			BufferDesc VertBuffDesc;
			VertBuffDesc.Name = "Plane vertex buffer";
			VertBuffDesc.Usage = USAGE_IMMUTABLE;
			VertBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
			VertBuffDesc.Size = sizeof(PlaneVerts);

			BufferData VBData;
			VBData.pData = PlaneVerts;
			VBData.DataSize = sizeof(PlaneVerts);

			m_pDevice->CreateBuffer(VertBuffDesc, &VBData, &m_PlaneVertexBuffer);


			constexpr uint32 Indices[] =
			{
				2, 0, 1,
				2, 3, 0
			};

			BufferDesc IndBuffDesc;
			IndBuffDesc.Name = "Plane index buffer";
			IndBuffDesc.Usage = USAGE_IMMUTABLE;
			IndBuffDesc.BindFlags = BIND_INDEX_BUFFER;
			IndBuffDesc.Size = sizeof(Indices);

			BufferData IBData;
			IBData.pData = Indices;
			IBData.DataSize = sizeof(Indices);

			m_pDevice->CreateBuffer(IndBuffDesc, &IBData, &m_PlaneIndexBuffer);


			TextureLoadInfo LoadInfo;
			LoadInfo.IsSRGB = true;
			RefCntAutoPtr<ITexture> pTex;
			CreateTextureFromFile("Assets/floor.dds", LoadInfo, m_pDevice, &pTex);
			m_PlaneTextureSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		}

		// ---------------------------------------------------------------------
		// Lights
		// ---------------------------------------------------------------------
		initLights();
		createLightsBuffer();

		// ---------------------------------------------------------------------
		// Create shader source stream factory
		// ---------------------------------------------------------------------
		RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
		m_pEngineFactory->CreateDefaultShaderSourceStreamFactory("Assets", &pShaderSourceFactory);

		// ---------------------------------------------------------------------
		// Create passes (RenderPass + Textures)
		// ---------------------------------------------------------------------
		createShadowPass();
		createGBufferPass();
		createLightingPass();
		createPostPass();

		// ---------------------------------------------------------------------
		// Create PSOs
		// ---------------------------------------------------------------------
		createShadowPSO(pShaderSourceFactory);
		createGBufferPSO(pShaderSourceFactory);
		createLightingPSO(pShaderSourceFactory);
		createPostPSO(pShaderSourceFactory);

		// ---------------------------------------------------------------------
		// Create framebuffers
		// ---------------------------------------------------------------------
		m_pShadowFB = createShadowFramebuffer();
		m_pGBufferFB = createGBufferFramebuffer();
		m_pLightingFB = createLightingFramebuffer();

		// ---------------------------------------------------------------------
		// SRB bindings that depend on created textures
		// ---------------------------------------------------------------------
		{
			// Shadow SRB
			m_pShadowPSO->CreateShaderResourceBinding(&m_pShadowSRB, true);
			ASSERT_EXPR(m_pShadowSRB != nullptr);

			if (auto* pObj = m_pShadowSRB->GetVariableByName(SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS"))
				pObj->Set(m_pObjectConstantsCB);

			// GBuffer SRB
			// Cube
			m_pGBufferPSO->CreateShaderResourceBinding(&m_pGBufferSRB_Cube, true);
			ASSERT_EXPR(m_pGBufferSRB_Cube);

			if (auto* pVar = m_pGBufferSRB_Cube->GetVariableByName(SHADER_TYPE_PIXEL, "g_BaseColorTex"))
				pVar->Set(m_CubeTextureSRV);
			if (auto* pObj = m_pGBufferSRB_Cube->GetVariableByName(SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS"))
				pObj->Set(m_pObjectConstantsCB);

			// Plane 
			m_pGBufferPSO->CreateShaderResourceBinding(&m_pGBufferSRB_Plane, true);
			ASSERT_EXPR(m_pGBufferSRB_Plane);

			if (auto* pVar = m_pGBufferSRB_Plane->GetVariableByName(SHADER_TYPE_PIXEL, "g_BaseColorTex"))
				pVar->Set(m_PlaneTextureSRV);

			if (auto* pObj = m_pGBufferSRB_Plane->GetVariableByName(SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS"))
				pObj->Set(m_pObjectConstantsCB);

			// Lighting SRB
			m_pLightingPSO->CreateShaderResourceBinding(&m_pLightingSRB, true);
			ASSERT_EXPR(m_pLightingSRB != nullptr);

			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_Albedo"))
				pVar->Set(m_GBuffer.pAlbedo->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_Normal"))
				pVar->Set(m_GBuffer.pNormal->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_Material"))
				pVar->Set(m_GBuffer.pMaterial->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_DepthZ"))
				pVar->Set(m_GBuffer.pDepthZ->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap"))
				pVar->Set(m_Shadow.pShadowSRV);

			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Lights"))
				pVar->Set(m_pLightsSRV);

			// Post SRB
			m_pPostPSO->CreateShaderResourceBinding(&m_pPostSRB, true);
			ASSERT_EXPR(m_pPostSRB != nullptr);

			if (auto* pVar = m_pPostSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_LightingTex"))
				pVar->Set(m_Post.pLightingHDR->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		}

		// ---------------------------------------------------------------------
		// Transition resources (render pass 내부에서는 TRANSITION 금지 -> VERIFY로 씀)
		// ---------------------------------------------------------------------
		StateTransitionDesc barriers[] =
		{
			{m_pShaderConstantsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
			{m_pShadowConstantsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
			{m_pObjectConstantsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},

			{m_CubeVertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
			{m_CubeIndexBuffer,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER,  STATE_TRANSITION_FLAG_UPDATE_STATE},
			{m_CubeTextureSRV->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},

			{m_PlaneVertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
			{m_PlaneIndexBuffer,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER,  STATE_TRANSITION_FLAG_UPDATE_STATE},
			{m_PlaneTextureSRV->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},

			// Lights buffer (SRV)
			{m_pLightsBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},

			// Targets: 초기 상태는 RenderPass Begin에서 TRANSITION 모드로 맞춰짐
		};

		m_pImmediateContext->TransitionResourceStates(_countof(barriers), barriers);
	}

	void Tutorial08_DeferredRendering::Render()
	{
		const SwapChainDesc& swapChainDesc = m_pSwapChain->GetDesc();

		// ---------------------------------------------------------------------
		// Update constant buffers
		// ---------------------------------------------------------------------
		{
			MapHelper<HLSL::ShaderConstants> cb(m_pImmediateContext, m_pShaderConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD);
			cb->ViewProj = m_CameraViewProjMatrix;
			cb->ViewProjInv = m_CameraViewProjInvMatrix;
			cb->ViewportSize = float4{
				static_cast<float>(swapChainDesc.Width),
				static_cast<float>(swapChainDesc.Height),
				1.f / static_cast<float>(swapChainDesc.Width),
				1.f / static_cast<float>(swapChainDesc.Height)
			};
			cb->CameraPosWS = float3{ 0.0f, -5.0f, 25.0f };
			cb->LightsCount = m_LightsCount;
			cb->ShowLightVolumes = 0;
			cb->Padding0 = cb->Padding1 = 0;
		}

		{
			// 전역(그림자) 라이트: 방향광
			const float3 dirLightDirWS = Vector3::Normalize(float3{ 0.f, -1.f, 0.f }); // 빛 진행 방향(위->아래)

			// 씬 중심/범위(현재 배치 기준 대충)
			const float3 sceneCenter = float3{ 0.f, 0.f, 0.f };
			const float  dist = 60.f;

			const float3 lightPos = sceneCenter - dirLightDirWS * dist;
			const float3 lightAt = sceneCenter;
			const float3 lightUp = float3{ 0.f, 1.f, 0.f };

			float4x4 lightView = float4x4::LookAtLH(lightPos, lightAt, lightUp);

			// 그리드(7x7, spacing 3, base -9) + plane scale 100 고려해서 넉넉히
			float4x4 lightProj = float4x4::OrthoOffCenter(-50.f, 50.f, -50.f, 50.f, 0.1f, 200.f);

			m_LightViewProj = lightView * lightProj;

			MapHelper<HLSL::ShadowConstants> scb(m_pImmediateContext, m_pShadowConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD);
			scb->LightViewProj = m_LightViewProj;
			scb->ShadowMapTexelSize = float2{ 1.f / float(m_Shadow.Width), 1.f / float(m_Shadow.Height) };
			scb->ShadowBias = m_Shadow.Bias;
			scb->ShadowStrength = m_Shadow.Strength;
			scb->LightDirWS = dirLightDirWS; // 빛 진행 방향
		}

		// ---------------------------------------------------------------------
		// Update lights buffer (StructuredBuffer)
		// ---------------------------------------------------------------------
		{
			MapHelper<HLSL::LightAttribs> lights(m_pImmediateContext, m_pLightsBuffer, MAP_WRITE, MAP_FLAG_DISCARD);
			memcpy(lights, m_Lights.data(), m_Lights.size() * sizeof(m_Lights[0]));
		}

		// ---------------------------------------------------------------------
		// PASS 0: Shadow Map
		// ---------------------------------------------------------------------
		{
			BeginRenderPassAttribs rp;
			rp.pRenderPass = m_pShadowRenderPass;
			rp.pFramebuffer = m_pShadowFB;

			OptimizedClearValue clear;
			clear.DepthStencil.Depth = 1.f;
			clear.DepthStencil.Stencil = 0;

			rp.pClearValues = &clear;
			rp.ClearValueCount = 1;
			rp.StateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

			m_pImmediateContext->BeginRenderPass(rp);
			{
				drawScene_Shadow();
			}
			m_pImmediateContext->EndRenderPass();
		}

		// ---------------------------------------------------------------------
		// PASS 1: GBuffer
		// ---------------------------------------------------------------------
		{
			BeginRenderPassAttribs rp;
			rp.pRenderPass = m_pGBufferRenderPass;
			rp.pFramebuffer = m_pGBufferFB;

			OptimizedClearValue clears[5] = {};

			// RT0 Albedo
			clears[0].Color[0] = 0.f; clears[0].Color[1] = 0.f; clears[0].Color[2] = 0.f; clears[0].Color[3] = 1.f;
			// RT1 Normal (encoded)
			clears[1].Color[0] = 0.5f; clears[1].Color[1] = 0.5f; clears[1].Color[2] = 1.f; clears[1].Color[3] = 1.f;
			// RT2 Material
			clears[2].Color[0] = 0.6f; clears[2].Color[1] = 0.0f; clears[2].Color[2] = 1.0f; clears[2].Color[3] = 1.f;
			// RT3 DepthZ
			clears[3].Color[0] = 1.f; clears[3].Color[1] = 1.f; clears[3].Color[2] = 1.f; clears[3].Color[3] = 1.f;
			// Depth
			clears[4].DepthStencil.Depth = 1.f;
			clears[4].DepthStencil.Stencil = 0;

			rp.pClearValues = clears;
			rp.ClearValueCount = _countof(clears);
			rp.StateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

			m_pImmediateContext->BeginRenderPass(rp);
			{
				drawScene_GBuffer();
			}
			m_pImmediateContext->EndRenderPass();
		}

		// ---------------------------------------------------------------------
		// PASS 2: Lighting (GBuffer + Shadow + Lights -> LightingHDR)
		// ---------------------------------------------------------------------
		{
			BeginRenderPassAttribs rp;
			rp.pRenderPass = m_pLightingRenderPass;
			rp.pFramebuffer = m_pLightingFB;

			OptimizedClearValue clear;
			clear.Color[0] = 0.f; clear.Color[1] = 0.f; clear.Color[2] = 0.f; clear.Color[3] = 1.f;

			rp.pClearValues = &clear;
			rp.ClearValueCount = 1;
			rp.StateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

			m_pImmediateContext->BeginRenderPass(rp);
			{
				drawFullscreen_Lighting();
			}
			m_pImmediateContext->EndRenderPass();
		}

		// ---------------------------------------------------------------------
		// PASS 3: Post (LightingHDR -> BackBuffer)
		// ---------------------------------------------------------------------
		{
			ITextureView* pBackBufferRTV = m_pSwapChain->GetCurrentBackBufferRTV();
			RefCntAutoPtr<IFramebuffer> postFB = createPostFramebuffer(pBackBufferRTV);

			BeginRenderPassAttribs rp;
			rp.pRenderPass = m_pPostRenderPass;
			rp.pFramebuffer = postFB;

			OptimizedClearValue clear;
			clear.Color[0] = 0.f; clear.Color[1] = 0.f; clear.Color[2] = 0.f; clear.Color[3] = 1.f;

			rp.pClearValues = &clear;
			rp.ClearValueCount = 1;
			rp.StateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

			m_pImmediateContext->BeginRenderPass(rp);
			{
				drawFullscreen_Post();
			}
			m_pImmediateContext->EndRenderPass();
		}
	}

	void Tutorial08_DeferredRendering::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
	{
		SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);

		if (m_bAnimateLights)
			updateLights(static_cast<float>(ElapsedTime));

		float4x4 view = float4x4::Translation({ 0.0f, -5.0f, 25.0f });
		float4x4 srfPreTransform = GetSurfacePretransformMatrix(float3{ 0, 0, 1 });
		float4x4 proj = GetAdjustedProjectionMatrix(PI / 4.0f, 0.1f, 100.f);

		m_CameraViewProjMatrix = view * srfPreTransform * proj;
		m_CameraViewProjInvMatrix = m_CameraViewProjMatrix.Inversed();
	}

	void Tutorial08_DeferredRendering::UpdateUI()
	{
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (ImGui::InputInt("Lights count (<=1024)", &m_LightsCount, 32, 128, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				m_LightsCount = std::max(m_LightsCount, 1);
				m_LightsCount = std::min(m_LightsCount, 1024); // Lighting.psh 내부 루프가 1024 고정
				initLights();
				createLightsBuffer();
				// Lighting SRB
				m_pLightingSRB.Release();
				m_pLightingPSO->CreateShaderResourceBinding(&m_pLightingSRB, true);
				ASSERT_EXPR(m_pLightingSRB != nullptr);

				if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_Albedo"))
					pVar->Set(m_GBuffer.pAlbedo->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
				if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_Normal"))
					pVar->Set(m_GBuffer.pNormal->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
				if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_Material"))
					pVar->Set(m_GBuffer.pMaterial->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
				if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_DepthZ"))
					pVar->Set(m_GBuffer.pDepthZ->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

				if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap"))
					pVar->Set(m_Shadow.pShadowSRV);

				if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Lights"))
					pVar->Set(m_pLightsSRV);
			}

			ImGui::Checkbox("Animate lights", &m_bAnimateLights);

			ImGui::Separator();
			ImGui::SliderFloat("Shadow Bias", &m_Shadow.Bias, 0.0f, 0.01f, "%.5f");
			ImGui::SliderFloat("Shadow Strength", &m_Shadow.Strength, 0.0f, 1.0f, "%.2f");
		}
		ImGui::End();
	}

	void Tutorial08_DeferredRendering::ReleaseSwapChainBuffers()
	{
		m_PostFBCache.clear();
	}

	void Tutorial08_DeferredRendering::WindowResize(uint32 Width, uint32 Height)
	{
		releaseWindowResources();

		m_pGBufferFB = createGBufferFramebuffer();
		m_pLightingFB = createLightingFramebuffer();

		{
			ASSERT(m_pLightingSRB == nullptr, "m_pLightingSRB must be released.");
			m_pLightingPSO->CreateShaderResourceBinding(&m_pLightingSRB, true);
			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_Albedo"))
				pVar->Set(m_GBuffer.pAlbedo->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_Normal"))
				pVar->Set(m_GBuffer.pNormal->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_Material"))
				pVar->Set(m_GBuffer.pMaterial->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer_DepthZ"))
				pVar->Set(m_GBuffer.pDepthZ->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap"))
				pVar->Set(m_Shadow.pShadowMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

			if (auto* pVar = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Lights"))
				pVar->Set(m_pLightsSRV);
		}
		{
			ASSERT(m_pPostSRB == nullptr, "m_pPostSRB must be released.");
			m_pPostPSO->CreateShaderResourceBinding(&m_pPostSRB, true);
			if (auto* pVar = m_pPostSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_LightingTex"))
				pVar->Set(m_Post.pLightingHDR->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		}
	}

	// ============================================================================
	// Pass creation
	// ============================================================================

	void Tutorial08_DeferredRendering::createShadowPass()
	{
		// RenderPass: Depth-only
		RenderPassAttachmentDesc Attach;
		Attach.Format = TEX_FORMAT_D32_FLOAT;
		Attach.InitialState = RESOURCE_STATE_DEPTH_WRITE;
		Attach.FinalState = RESOURCE_STATE_SHADER_RESOURCE; // ShadowMap SRV로 읽을 것
		Attach.LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Attach.StoreOp = ATTACHMENT_STORE_OP_STORE;

		AttachmentReference DepthRef = { 0, RESOURCE_STATE_DEPTH_WRITE };

		SubpassDesc Subpass = {};
		Subpass.pDepthStencilAttachment = &DepthRef;

		RenderPassDesc RP = {};
		RP.Name = "Tutorial08 ShadowPass";
		RP.AttachmentCount = 1;
		RP.pAttachments = &Attach;
		RP.SubpassCount = 1;
		RP.pSubpasses = &Subpass;

		m_pDevice->CreateRenderPass(RP, &m_pShadowRenderPass);
		ASSERT_EXPR(m_pShadowRenderPass != nullptr);
	}

	void Tutorial08_DeferredRendering::createGBufferPass()
	{
		// 4 RT + depth
		RenderPassAttachmentDesc Att[5] = {};

		// RT0 Albedo
		Att[0].Format = TEX_FORMAT_RGBA8_UNORM;
		Att[0].InitialState = RESOURCE_STATE_RENDER_TARGET;
		Att[0].FinalState = RESOURCE_STATE_SHADER_RESOURCE;
		Att[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Att[0].StoreOp = ATTACHMENT_STORE_OP_STORE;

		// RT1 Normal
		Att[1].Format = TEX_FORMAT_RGBA16_FLOAT;
		Att[1].InitialState = RESOURCE_STATE_RENDER_TARGET;
		Att[1].FinalState = RESOURCE_STATE_SHADER_RESOURCE;
		Att[1].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Att[1].StoreOp = ATTACHMENT_STORE_OP_STORE;

		// RT2 Material
		Att[2].Format = TEX_FORMAT_RGBA8_UNORM;
		Att[2].InitialState = RESOURCE_STATE_RENDER_TARGET;
		Att[2].FinalState = RESOURCE_STATE_SHADER_RESOURCE;
		Att[2].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Att[2].StoreOp = ATTACHMENT_STORE_OP_STORE;

		// RT3 DepthZ (R32F 우선, fallback은 네 장치 포맷 지원 따라 바꿀 수 있음)
		Att[3].Format = TEX_FORMAT_R32_FLOAT;
		Att[3].InitialState = RESOURCE_STATE_RENDER_TARGET;
		Att[3].FinalState = RESOURCE_STATE_SHADER_RESOURCE;
		Att[3].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Att[3].StoreOp = ATTACHMENT_STORE_OP_STORE;

		// Depth
		Att[4].Format = TEX_FORMAT_D32_FLOAT;
		Att[4].InitialState = RESOURCE_STATE_DEPTH_WRITE;
		Att[4].FinalState = RESOURCE_STATE_DEPTH_WRITE;
		Att[4].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Att[4].StoreOp = ATTACHMENT_STORE_OP_STORE;

		AttachmentReference RTRefs[4] =
		{
			{0, RESOURCE_STATE_RENDER_TARGET},
			{1, RESOURCE_STATE_RENDER_TARGET},
			{2, RESOURCE_STATE_RENDER_TARGET},
			{3, RESOURCE_STATE_RENDER_TARGET},
		};
		AttachmentReference DepthRef = { 4, RESOURCE_STATE_DEPTH_WRITE };

		SubpassDesc Subpass = {};
		Subpass.RenderTargetAttachmentCount = _countof(RTRefs);
		Subpass.pRenderTargetAttachments = RTRefs;
		Subpass.pDepthStencilAttachment = &DepthRef;

		RenderPassDesc RP = {};
		RP.Name = "Tutorial08 GBufferPass";
		RP.AttachmentCount = _countof(Att);
		RP.pAttachments = Att;
		RP.SubpassCount = 1;
		RP.pSubpasses = &Subpass;

		m_pDevice->CreateRenderPass(RP, &m_pGBufferRenderPass);
		ASSERT_EXPR(m_pGBufferRenderPass != nullptr);
	}

	void Tutorial08_DeferredRendering::createLightingPass()
	{
		// RenderPass: 1 RT (HDR)
		RenderPassAttachmentDesc Att;
		Att.Format = TEX_FORMAT_RGBA16_FLOAT;
		Att.InitialState = RESOURCE_STATE_RENDER_TARGET;
		Att.FinalState = RESOURCE_STATE_SHADER_RESOURCE; // Post에서 SRV로 읽음
		Att.LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Att.StoreOp = ATTACHMENT_STORE_OP_STORE;

		AttachmentReference RTRef = { 0, RESOURCE_STATE_RENDER_TARGET };

		SubpassDesc Subpass = {};
		Subpass.RenderTargetAttachmentCount = 1;
		Subpass.pRenderTargetAttachments = &RTRef;

		RenderPassDesc RP = {};
		RP.Name = "Tutorial08 LightingPass";
		RP.AttachmentCount = 1;
		RP.pAttachments = &Att;
		RP.SubpassCount = 1;
		RP.pSubpasses = &Subpass;

		m_pDevice->CreateRenderPass(RP, &m_pLightingRenderPass);
		ASSERT_EXPR(m_pLightingRenderPass != nullptr);
	}

	void Tutorial08_DeferredRendering::createPostPass()
	{
		// RenderPass: backbuffer 1 RT
		RenderPassAttachmentDesc Att;
		Att.Format = m_pSwapChain->GetDesc().ColorBufferFormat;
		Att.InitialState = RESOURCE_STATE_RENDER_TARGET;
		Att.FinalState = RESOURCE_STATE_RENDER_TARGET;
		Att.LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Att.StoreOp = ATTACHMENT_STORE_OP_STORE;

		AttachmentReference RTRef = { 0, RESOURCE_STATE_RENDER_TARGET };

		SubpassDesc Subpass = {};
		Subpass.RenderTargetAttachmentCount = 1;
		Subpass.pRenderTargetAttachments = &RTRef;

		RenderPassDesc RP = {};
		RP.Name = "Tutorial08 PostPass";
		RP.AttachmentCount = 1;
		RP.pAttachments = &Att;
		RP.SubpassCount = 1;
		RP.pSubpasses = &Subpass;

		m_pDevice->CreateRenderPass(RP, &m_pPostRenderPass);
		ASSERT_EXPR(m_pPostRenderPass != nullptr);
	}

	// ============================================================================
	// Framebuffers
	// ============================================================================

	RefCntAutoPtr<IFramebuffer> Tutorial08_DeferredRendering::createShadowFramebuffer()
	{
		ASSERT(m_Shadow.pShadowMap == nullptr, "m_Shadow.pShadowMap must be null");

		TextureDesc td;
		td.Name = "ShadowMap";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = m_Shadow.Width;
		td.Height = m_Shadow.Height;
		td.MipLevels = 1;

		// typeless
		td.Format = TEX_FORMAT_R32_TYPELESS;
		td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

		// ClearValue는 DSV 포맷으로
		td.ClearValue.Format = TEX_FORMAT_D32_FLOAT;
		td.ClearValue.DepthStencil.Depth = 1.f;
		td.ClearValue.DepthStencil.Stencil = 0;

		m_pDevice->CreateTexture(td, nullptr, &m_Shadow.pShadowMap);
		ASSERT_EXPR(m_Shadow.pShadowMap != nullptr);

		// DSV view (D32_FLOAT)
		{
			TextureViewDesc dsvDesc;
			dsvDesc.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			dsvDesc.Format = TEX_FORMAT_D32_FLOAT;
			m_Shadow.pShadowMap->CreateView(dsvDesc, &m_Shadow.pShadowDSV);
			ASSERT_EXPR(m_Shadow.pShadowDSV != nullptr);
		}

		// SRV view (R32_FLOAT) - Lighting에서 Texture2D<float>로 읽음
		{
			TextureViewDesc srvDesc;
			srvDesc.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
			srvDesc.Format = TEX_FORMAT_R32_FLOAT;
			m_Shadow.pShadowMap->CreateView(srvDesc, &m_Shadow.pShadowSRV);
			ASSERT_EXPR(m_Shadow.pShadowSRV != nullptr);
		}

		ITextureView* pAttachments[] =
		{
			m_Shadow.pShadowDSV
		};

		FramebufferDesc fb;
		fb.Name = "Shadow FB";
		fb.pRenderPass = m_pShadowRenderPass;
		fb.AttachmentCount = 1;
		fb.ppAttachments = pAttachments;

		RefCntAutoPtr<IFramebuffer> out;
		m_pDevice->CreateFramebuffer(fb, &out);
		ASSERT_EXPR(out != nullptr);
		return out;
	}


	RefCntAutoPtr<IFramebuffer> Tutorial08_DeferredRendering::createGBufferFramebuffer()
	{
		ASSERT(m_GBuffer.pAlbedo == nullptr, "m_GBuffer.pAlbedo must be null");
		ASSERT(m_GBuffer.pNormal == nullptr, "m_GBuffer.pNormal must be null");
		ASSERT(m_GBuffer.pMaterial == nullptr, "m_GBuffer.pMaterial must be null");
		ASSERT(m_GBuffer.pDepthZ == nullptr, "m_GBuffer.pDepthZ must be null");
		ASSERT(m_GBuffer.pDepth == nullptr, "m_GBuffer.pDepth must be null");
		// Create textures (window-sized)
		const SwapChainDesc& sc = m_pSwapChain->GetDesc();

		auto CreateRT = [&](const char* name, TEXTURE_FORMAT fmt, RefCntAutoPtr<ITexture>& outTex)
		{
			TextureDesc td;
			td.Name = name;
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = sc.Width;
			td.Height = sc.Height;
			td.MipLevels = 1;
			td.Format = fmt;
			td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

			td.ClearValue.Format = fmt;
			td.ClearValue.Color[0] = 0.f;
			td.ClearValue.Color[1] = 0.f;
			td.ClearValue.Color[2] = 0.f;
			td.ClearValue.Color[3] = 1.f;

			if (!outTex)
				m_pDevice->CreateTexture(td, nullptr, &outTex);
		};

		CreateRT("GBuffer_Albedo", TEX_FORMAT_RGBA8_UNORM, m_GBuffer.pAlbedo);
		CreateRT("GBuffer_Normal", TEX_FORMAT_RGBA16_FLOAT, m_GBuffer.pNormal);
		CreateRT("GBuffer_Material", TEX_FORMAT_RGBA8_UNORM, m_GBuffer.pMaterial);
		CreateRT("GBuffer_DepthZ", TEX_FORMAT_R32_FLOAT, m_GBuffer.pDepthZ);

		// Depth buffer
		{
			TextureDesc td;
			td.Name = "GBuffer_Depth";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = sc.Width;
			td.Height = sc.Height;
			td.MipLevels = 1;
			td.Format = TEX_FORMAT_D32_FLOAT;
			td.BindFlags = BIND_DEPTH_STENCIL;

			td.ClearValue.Format = td.Format;
			td.ClearValue.DepthStencil.Depth = 1.f;
			td.ClearValue.DepthStencil.Stencil = 0;

			if (!m_GBuffer.pDepth)
				m_pDevice->CreateTexture(td, nullptr, &m_GBuffer.pDepth);
		}

		ITextureView* pAttachments[] =
		{
			m_GBuffer.pAlbedo->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
			m_GBuffer.pNormal->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
			m_GBuffer.pMaterial->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
			m_GBuffer.pDepthZ->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
			m_GBuffer.pDepth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
		};

		FramebufferDesc fb;
		fb.Name = "GBuffer FB";
		fb.pRenderPass = m_pGBufferRenderPass;
		fb.AttachmentCount = _countof(pAttachments);
		fb.ppAttachments = pAttachments;

		RefCntAutoPtr<IFramebuffer> out;
		m_pDevice->CreateFramebuffer(fb, &out);
		ASSERT_EXPR(out != nullptr);
		return out;
	}

	RefCntAutoPtr<IFramebuffer> Tutorial08_DeferredRendering::createLightingFramebuffer()
	{
		ASSERT(m_Post.pLightingHDR == nullptr, "m_Post.pLightingHDR must be null");
		// Create lighting HDR texture (window-sized)
		const SwapChainDesc& sc = m_pSwapChain->GetDesc();

		TextureDesc td;
		td.Name = "LightingHDR";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = sc.Width;
		td.Height = sc.Height;
		td.MipLevels = 1;
		td.Format = TEX_FORMAT_RGBA16_FLOAT;
		td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

		td.ClearValue.Format = td.Format;
		td.ClearValue.Color[0] = 0.f;
		td.ClearValue.Color[1] = 0.f;
		td.ClearValue.Color[2] = 0.f;
		td.ClearValue.Color[3] = 1.f;

		m_Post.pLightingHDR.Release();
		m_pDevice->CreateTexture(td, nullptr, &m_Post.pLightingHDR);

		ITextureView* pAttachments[] =
		{
			m_Post.pLightingHDR->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)
		};

		FramebufferDesc fb;
		fb.Name = "Lighting FB";
		fb.pRenderPass = m_pLightingRenderPass;
		fb.AttachmentCount = 1;
		fb.ppAttachments = pAttachments;

		RefCntAutoPtr<IFramebuffer> out;
		m_pDevice->CreateFramebuffer(fb, &out);
		ASSERT_EXPR(out != nullptr);
		return out;
	}

	RefCntAutoPtr<IFramebuffer> Tutorial08_DeferredRendering::createPostFramebuffer(ITextureView* pBackBufferRTV)
	{
		auto it = m_PostFBCache.find(pBackBufferRTV);
		if (it != m_PostFBCache.end())
			return it->second;

		ITextureView* pAttachments[] = { pBackBufferRTV };

		FramebufferDesc fb;
		fb.Name = "Post FB";
		fb.pRenderPass = m_pPostRenderPass;
		fb.AttachmentCount = 1;
		fb.ppAttachments = pAttachments;

		RefCntAutoPtr<IFramebuffer> out;
		m_pDevice->CreateFramebuffer(fb, &out);
		ASSERT_EXPR(out != nullptr);

		m_PostFBCache.emplace(pBackBufferRTV, out);
		return out;
	}

	// ============================================================================
	// PSO creation
	// ============================================================================

	static void setupCommonShaderCI(
		ShaderCreateInfo& CI,
		IShaderSourceInputStreamFactory* pShaderSourceFactory)
	{
		CI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		CI.Desc.UseCombinedTextureSamplers = true;
		CI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
		CI.pShaderSourceStreamFactory = pShaderSourceFactory;
	}

	static void bindStaticCB(
		IPipelineState* pPSO,
		SHADER_TYPE     Stage,
		const char* Name,
		IBuffer* pCB)
	{
		if (auto* pVar = pPSO->GetStaticVariableByName(Stage, Name))
			pVar->Set(pCB);
	}

	void Tutorial08_DeferredRendering::createShadowPSO(IShaderSourceInputStreamFactory* pShaderSourceFactory)
	{
		GraphicsPipelineStateCreateInfo PSOCreateInfo;
		PipelineStateDesc& PSODesc = PSOCreateInfo.PSODesc;
		PSODesc.Name = "Tutorial08 Shadow PSO";

		PSOCreateInfo.GraphicsPipeline.pRenderPass = m_pShadowRenderPass;
		PSOCreateInfo.GraphicsPipeline.SubpassIndex = 0;
		PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;

		ShaderCreateInfo CI;
		setupCommonShaderCI(CI, pShaderSourceFactory);

		RefCntAutoPtr<IShader> pVS;
		{
			CI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			CI.EntryPoint = "main";
			CI.Desc.Name = "ShadowMap VS";
			CI.FilePath = "ShadowMap.vsh";
			m_pDevice->CreateShader(CI, &pVS);
			ASSERT_EXPR(pVS != nullptr);
		}

		RefCntAutoPtr<IShader> pPS;
		{
			CI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			CI.EntryPoint = "main";
			CI.Desc.Name = "ShadowMap PS";
			CI.FilePath = "ShadowMap.psh";
			m_pDevice->CreateShader(CI, &pPS);
			ASSERT_EXPR(pPS != nullptr);
		}

		const LayoutElement LayoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
			LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
			LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
		};

		PSOCreateInfo.pVS = pVS;
		PSOCreateInfo.pPS = pPS;
		PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
		PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

		PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
		};
		PSODesc.ResourceLayout.Variables = Vars;
		PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pShadowPSO);
		ASSERT_EXPR(m_pShadowPSO != nullptr);

		bindStaticCB(m_pShadowPSO, SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS", m_pShadowConstantsCB);
	}

	void Tutorial08_DeferredRendering::createGBufferPSO(IShaderSourceInputStreamFactory* pShaderSourceFactory)
	{
		GraphicsPipelineStateCreateInfo PSOCreateInfo;
		PipelineStateDesc& PSODesc = PSOCreateInfo.PSODesc;
		PSODesc.Name = "Tutorial08 GBuffer PSO";

		PSOCreateInfo.GraphicsPipeline.pRenderPass = m_pGBufferRenderPass;
		PSOCreateInfo.GraphicsPipeline.SubpassIndex = 0;
		PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;

		ShaderCreateInfo CI;
		setupCommonShaderCI(CI, pShaderSourceFactory);

		RefCntAutoPtr<IShader> pVS;
		{
			CI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			CI.EntryPoint = "main";
			CI.Desc.Name = "GBuffer VS";
			CI.FilePath = "GBuffer.vsh";
			m_pDevice->CreateShader(CI, &pVS);
			ASSERT_EXPR(pVS != nullptr);
		}

		RefCntAutoPtr<IShader> pPS;
		{
			CI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			CI.EntryPoint = "main";
			CI.Desc.Name = "GBuffer PS";
			CI.FilePath = "GBuffer.psh";
			m_pDevice->CreateShader(CI, &pPS);
			ASSERT_EXPR(pPS != nullptr);
		}

		const LayoutElement LayoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Pos
			LayoutElement{1, 0, 2, VT_FLOAT32, false}, // UV
			LayoutElement{2, 0, 3, VT_FLOAT32, false}, // Normal
		};

		PSOCreateInfo.pVS = pVS;
		PSOCreateInfo.pPS = pPS;
		PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
		PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

		PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{SHADER_TYPE_PIXEL, "g_BaseColorTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
			{SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
		};
		PSODesc.ResourceLayout.Variables = Vars;
		PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		SamplerDesc SamLinearClampDesc
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
		};

		ImmutableSamplerDesc Imtbl[] =
		{
			{SHADER_TYPE_PIXEL, "g_BaseColorTex", SamLinearClampDesc}
		};
		PSODesc.ResourceLayout.ImmutableSamplers = Imtbl;
		PSODesc.ResourceLayout.NumImmutableSamplers = _countof(Imtbl);

		m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pGBufferPSO);
		ASSERT_EXPR(m_pGBufferPSO != nullptr);

		bindStaticCB(m_pGBufferPSO, SHADER_TYPE_VERTEX, "SHADER_CONSTANTS", m_pShaderConstantsCB);
	}

	void Tutorial08_DeferredRendering::createLightingPSO(IShaderSourceInputStreamFactory* pShaderSourceFactory)
	{
		GraphicsPipelineStateCreateInfo PSOCreateInfo;
		PipelineStateDesc& PSODesc = PSOCreateInfo.PSODesc;
		PSODesc.Name = "Tutorial08 Lighting PSO";

		PSOCreateInfo.GraphicsPipeline.pRenderPass = m_pLightingRenderPass;
		PSOCreateInfo.GraphicsPipeline.SubpassIndex = 0;

		PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;

		ShaderCreateInfo CI;
		setupCommonShaderCI(CI, pShaderSourceFactory);

		RefCntAutoPtr<IShader> pVS;
		{
			CI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			CI.EntryPoint = "main";
			CI.Desc.Name = "Lighting VS";
			CI.FilePath = "Lighting.vsh";
			m_pDevice->CreateShader(CI, &pVS);
			ASSERT_EXPR(pVS != nullptr);
		}

		RefCntAutoPtr<IShader> pPS;
		{
			CI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			CI.EntryPoint = "main";
			CI.Desc.Name = "Lighting PS";
			CI.FilePath = "Lighting.psh";
			m_pDevice->CreateShader(CI, &pPS);
			ASSERT_EXPR(pPS != nullptr);
		}

		PSOCreateInfo.pVS = pVS;
		PSOCreateInfo.pPS = pPS;

		PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{SHADER_TYPE_PIXEL, "g_GBuffer_Albedo",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
			{SHADER_TYPE_PIXEL, "g_GBuffer_Normal",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
			{SHADER_TYPE_PIXEL, "g_GBuffer_Material", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
			{SHADER_TYPE_PIXEL, "g_GBuffer_DepthZ",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
			{SHADER_TYPE_PIXEL, "g_ShadowMap",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
			{SHADER_TYPE_PIXEL, "g_Lights",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
		};
		PSODesc.ResourceLayout.Variables = Vars;
		PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		// 샘플러들
		SamplerDesc SamLinearClamp
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
		};

		// Shadow 비교 샘플러
		SamplerDesc ShadowCmp =
			SamplerDesc{
				FILTER_TYPE_LINEAR,
				FILTER_TYPE_LINEAR,
				FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
		};
		ShadowCmp.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;

		ImmutableSamplerDesc Imtbl[] =
		{
			{SHADER_TYPE_PIXEL, "g_GBuffer_Albedo",   SamLinearClamp},
			{SHADER_TYPE_PIXEL, "g_GBuffer_Normal",   SamLinearClamp},
			{SHADER_TYPE_PIXEL, "g_GBuffer_Material", SamLinearClamp},
			{SHADER_TYPE_PIXEL, "g_GBuffer_DepthZ",   SamLinearClamp},
			{SHADER_TYPE_PIXEL, "g_ShadowMap",        ShadowCmp},
		};

		PSODesc.ResourceLayout.ImmutableSamplers = Imtbl;
		PSODesc.ResourceLayout.NumImmutableSamplers = _countof(Imtbl);

		m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pLightingPSO);
		ASSERT_EXPR(m_pLightingPSO != nullptr);

		bindStaticCB(m_pLightingPSO, SHADER_TYPE_VERTEX, "SHADER_CONSTANTS", m_pShaderConstantsCB);
		bindStaticCB(m_pLightingPSO, SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS", m_pShadowConstantsCB);
		bindStaticCB(m_pLightingPSO, SHADER_TYPE_PIXEL, "SHADER_CONSTANTS", m_pShaderConstantsCB);
		bindStaticCB(m_pLightingPSO, SHADER_TYPE_PIXEL, "SHADOW_CONSTANTS", m_pShadowConstantsCB);
	}

	void Tutorial08_DeferredRendering::createPostPSO(IShaderSourceInputStreamFactory* pShaderSourceFactory)
	{
		GraphicsPipelineStateCreateInfo PSOCreateInfo;
		PipelineStateDesc& PSODesc = PSOCreateInfo.PSODesc;
		PSODesc.Name = "Tutorial08 Post PSO";

		PSOCreateInfo.GraphicsPipeline.pRenderPass = m_pPostRenderPass;
		PSOCreateInfo.GraphicsPipeline.SubpassIndex = 0;

		PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;

		ShaderCreateInfo CI;
		setupCommonShaderCI(CI, pShaderSourceFactory);

		// gamma 매크로 (Post.psh에 있음)
		ShaderMacro Macros[] = { {"CONVERT_PS_OUTPUT_TO_GAMMA", m_ConvertPSOutputToGamma ? "1" : "0"} };
		CI.Macros = { Macros, _countof(Macros) };

		RefCntAutoPtr<IShader> pVS;
		{
			CI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			CI.EntryPoint = "main";
			CI.Desc.Name = "Post VS";
			CI.FilePath = "Post.vsh";
			m_pDevice->CreateShader(CI, &pVS);
			ASSERT_EXPR(pVS != nullptr);
		}

		RefCntAutoPtr<IShader> pPS;
		{
			CI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			CI.EntryPoint = "main";
			CI.Desc.Name = "Post PS";
			CI.FilePath = "Post.psh";
			m_pDevice->CreateShader(CI, &pPS);
			ASSERT_EXPR(pPS != nullptr);
		}

		PSOCreateInfo.pVS = pVS;
		PSOCreateInfo.pPS = pPS;

		PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{SHADER_TYPE_PIXEL, "g_LightingTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
		};
		PSODesc.ResourceLayout.Variables = Vars;
		PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		SamplerDesc SamLinearClamp
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
		};

		ImmutableSamplerDesc Imtbl[] =
		{
			{SHADER_TYPE_PIXEL, "g_LightingTex", SamLinearClamp},
		};
		PSODesc.ResourceLayout.ImmutableSamplers = Imtbl;
		PSODesc.ResourceLayout.NumImmutableSamplers = _countof(Imtbl);

		m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPostPSO);
		ASSERT_EXPR(m_pPostPSO != nullptr);

		bindStaticCB(m_pPostPSO, SHADER_TYPE_PIXEL, "SHADER_CONSTANTS", m_pShaderConstantsCB);
		bindStaticCB(m_pPostPSO, SHADER_TYPE_PIXEL, "SHADOW_CONSTANTS", m_pShadowConstantsCB);
	}

	// ============================================================================
	// Draw helpers
	// ============================================================================

	void Tutorial08_DeferredRendering::drawScene_Shadow()
	{
		{
			IBuffer* pVBs[] = { m_CubeVertexBuffer };
			m_pImmediateContext->SetVertexBuffers(0, 1, pVBs, nullptr, RESOURCE_STATE_TRANSITION_MODE_VERIFY, SET_VERTEX_BUFFERS_FLAG_RESET);
			m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			m_pImmediateContext->SetPipelineState(m_pShadowPSO);
			m_pImmediateContext->CommitShaderResources(m_pShadowSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			const int GridDim = 7;
			const float Spacing = 3.0f;
			const float3 Base = float3{ -9.f, 1.f, -9.f };

			for (int z = 0; z < GridDim; ++z)
			{
				for (int x = 0; x < GridDim; ++x)
				{
					float3 t = Base + float3{ x * Spacing, 0.f, z * Spacing };

					{
						MapHelper<HLSL::ObjectConstants> obj(m_pImmediateContext, m_pObjectConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD);
						obj->World = float4x4::Translation(t);
						obj->WorldInvertTranspose = float3x3::Identity(); // translation-only면 identity OK
					}

					DrawIndexedAttribs draw;
					draw.IndexType = VT_UINT32;
					draw.NumIndices = 36;
					draw.NumInstances = 1;
					draw.Flags = DRAW_FLAG_VERIFY_ALL;
					m_pImmediateContext->DrawIndexed(draw);
				}
			}
		}

		{
			IBuffer* pVBs[] = { m_PlaneVertexBuffer };
			m_pImmediateContext->SetVertexBuffers(0, 1, pVBs, nullptr, RESOURCE_STATE_TRANSITION_MODE_VERIFY, SET_VERTEX_BUFFERS_FLAG_RESET);
			m_pImmediateContext->SetIndexBuffer(m_PlaneIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			m_pImmediateContext->SetPipelineState(m_pShadowPSO);
			m_pImmediateContext->CommitShaderResources(m_pShadowSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			{
				MapHelper<HLSL::ObjectConstants> obj(m_pImmediateContext, m_pObjectConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD);
				obj->World = float4x4::Scale({ 100.f, 1.f, 100.f }) * float4x4::Translation({ 0.f, -2.f, 0.f });
				obj->WorldInvertTranspose = float3x3::Identity();
			}

			DrawIndexedAttribs draw;
			draw.IndexType = VT_UINT32;
			draw.NumIndices = 6;
			draw.NumInstances = 1;
			draw.Flags = DRAW_FLAG_VERIFY_ALL;
			m_pImmediateContext->DrawIndexed(draw);
		}
	}

	void Tutorial08_DeferredRendering::drawScene_GBuffer()
	{
		// Cube VB/IB
		{
			m_pImmediateContext->SetPipelineState(m_pGBufferPSO);
			m_pImmediateContext->CommitShaderResources(m_pGBufferSRB_Cube, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			if (auto* pVar = m_pGBufferSRB_Cube->GetVariableByName(SHADER_TYPE_PIXEL, "g_BaseColorTex"))
				pVar->Set(m_CubeTextureSRV);

			IBuffer* pVBs[] = { m_CubeVertexBuffer };
			m_pImmediateContext->SetVertexBuffers(0, 1, pVBs, nullptr, RESOURCE_STATE_TRANSITION_MODE_VERIFY, SET_VERTEX_BUFFERS_FLAG_RESET);
			m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			const int GridDim = 7;
			const float Spacing = 3.0f;
			const float3 Base = float3{ -9.f, 1.f, -9.f };

			for (int z = 0; z < GridDim; ++z)
			{
				for (int x = 0; x < GridDim; ++x)
				{
					float3 t = Base + float3{ x * Spacing, 0.f, z * Spacing };

					{
						MapHelper<HLSL::ObjectConstants> obj(m_pImmediateContext, m_pObjectConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD);
						obj->World = float4x4::Translation(t);
						obj->WorldInvertTranspose = float3x3::Identity(); // translation-only면 identity OK
					}

					DrawIndexedAttribs draw;
					draw.IndexType = VT_UINT32;
					draw.NumIndices = 36;
					draw.NumInstances = 1;
					draw.Flags = DRAW_FLAG_VERIFY_ALL;
					m_pImmediateContext->DrawIndexed(draw);
				}
			}
		}

		// Plane VB/IB
		{
			m_pImmediateContext->SetPipelineState(m_pGBufferPSO);
			m_pImmediateContext->CommitShaderResources(m_pGBufferSRB_Plane, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			IBuffer* pVBs[] = { m_PlaneVertexBuffer };
			m_pImmediateContext->SetVertexBuffers(0, 1, pVBs, nullptr, RESOURCE_STATE_TRANSITION_MODE_VERIFY, SET_VERTEX_BUFFERS_FLAG_RESET);
			m_pImmediateContext->SetIndexBuffer(m_PlaneIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			{
				MapHelper<HLSL::ObjectConstants> obj(m_pImmediateContext, m_pObjectConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD);
				obj->World = float4x4::Scale({ 100.f, 1.f, 100.f }) * float4x4::Translation({ 0.f, -2.f, 0.f });
				obj->WorldInvertTranspose = float3x3::Identity();
			}

			DrawIndexedAttribs draw;
			draw.IndexType = VT_UINT32;
			draw.NumIndices = 6;
			draw.NumInstances = 1;
			draw.Flags = DRAW_FLAG_VERIFY_ALL;
			m_pImmediateContext->DrawIndexed(draw);
		}
	}


	void Tutorial08_DeferredRendering::drawFullscreen_Lighting()
	{
		m_pImmediateContext->SetPipelineState(m_pLightingPSO);
		m_pImmediateContext->CommitShaderResources(m_pLightingSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		DrawAttribs draw;
		draw.NumVertices = 4; // Lighting.vsh: triangle strip 4 verts
		draw.Flags = DRAW_FLAG_VERIFY_ALL;
		m_pImmediateContext->Draw(draw);
	}

	void Tutorial08_DeferredRendering::drawFullscreen_Post()
	{
		m_pImmediateContext->SetPipelineState(m_pPostPSO);
		m_pImmediateContext->CommitShaderResources(m_pPostSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		DrawAttribs draw;
		draw.NumVertices = 4;
		draw.Flags = DRAW_FLAG_VERIFY_ALL;
		m_pImmediateContext->Draw(draw);
	}

	// ============================================================================
	// Lights
	// ============================================================================

	void Tutorial08_DeferredRendering::initLights()
	{
		FastRandReal<float> Rnd{ 0, 0, 1 };

		m_Lights.resize(m_LightsCount);
		m_LightMoveDirs.resize(m_LightsCount);

		if (m_LightsCount > 0)
		{
			auto& L0 = m_Lights[0];
			L0.Location = float3{ 0.f, 100.f, 0.f };          // 위쪽에서 비추는 느낌
			L0.Radius = 1000.f;                           // 사실상 전역처럼
			L0.Color = float3{ 1.0f, 0.98f, 0.92f } *1.5f; // 약간 warm, 강도는 원하는 대로
			L0.Padding = 0.f;

			m_LightMoveDirs[0] = float3{ 0.f, 0.f, 0.f };   // 안 움직임
		}

		for (int i = 1; i < m_LightsCount; ++i)
		{
			auto& L = m_Lights[i];
			L.Location = (float3{ Rnd(), Rnd(), Rnd() } - float3{ 0.5f, 0.5f, 0.5f }) * 2.0f * 7.0f;
			L.Radius = 3.0f + Rnd() * 10.0f;
			L.Color = float3{ Rnd(), Rnd(), Rnd() };
			L.Padding = 0.f;

			m_LightMoveDirs[i] =
				(float3{ Rnd(), Rnd(), Rnd() } - float3{ 0.5f, 0.5f, 0.5f }) * 1.f;
		}
	}


	void Tutorial08_DeferredRendering::createLightsBuffer()
	{
		m_pLightsBuffer.Release();
		m_pLightsSRV.Release();

		BufferDesc bd;
		bd.Name = "Lights StructuredBuffer";
		bd.Usage = USAGE_DYNAMIC;
		bd.BindFlags = BIND_SHADER_RESOURCE;
		bd.CPUAccessFlags = CPU_ACCESS_WRITE;
		bd.Mode = BUFFER_MODE_STRUCTURED;
		bd.ElementByteStride = sizeof(HLSL::LightAttribs);
		bd.Size = sizeof(HLSL::LightAttribs) * static_cast<uint64>(m_LightsCount);

		m_pDevice->CreateBuffer(bd, nullptr, &m_pLightsBuffer);
		ASSERT_EXPR(m_pLightsBuffer != nullptr);

		m_pLightsSRV = m_pLightsBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
		ASSERT_EXPR(m_pLightsSRV != nullptr);
	}

	void Tutorial08_DeferredRendering::updateLights(float fElapsedTime)
	{
		float3 VolumeMin{ -7.f, -7.f, -7.f };
		float3 VolumeMax{ +7.f, +7.f, +7.f };

		for (int i = 1; i < m_LightsCount; ++i) // ✅ 0번은 고정 전역 라이트
		{
			auto& L = m_Lights[i];
			auto& Dir = m_LightMoveDirs[i];

			L.Location += Dir * fElapsedTime;

			auto Clamp = [](float& c, float& d, float mn, float mx)
			{
				if (c < mn) { c += (mn - c) * 2.f; d *= -1.f; }
				else if (c > mx) { c -= (c - mx) * 2.f; d *= -1.f; }
			};

			Clamp(L.Location.x, Dir.x, VolumeMin.x, VolumeMax.x);
			Clamp(L.Location.y, Dir.y, VolumeMin.y, VolumeMax.y);
			Clamp(L.Location.z, Dir.z, VolumeMin.z, VolumeMax.z);
		}
	}

	// ============================================================================

	void Tutorial08_DeferredRendering::releaseWindowResources()
	{
		m_GBuffer = {};
		m_Post = {};

		m_pGBufferFB.Release();
		m_pLightingFB.Release();
		m_PostFBCache.clear();

		m_pLightingSRB.Release();
		m_pPostSRB.Release();
	}

} // namespace shz
