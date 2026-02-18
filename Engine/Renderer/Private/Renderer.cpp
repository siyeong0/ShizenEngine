#include "pch.h"
#include "Engine/Renderer/Public/Renderer.h"

#include "Engine/Core/Math/Math.h"

#include "Engine/Image/Public/TextureUtilities.h"
#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/AssetManager/Public/AssimpImporter.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/GraphicsTools/Public/ShaderMacroHelper.hpp"
#include "Engine/GraphicsUtils/Public/GraphicsUtils.hpp"

#include "Engine/Renderer/Public/DrawPacket.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	bool Renderer::Initialize(const RendererCreateInfo& createInfo)
	{
		ASSERT(createInfo.pDevice, "Device is null.");
		ASSERT(createInfo.pImmediateContext, "ImmediateContext is null.");
		ASSERT(createInfo.pSwapChain, "SwapChain is null.");
		ASSERT(createInfo.pAssetManager, "AssetManager is null.");
		ASSERT(createInfo.pShaderSourceFactory, "ShaderSourceFactory is null.");

		m_CreateInfo = createInfo;
		m_pDevice = createInfo.pDevice;
		m_pImmediateContext = createInfo.pImmediateContext;
		m_pDeferredContexts = createInfo.pDeferredContexts;
		m_pSwapChain = createInfo.pSwapChain;
		m_pAssetManager = createInfo.pAssetManager;
		m_pShaderSourceFactory = createInfo.pShaderSourceFactory;

		m_pRegistry = std::make_unique<RenderResourceRegistry>();
		m_pRegistry->Initialize();

		const SwapChainDesc& scDesc = m_pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : scDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : scDesc.Height;

		m_pPipelineStateManager = std::make_unique<PipelineStateManager>();
		m_pPipelineStateManager->Initialize(m_pDevice, m_pRegistry.get());

		// -----------------------------------------------------------------
		// Fill PassContext from registry (shared resources)
		// -----------------------------------------------------------------
		m_PassCtx = {};
		m_PassCtx.pDevice = m_pDevice.RawPtr();
		m_PassCtx.pImmediateContext = m_pImmediateContext.RawPtr();
		m_PassCtx.pSwapChain = m_pSwapChain.RawPtr();
		m_PassCtx.pRenderer = this;
		m_PassCtx.pShaderSourceFactory = m_pShaderSourceFactory.RawPtr();
		m_PassCtx.pAssetManager = m_pAssetManager;
		m_PassCtx.pPipelineStateManager = m_pPipelineStateManager.get();
		m_PassCtx.pRegistry = m_pRegistry.get();

		// -----------------------------------------------------------------
		// Create error texture
		// -----------------------------------------------------------------
		{
			AssetRef<Texture> errorTexRef = m_pAssetManager->RegisterAsset<Texture>("C:/Dev/ShizenEngine/Assets/Error.jpg");
			AssetPtr<Texture> errorTexPtr = m_pAssetManager->Acquire(errorTexRef);

			Texture* pErrorTex = errorTexPtr.Get();
			const auto& mips = pErrorTex->GetMips();
			ASSERT(!mips.empty(), "TextureAsset has no mips.");

			const uint32 width = mips[0].Width;
			const uint32 height = mips[0].Height;

			TextureDesc desc = {};
			desc.Name = "ErrorTexture";
			desc.Type = RESOURCE_DIM_TEX_2D;
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = static_cast<uint32>(mips.size());
			desc.ArraySize = 1;
			desc.Format = pErrorTex->GetFormat();
			desc.Usage = USAGE_DEFAULT;
			desc.BindFlags = BIND_SHADER_RESOURCE;

			std::vector<TextureSubResData> subres;
			subres.resize(mips.size());

			for (size_t i = 0; i < mips.size(); ++i)
			{
				const TextureMip& mip = mips[i];
				TextureSubResData sr = {};
				sr.pData = mip.Data.data();
				sr.Stride = static_cast<uint64>(mip.Width) * GetTextureFormatAttribs(desc.Format).GetElementSize();
				sr.DepthStride = 0;
				subres[i] = sr;
			}

			TextureData initData = {};
			initData.pSubResources = subres.data();
			initData.NumSubresources = static_cast<uint32>(subres.size());

			RefCntAutoPtr<ITexture> errorTex = CreateTexture(desc, &initData);
			ASSERT(errorTex, "CreateTexture failed.");

			AddTexture(STRING_HASH("ErrorTex"), std::move(errorTex));
		}

		// -----------------------------------------------------------------
		// Create shared buffers
		// -----------------------------------------------------------------
		{
			IRenderDevice* dev = m_pDevice.RawPtr();
			ASSERT(dev, "Device is null.");

			RefCntAutoPtr<IBuffer> frameCB;
			RefCntAutoPtr<IBuffer> viewCB;
			RefCntAutoPtr<IBuffer> drawCB;
			RefCntAutoPtr<IBuffer> shadowCB;

			CreateUniformBuffer(dev, sizeof(hlsl::FrameConstants), "Frame constants", &frameCB);
			CreateUniformBuffer(dev, sizeof(hlsl::ViewConstants), "View constants", &viewCB);
			CreateUniformBuffer(dev, sizeof(hlsl::DrawConstants), "Draw constants", &drawCB);
			CreateUniformBuffer(dev, sizeof(hlsl::ShadowConstants), "Shadow constants", &shadowCB);
			ASSERT(frameCB, "Frame CB create failed.");
			ASSERT(viewCB, "View CB create failed.");
			ASSERT(drawCB, "Draw CB create failed.");
			ASSERT(shadowCB, "Shadow CB create failed.");

			m_pRegistry->RegisterBuffer(STRING_HASH("FRAME_CONSTANTS"), std::move(frameCB));
			m_pRegistry->RegisterBuffer(STRING_HASH("VIEW_CONSTANTS"), std::move(viewCB));
			m_pRegistry->RegisterBuffer(STRING_HASH("DRAW_CONSTANTS"), std::move(drawCB));
			m_pRegistry->RegisterBuffer(STRING_HASH("SHADOW_CONSTANTS"), std::move(shadowCB));

			m_pPipelineStateManager->RegisterStaticBufferCBV("FRAME_CONSTANTS", STRING_HASH("FRAME_CONSTANTS"));
			m_pPipelineStateManager->RegisterStaticBufferCBV("VIEW_CONSTANTS", STRING_HASH("VIEW_CONSTANTS"));
			m_pPipelineStateManager->RegisterStaticBufferCBV("DRAW_CONSTANTS", STRING_HASH("DRAW_CONSTANTS"));
			m_pPipelineStateManager->RegisterStaticBufferCBV("SHADOW_CONSTANTS", STRING_HASH("SHADOW_CONSTANTS"));

			auto createObjectTable = [&](const char* name) -> RefCntAutoPtr<IBuffer>
				{
					BufferDesc desc = {};
					desc.Name = name;
					desc.Usage = USAGE_DYNAMIC;
					desc.BindFlags = BIND_SHADER_RESOURCE;
					desc.CPUAccessFlags = CPU_ACCESS_WRITE;
					desc.Mode = BUFFER_MODE_STRUCTURED;
					desc.ElementByteStride = sizeof(hlsl::ObjectConstants);
					desc.Size = uint64(desc.ElementByteStride) * uint64(DEFAULT_MAX_OBJECT_COUNT);

					RefCntAutoPtr<IBuffer> sb = CreateBuffer(desc, nullptr);
					ASSERT(sb, "Object table create failed.");
					return sb;
				};

			AddBuffer(STRING_HASH("BasePassObjectTable"), std::move(createObjectTable("BasePassObjectTable")));
			AddBuffer(STRING_HASH("ForwardPassObjectTable"), std::move(createObjectTable("ForwardPassObjectTable")));
			AddBuffer(STRING_HASH("ShadowPassObjectTable"), std::move(createObjectTable("ShadowPassObjectTable")));
		}

		// -----------------------------------------------------------------
		// Create env textures
		// -----------------------------------------------------------------
		{
			TextureLoadInfo tli = {};
			RefCntAutoPtr<ITexture> env, diff, spec, brdf;

			CreateTextureFromFile(m_CreateInfo.EnvTexturePath.c_str(), tli, m_pDevice, &env);
			CreateTextureFromFile(m_CreateInfo.DiffuseIrradianceTexPath.c_str(), tli, m_pDevice, &diff);
			CreateTextureFromFile(m_CreateInfo.SpecularIrradianceTexPath.c_str(), tli, m_pDevice, &spec);
			CreateTextureFromFile(m_CreateInfo.BrdfLUTTexPath.c_str(), tli, m_pDevice, &brdf);

			ASSERT(env, "Env tex load failed.");
			ASSERT(diff, "Env diffuse load failed.");
			ASSERT(spec, "Env specular load failed.");
			ASSERT(brdf, "Env brdf load failed.");

			AddTexture(STRING_HASH("EnvTex"), std::move(env));
			AddTexture(STRING_HASH("EnvDiffuseTex"), std::move(diff));
			AddTexture(STRING_HASH("EnvSpecularTex"), std::move(spec));
			AddTexture(STRING_HASH("EnvBrdfTex"), std::move(brdf));

			m_pPipelineStateManager->RegisterStaticTextureResource("g_EnvMapTex", STRING_HASH("EnvTex"));
			m_pPipelineStateManager->RegisterStaticTextureResource("g_IrradianceIBLTex", STRING_HASH("EnvDiffuseTex"));
			m_pPipelineStateManager->RegisterStaticTextureResource("g_SpecularIBLTex", STRING_HASH("EnvSpecularTex"));
			m_pPipelineStateManager->RegisterStaticTextureResource("g_BrdfIBLTex", STRING_HASH("EnvBrdfTex"));
		}

		m_pShadowSystem = std::make_unique<ShadowSystem>();
		m_pShadowSystem->Initialize(*this, ShadowSystem::CreateInfo{});
		m_pShadowSystem->InstallPasses(*this);

		Material::RegisterTemplateLibrary(&m_TemplateLibrary);
		RegisterMaterialTemplate("DefaultLit", "GBuffer.vsh", "GBuffer.psh", MATERIAL_BLEND_MODE_MASKED);

		return true;
	}

	void Renderer::Cleanup()
	{
		ReleaseSwapChainBuffers();

		m_PassTable.clear();
		m_PassAddOrder.clear();
		m_CompiledPassOrder.clear();
		m_ResourceStates.clear();
		m_ExternalStates.clear();
		m_PassNameTable.clear();

		m_StaticMeshCache.Clear();

		if (m_pPipelineStateManager)
		{
			m_pPipelineStateManager->Clear();
			m_pPipelineStateManager.reset();
		}

		m_pShaderSourceFactory.Release();
		m_pAssetManager = nullptr;

		m_pRegistry->Shutdown();

		m_PipelineBindingCache.clear();

		m_PendingBarriers.clear();

		m_CreateInfo = {};
		m_PassCtx = {};
		m_Width = 0;
		m_Height = 0;

		m_pSwapChain.Release();
		m_pImmediateContext.Release();
		m_pDeferredContexts.clear();
		m_pDevice.Release();
	}

	void Renderer::BeginFrame()
	{
		if (m_bRenderGraphDirty)
		{
			compileRenderGraphOrder();
			m_bRenderGraphDirty = false;

			std::cout << "Compiled pass order : " << std::endl;
			for (uint64 passId : m_CompiledPassOrder)
			{
				std::cout << m_PassNameTable[passId] << std::endl;
			}
			std::cout << std::endl << std::endl;
		}

		{
			ASSERT(m_pDevice, "Device is null.");
			ASSERT(m_pSwapChain, "SwapChain is null.");
			ASSERT(m_pPresentRenderPass, "Present RenderPass is null.");

			ITextureView* pBBRTV = m_pSwapChain->GetCurrentBackBufferRTV();
			ASSERT(pBBRTV, "Current backbuffer RTV is null.");

			FramebufferDesc fb = {};
			fb.Name = "FB_SwapChainBackBuffer";
			fb.pRenderPass = m_pPresentRenderPass;
			fb.AttachmentCount = 1;
			fb.ppAttachments = &pBBRTV;

			m_pSwapChainFramebuffer.Release();
			m_pDevice->CreateFramebuffer(fb, &m_pSwapChainFramebuffer);
			ASSERT(m_pSwapChainFramebuffer, "CreateFramebuffer(FB_SwapChainBackBuffer) failed.");
		}
	}

	void Renderer::Render(RenderScene& scene, const ViewFamily& viewFamily)
	{
		ASSERT(m_PassCtx.pImmediateContext, "Context is invalid.");
		ASSERT(!viewFamily.Views.empty(), "No view.");

		IDeviceContext* ctx = m_PassCtx.pImmediateContext;
		m_PassCtx.ResetFrame();

		// ---------------------------------------------------------------------
		// Pull shared renderer resources from registry
		// ---------------------------------------------------------------------
		IBuffer* pFrameCB = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("FRAME_CONSTANTS"));
		IBuffer* pViewCB = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("VIEW_CONSTANTS"));
		IBuffer* pDrawCB = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("DRAW_CONSTANTS"));

		IBuffer* pBasePassObjectTable = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("BasePassObjectTable"));
		IBuffer* pForwardPassObjectTable = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("ForwardPassObjectTable"));
		IBuffer* pShadowPassObjectTable = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("ShadowPassObjectTable"));

		ITexture* pEnvTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvTex"));
		ITexture* pEnvDiffTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvDiffuseTex"));
		ITexture* pEnvSpecTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvSpecularTex"));
		ITexture* pEnvBrdfTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvBrdfTex"));
		ITexture* pErrorTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("ErrorTex"));

		ASSERT(pFrameCB, "FrameCB missing (registry).");
		ASSERT(pViewCB, "ViewCB missing (registry).");
		ASSERT(pDrawCB, "DrawCB missing (registry).");
		ASSERT(pBasePassObjectTable && pForwardPassObjectTable && pShadowPassObjectTable, "ObjectTable SB missing (registry).");
		ASSERT(pEnvTex && pEnvDiffTex && pEnvSpecTex && pEnvBrdfTex, "Env textures missing (registry).");
		ASSERT(pErrorTex, "Error texture missing (registry).");

		m_PassCtx.pScene = &scene;
		m_PassCtx.pViewFamily = &viewFamily;
		m_PassCtx.DeltaTime = viewFamily.DeltaTime;
		m_PassCtx.FrameIndex = viewFamily.FrameIndex;

		const View& view = viewFamily.Views[0];

		// ------------------------------------------------------------
		// Viewport + Frustum
		// ------------------------------------------------------------
		float2 viewportSize =
		{
			static_cast<float>(view.Viewport.right - view.Viewport.left),
			static_cast<float>(view.Viewport.bottom - view.Viewport.top)
		};

		ViewFrustumExt frustumMain = {};
		{
			Matrix4x4 viewProj = view.ViewProjMatrix;
			ExtractViewFrustumPlanesFromMatrix(viewProj, frustumMain);
		}
		m_PassCtx.MainViewFrustum = frustumMain;

		// ------------------------------------------------------------
		// Update Frame/Shadow constants + compute lightViewProj (STABLE)
		// ------------------------------------------------------------
		Matrix4x4 lightViewProj = {};
		float3 lightDirWs = {};
		{
			MapHelper<hlsl::FrameConstants> frameCB(ctx, pFrameCB, MAP_WRITE, MAP_FLAG_DISCARD);

			frameCB->CameraPosition = view.CameraPosition;
			frameCB->FrameIndex = static_cast<uint32>(viewFamily.FrameIndex);

			frameCB->View = view.ViewMatrix;
			frameCB->Proj = view.ProjMatrix;
			frameCB->ViewProj = view.ViewProjMatrix;
			frameCB->InvViewProj = view.ViewProjMatrix.Inversed();

			frameCB->FrustumPlanesWS[0] = frustumMain.NearPlane;
			frameCB->FrustumPlanesWS[1] = frustumMain.FarPlane;
			frameCB->FrustumPlanesWS[2] = frustumMain.TopPlane;
			frameCB->FrustumPlanesWS[3] = frustumMain.BottomPlane;
			frameCB->FrustumPlanesWS[4] = frustumMain.LeftPlane;
			frameCB->FrustumPlanesWS[5] = frustumMain.RightPlane;

			frameCB->ViewportSize = viewportSize;
			frameCB->InvViewportSize = { 1.f / viewportSize.x, 1.f / viewportSize.y };

			frameCB->NearPlane = view.NearPlane;
			frameCB->FarPlane = view.FarPlane;
			frameCB->DeltaTime = viewFamily.DeltaTime;
			frameCB->CurrTime = viewFamily.CurrentTime;

			// ------------------------------------------------------------
			// Global light (first one)
			// ------------------------------------------------------------
			const RenderScene::LightObject* globalLight = nullptr;
			for (const auto& l : scene.GetLights()) { globalLight = &l; break; }

			lightDirWs = globalLight ? globalLight->Direction.Normalized() : float3(0, -1, 0);
			float3 lightColor = globalLight ? globalLight->Color : float3(1, 1, 1);
			float  lightIntensity = globalLight ? globalLight->Intensity : 1.0f;

			frameCB->LightDirWS = lightDirWs;
			frameCB->LightColor = lightColor;
			frameCB->LightIntensity = lightIntensity;

			// ============================================================
			// STABLE SHADOW (Camera-locked center + Texel Snap + WorldY near/far)
			// ============================================================
			// ----------------------------
			// Tunables
			// ----------------------------
			const float ShadowHalfExtent = 100.0f;  // XY 반범위(m)
			const float ShadowDepth = 200.0f;  // 라이트 방향 커버 깊이(m)
			const float PadZ = 30.0f;

			// 월드 높이 범위 (네 월드 스펙)
			const float WorldMinY = -500.0f;
			const float WorldMaxY = 1000.0f;

			const float3 lightForward = lightDirWs.Normalized();

			// Robust up
			float3 up = float3(0, 1, 0);
			if (Abs(Vector3::Dot(up, lightForward)) > 0.99f) { up = float3(0, 0, 1); }

			// ----------------------------
			// 0) unitsPerTexel (고정) + centerWs 월드 양자화(이동 안정화의 핵심)
			// ----------------------------
			const float extentXY = ShadowHalfExtent * 2.0f;
			const float unitsPerTexel = extentXY / float(m_pShadowSystem->GetResolution());

			// 카메라 중심을 "텍셀 월드 크기" 단위로 양자화해서 연속 이동을 제거
			float3 centerWs = view.CameraPosition;
			centerWs.x = floor(centerWs.x / unitsPerTexel + 0.5f) * unitsPerTexel;
			centerWs.z = floor(centerWs.z / unitsPerTexel + 0.5f) * unitsPerTexel;
			// Y는 보통 양자화하지 않음(높낮이 이동 시 점프 방지). 필요하면 고정/클램프 가능.
			// centerWs.y = view.CameraPosition.y;

			// ----------------------------
			// 1) light view
			// ----------------------------
			const float3 lightPosWs = centerWs - lightForward * ShadowDepth;
			const Matrix4x4 lightView = Matrix4x4::LookAtLH(lightPosWs, centerWs, up);

			// ----------------------------
			// 2) Ortho XY (고정 크기)
			// ----------------------------
			float minX = -ShadowHalfExtent;
			float maxX = +ShadowHalfExtent;
			float minY = -ShadowHalfExtent;
			float maxY = +ShadowHalfExtent;

			// ----------------------------
			// 3) near/far: 월드 Y 범위를 대표점 8개로 투영해서 z min/max
			//    (centerWs가 양자화돼서 이 값도 프레임마다 '덜' 흔들림)
			// ----------------------------
			float nearZ = +FLT_MAX;
			float farZ = -FLT_MAX;

			const float x0 = centerWs.x - ShadowHalfExtent;
			const float x1 = centerWs.x + ShadowHalfExtent;
			const float z0 = centerWs.z - ShadowHalfExtent;
			const float z1 = centerWs.z + ShadowHalfExtent;

			const float3 samplesWS[8] =
			{
				float3(x0, WorldMinY, z0),
				float3(x1, WorldMinY, z0),
				float3(x0, WorldMinY, z1),
				float3(x1, WorldMinY, z1),

				float3(x0, WorldMaxY, z0),
				float3(x1, WorldMaxY, z0),
				float3(x0, WorldMaxY, z1),
				float3(x1, WorldMaxY, z1),
			};

			for (int i = 0; i < 8; ++i)
			{
				const float4 pLs4 = float4(samplesWS[i], 1.0f) * lightView;
				nearZ = Min(nearZ, pLs4.z);
				farZ = Max(farZ, pLs4.z);
			}

			nearZ -= PadZ;
			farZ += PadZ;

			// 안전장치
			if (farZ < nearZ + 1.0f)
			{
				const float mid = 0.5f * (nearZ + farZ);
				nearZ = mid - 1.0f;
				farZ = mid + 1.0f;
			}

			nearZ = -PadZ;
			farZ = ShadowDepth * 3.0f + PadZ; // 넉넉히

			// ----------------------------
			// 4) (선택 but 권장) Projection Window Snap in light-space
			//    - world 양자화만으로도 대부분 끝나지만,
			//      남는 부동소수/행렬 오차를 한번 더 눌러줌
			// ----------------------------
			{
				// center in light-space
				const float4 centerLs4 = float4(centerWs, 1.0f) * lightView;
				const float2 centerLs = float2(centerLs4.x, centerLs4.y);

				// snap to nearest texel in light-space (unitsPerTexel은 위에서 고정 계산)
				const float2 snapped =
				{
					floor(centerLs.x / unitsPerTexel + 0.5f) * unitsPerTexel,
					floor(centerLs.y / unitsPerTexel + 0.5f) * unitsPerTexel
				};

				const float2 delta = snapped - centerLs;

				// shift ortho window by delta
				minX += delta.x;  maxX += delta.x;
				minY += delta.y;  maxY += delta.y;
			}

			// ----------------------------
			// 5) lightProj + lightViewProj
			// ----------------------------
			const Matrix4x4 lightProj = Matrix4x4::OrthoOffCenter(
				minX, maxX,
				minY, maxY,
				nearZ, farZ);

			lightViewProj = lightView * lightProj;
			frameCB->LightViewProj = lightViewProj;

			// ----------------------------
			// 6) PassCtx ShadowView 갱신
			// ----------------------------
			m_PassCtx.ShadowView.PrevViewMatrix = m_PassCtx.ShadowView.ViewMatrix;
			m_PassCtx.ShadowView.PrevProjMatrix = m_PassCtx.ShadowView.ProjMatrix;
			m_PassCtx.ShadowView.PrevViewProjMatrix = m_PassCtx.ShadowView.ViewProjMatrix;

			m_PassCtx.ShadowView.CameraPosition = float3(0.0f, 0.0f, 0.0f);
			m_PassCtx.ShadowView.ViewMatrix = lightView;
			m_PassCtx.ShadowView.ProjMatrix = lightProj;
			m_PassCtx.ShadowView.ViewProjMatrix = lightViewProj;

			m_PassCtx.ShadowView.FieldOfViewY = 0.0f;
			m_PassCtx.ShadowView.AspectRatio = 1.0f;

			m_PassCtx.ShadowView.Viewport =
			{
				0, 0,
				static_cast<int32>(m_pShadowSystem->GetResolution()),
				static_cast<int32>(m_pShadowSystem->GetResolution())
			};

			m_PassCtx.ShadowView.NearPlane = nearZ;
			m_PassCtx.ShadowView.FarPlane = farZ;

			m_PassCtx.ShadowView.bOrthographic = true;
			m_PassCtx.ShadowView.OrthographicSize = (maxX - minX);

		}

		m_pShadowSystem->UpdateShadowMatrices(*this, view, lightDirWs);

		// ------------------------------------------------------------
		// Shadow frustum (for culling etc.)
		// ------------------------------------------------------------
		ViewFrustumExt frustumShadow = {};
		ExtractViewFrustumPlanesFromMatrix(lightViewProj, frustumShadow);
		m_PassCtx.ShadowViewFrustum = frustumShadow;

		// ------------------------------------------------------------
		// Common barriers
		// ------------------------------------------------------------
		pushBarrier(pFrameCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		pushBarrier(pViewCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		pushBarrier(pDrawCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

		pushBarrier(pBasePassObjectTable, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pForwardPassObjectTable, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pShadowPassObjectTable, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		pushBarrier(pEnvTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pEnvDiffTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pEnvSpecTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pEnvBrdfTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		pushBarrier(pErrorTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		// Apply pending buffer updates
		for (const BufferUpdateDesc& bud : m_PendingBufferUpdates)
		{
			IBuffer* pBuf = m_pRegistry->GetBuffer(bud.ResourceId);
			ASSERT(pBuf, "Buffer not found for ResourceId=%llu", (unsigned long long)bud.ResourceId);

			const auto& desc = pBuf->GetDesc();
			ASSERT(desc.Size >= bud.Data.size(), "Update size exceeds buffer size.");

			ASSERT(bud.Data.size() <= std::numeric_limits<uint32>::max(), "Update too large.");
			const uint32 updateSize = static_cast<uint32>(bud.Data.size());

			const bool bCpuWritableDynamic = (desc.Usage == USAGE_DYNAMIC) && ((desc.CPUAccessFlags & CPU_ACCESS_WRITE) != 0);

			if (bCpuWritableDynamic)
			{
				void* pData = nullptr;
				ctx->MapBuffer(pBuf, MAP_WRITE, MAP_FLAG_DISCARD, pData);
				ASSERT(pData, "MapBuffer returned null.");
				std::memcpy(pData, bud.Data.data(), bud.Data.size());
				ctx->UnmapBuffer(pBuf, MAP_WRITE);
			}
			else
			{
				ASSERT(desc.Usage == USAGE_DEFAULT || desc.Usage == USAGE_SPARSE,
					"Unable to update buffer '%s': only USAGE_DEFAULT/USAGE_SPARSE can use UpdateBuffer(). "
					"For USAGE_DYNAMIC use MapBuffer().",
					desc.Name ? desc.Name : "(null)");

				ctx->UpdateBuffer(
					pBuf,
					0,
					updateSize,
					bud.Data.data(),
					RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			}
		}
		m_PendingBufferUpdates.clear();

		// ------------------------------------------------------------
		// Helper: pack object table using instanceRemap
		// ------------------------------------------------------------
		auto packObjectTableFromRemap = [&](IBuffer* pObjectTableSB, const std::vector<uint32>& remap)
			{
				ASSERT(pObjectTableSB, "ObjectTableSB is null.");
				const std::vector<hlsl::ObjectConstants>& tableCPU = scene.GetObjectConstantsTableCPU();

				MapHelper<hlsl::ObjectConstants> map(ctx, pObjectTableSB, MAP_WRITE, MAP_FLAG_DISCARD);
				hlsl::ObjectConstants* dst = map;

				for (size_t i = 0; i < remap.size(); ++i)
				{
					const uint32 oc = remap[i];
					ASSERT(oc < static_cast<uint32>(tableCPU.size()), "OcIndex OOB.");
					dst[i] = tableCPU[oc];
				}
			};

		// ------------------------------------------------------------
		// Build packets + pack object tables
		// (Visibility + LOD is now inside RenderScene::BuildDrawPackets)
		// ------------------------------------------------------------
		std::vector<uint32> instanceRemap;

		auto pipelineResolver = [this](MaterialId matId, uint64 rpKey) -> const MaterialPipelineBinding&
			{
				return this->AcquireMaterialPipelineBinding(matId, rpKey);
			};

		View shadowView = m_PassCtx.ShadowView;

		// GBuffer
		scene.BuildDrawPackets(
			STRING_HASH("GBuffer"),
			view,
			view,
			pipelineResolver,
			m_PassCtx.MainDrawPackets,
			instanceRemap);

		packObjectTableFromRemap(pBasePassObjectTable, instanceRemap);

		// Forward
		scene.BuildDrawPackets(
			STRING_HASH("Forward"),
			view,
			view,
			pipelineResolver,
			m_PassCtx.ForwardDrawPackets,
			instanceRemap);

		// Depth prepass
		scene.BuildDrawPackets(
			STRING_HASH("DepthPrepass"),
			view,
			view,
			pipelineResolver,
			m_PassCtx.DepthPrepassDrawPackets,
			instanceRemap);

		packObjectTableFromRemap(pForwardPassObjectTable, instanceRemap);

		// Shadow
		scene.BuildDrawPackets(
			STRING_HASH("Shadow"),
			m_PassCtx.ShadowView,
			view,
			pipelineResolver,
			m_PassCtx.ShadowDrawPackets,
			instanceRemap);

		packObjectTableFromRemap(pShadowPassObjectTable, instanceRemap);

		scene.BuildIndirectDrawPackets(STRING_HASH("GBuffer"), pipelineResolver, m_PassCtx.MainIndirectPackets);
		scene.BuildIndirectDrawPackets(STRING_HASH("Forward"), pipelineResolver, m_PassCtx.ForwardIndirectPackets);
		scene.BuildIndirectDrawPackets(STRING_HASH("Shadow"), pipelineResolver, m_PassCtx.ShadowIndirectPackets);
		scene.BuildIndirectDrawPackets(STRING_HASH("DepthPrepass"), pipelineResolver, m_PassCtx.DepthPrepassIndirectDrawPackets);

		IBuffer* pIndirectArgs = m_pRegistry->GetBuffer(STRING_HASH("IndirectArgsBuffer"));
		IBuffer* pIndirectCounters = m_pRegistry->GetBuffer(STRING_HASH("IndirectDrawCountBuffer"));
		ASSERT(pIndirectArgs, "IndirectArgs buffer missing.");

		auto patchIndirectPackets = [&](std::vector<DrawIndirectPacket>& packets)
			{
				for (DrawIndirectPacket& p : packets)
				{
					p.DrawAttribs.pAttribsBuffer = pIndirectArgs;
					p.DrawAttribs.pCounterBuffer = pIndirectCounters;
				}
			};
		patchIndirectPackets(m_PassCtx.MainIndirectPackets);
		patchIndirectPackets(m_PassCtx.ForwardIndirectPackets);
		patchIndirectPackets(m_PassCtx.ShadowIndirectPackets);
		patchIndirectPackets(m_PassCtx.DepthPrepassIndirectDrawPackets);

		// ------------------------------------------------------------
		// Pending transitions
		// ------------------------------------------------------------
		if (!m_PendingBarriers.empty())
		{
			std::vector<StateTransitionDesc> descs;
			descs.reserve(m_PendingBarriers.size());
			for (auto& pb : m_PendingBarriers) descs.push_back(pb.Desc);

			ctx->TransitionResourceStates((uint32)descs.size(), descs.data());
		}
		m_PendingBarriers.clear();

		// ------------------------------------------------------------
		// Execute passes
		// ------------------------------------------------------------
		std::vector<StateTransitionDesc> barriers;

		for (uint64 passId : m_CompiledPassOrder)
		{
			ASSERT(passId != 0, "Invalid pass ID.");
			RenderPassItem& pass = m_PassTable[passId];

			buildTransitionsForPass(passId, barriers);
			if (!barriers.empty())
			{
				ctx->TransitionResourceStates(static_cast<uint32>(barriers.size()), barriers.data());
			}

			if (m_bViewCBDirty)
			{
				// Reset to main view
				UpdateViewConstantBuffer(view);
			}

			if (pass.eDomain == EPassExecutionDomain::RenderPass && pass.pRHIRenderpass)
			{
				m_PassCtx.pRHIRenderPass = pass.pRHIRenderpass;

				BeginRenderPassAttribs rp = {};
				rp.pRenderPass = pass.pRHIRenderpass;
				rp.pFramebuffer = !pass.bUseSwapChainBackBuffer ? pass.pRHIFramebuffer : m_pSwapChainFramebuffer;
				rp.ClearValueCount = static_cast<uint32>(pass.ClearValues.size());
				rp.pClearValues = pass.ClearValues.empty() ? nullptr : pass.ClearValues.data();

				if (pass.BeginLambda)
				{
					pass.BeginLambda(m_PassCtx);
				}

				ctx->BeginRenderPass(rp);
				pass.ExecuteLambda(m_PassCtx);
				ctx->EndRenderPass();

				if (pass.EndLambda)
				{
					pass.EndLambda(m_PassCtx);
				}
			}
			else
			{
				if (pass.BeginLambda)
				{
					pass.BeginLambda(m_PassCtx);
				}

				pass.ExecuteLambda(m_PassCtx);

				if (pass.EndLambda)
				{
					pass.EndLambda(m_PassCtx);
				}
			}
		}
	}

	void Renderer::EndFrame()
	{
	}

	void Renderer::ReleaseSwapChainBuffers()
	{

	}

	void Renderer::OnResize(uint32 width, uint32 height)
	{
		ASSERT(width != 0 && height != 0, "Invalid size.");
		m_Width = width;
		m_Height = height;

		m_bRenderGraphDirty = true;
	}

	// ---------------------------------------------------------------------
	// Render pass management
	// ---------------------------------------------------------------------

	void Renderer::AddPass(
		const std::string& name,
		EPassExecutionDomain domain,
		std::function<void(RenderPassBuilder&)> buildLambda,
		std::function<void(RenderPassContext&)> executeLambda,
		std::function<void()> onCreated)
	{
		AddPass(name, domain, buildLambda, executeLambda, {}, {}, onCreated);
	}

	void Renderer::AddPass(
		const std::string& name,
		EPassExecutionDomain domain,
		std::function<void(RenderPassBuilder&)> buildLambda,
		std::function<void(RenderPassContext&)> executeLambda,
		std::function<void(RenderPassContext&)> beginLambda,
		std::function<void(RenderPassContext&)> endLambda,
		std::function<void()> onCreated)
	{
		ASSERT(!name.empty(), "Pass name is empty.");
		ASSERT(buildLambda, "buildLambda is null.");
		ASSERT(executeLambda, "executeLambda is null.");
		ASSERT(m_pDevice, "Device is null.");
		ASSERT(m_pRegistry, "Registry is null.");

		const uint64 passId = STRING_HASH(name.c_str());
		ASSERT(m_PassTable.find(passId) == m_PassTable.end(), "%s pass already exist.", name.c_str());

		RenderPassItem rpItem = {};
		rpItem.Name = name;
		rpItem.eDomain = domain;
		rpItem.ExecuteLambda = std::move(executeLambda);

		rpItem.BeginLambda = beginLambda;
		rpItem.EndLambda = endLambda;

		RenderPassBuilder builder = {};
		buildLambda(builder);

		rpItem.ResourceAccess.swap(builder.DeclaredAccesses);

		// -----------------------------------------------------------------
		// Helpers
		// -----------------------------------------------------------------
		auto isWriteAccess = [](const RenderPassResourceAccess& a) -> bool
			{
				if (a.Access == RENDER_ACCESS_WRITE || a.Access == RENDER_ACCESS_READWRITE) return true;
				if (a.Usage == RENDER_USAGE_RTV || a.Usage == RENDER_USAGE_DSV_WRITE || a.Usage == RENDER_USAGE_UAV) return true;
				return false;
			};

		auto hasClearValue = [&](uint64 resourceId) -> bool
			{
				return builder.ClearValues.find(resourceId) != builder.ClearValues.end();
			};

		auto findClearValue = [&](uint64 resourceId, bool bDepth) -> OptimizedClearValue
			{
				OptimizedClearValue cv = {};
				if (auto it = builder.ClearValues.find(resourceId); it != builder.ClearValues.end())
				{
					cv = it->second;
				}
				else
				{
					// default: black / depth=1
					if (bDepth)
					{
						cv.DepthStencil.Depth = 1.f;
						cv.DepthStencil.Stencil = 0;
					}
					else
					{
						cv.Color[0] = 0.f;
						cv.Color[1] = 0.f;
						cv.Color[2] = 0.f;
						cv.Color[3] = 0.f;
					}
				}
				return cv;
			};

		// -----------------------------------------------------------------
		// Build RHI RenderPass + Framebuffer attachments
		// (attachment order == framebuffer order == clearvalue order)
		// -----------------------------------------------------------------
		std::vector<RenderPassAttachmentDesc> attachments;
		std::vector<AttachmentReference>      colorRefs;

		AttachmentReference depthRef = {};
		bool bHasDepth = false;

		rpItem.ClearValues.clear();
		rpItem.StaticFBAttachments.clear();

		attachments.reserve(8);
		colorRefs.reserve(8);
		rpItem.ClearValues.reserve(8);
		rpItem.StaticFBAttachments.reserve(8);

		for (const RenderPassResourceAccess& a : rpItem.ResourceAccess)
		{
			const bool bWrite = isWriteAccess(a);

			// -------------------------------------------------------------
			// Texture RTV write
			// -------------------------------------------------------------
			if (a.Kind == RENDER_RESOURCE_KIND_TEXTURE &&
				bWrite &&
				a.Usage == RENDER_USAGE_RTV &&
				a.TextureViewType == TEXTURE_VIEW_RENDER_TARGET)
			{
				ITexture* pTex = m_pRegistry->GetTexture(a.ResourceId);
				ASSERT(pTex, "RTV texture not found.");

				ITextureView* pRTV = m_pRegistry->GetTextureRTV(a.ResourceId);
				ASSERT(pRTV, "RTV view not found.");

				const TextureDesc& td = pTex->GetDesc();
				const TextureViewDesc& vd = pRTV->GetDesc();

				const bool bClear = hasClearValue(a.ResourceId);

				RenderPassAttachmentDesc at = {};
				at.Format = vd.Format; // View format
				at.SampleCount = td.SampleCount;

				at.LoadOp = bClear ? ATTACHMENT_LOAD_OP_CLEAR : ATTACHMENT_LOAD_OP_LOAD;
				at.StoreOp = ATTACHMENT_STORE_OP_STORE;

				// Color attachment는 stencil 의미 없지만 안전하게 discard
				at.StencilLoadOp = ATTACHMENT_LOAD_OP_DISCARD;
				at.StencilStoreOp = ATTACHMENT_STORE_OP_DISCARD;

				at.InitialState = RESOURCE_STATE_RENDER_TARGET;
				at.FinalState = RESOURCE_STATE_RENDER_TARGET;

				attachments.emplace_back(at);

				AttachmentReference cr = {};
				cr.AttachmentIndex = static_cast<uint32>(attachments.size() - 1);
				cr.State = RESOURCE_STATE_RENDER_TARGET;
				colorRefs.emplace_back(cr);

				rpItem.StaticFBAttachments.emplace_back(pRTV);

				rpItem.ClearValues.emplace_back(findClearValue(a.ResourceId, /*bDepth*/false));
			}
			// -------------------------------------------------------------
			// Texture DSV write (single depth)
			// -------------------------------------------------------------
			else if (a.Kind == RENDER_RESOURCE_KIND_TEXTURE &&
				(a.Usage == RENDER_USAGE_DSV_WRITE || a.Usage == RENDER_USAGE_DSV_READ) &&
				a.TextureViewType == TEXTURE_VIEW_DEPTH_STENCIL)
			{
				ASSERT(!bHasDepth, "Multiple depth attachments are not supported yet.");

				ITexture* pTex = nullptr;
				ITextureView* pDSV = nullptr;

				// NEW: a.ResourceId는 "textureId"일 수도 있고, "viewId"일 수도 있다.
				// 1) viewId 경로 (CSM slice DSV)
				if (m_pRegistry->HasTextureView(a.ResourceId))
				{
					pDSV = m_pRegistry->GetTextureDSVView(a.ResourceId);
					ASSERT(pDSV, "DSV view not found (named view).");

					// 뷰에서 원본 텍스처를 얻는다
					pTex = pDSV->GetTexture();
					ASSERT(pTex, "DSV view has no texture.");
				}
				else
				{
					// 2) 기존 호환: textureId 경로 (default DSV)
					pTex = m_pRegistry->GetTexture(a.ResourceId);
					ASSERT(pTex, "DSV texture not found.");

					pDSV = m_pRegistry->GetTextureDSV(a.ResourceId);
					ASSERT(pDSV, "DSV view not found.");
				}

				const TextureDesc& td = pTex->GetDesc();
				const TextureViewDesc& vd = pDSV->GetDesc();

				const bool bClear = hasClearValue(a.ResourceId);

				RenderPassAttachmentDesc at = {};
				at.Format = vd.Format;
				at.SampleCount = td.SampleCount;

				at.LoadOp = bClear ? ATTACHMENT_LOAD_OP_CLEAR : ATTACHMENT_LOAD_OP_LOAD;
				at.StoreOp = ATTACHMENT_STORE_OP_STORE;

				at.StencilLoadOp = bClear ? ATTACHMENT_LOAD_OP_CLEAR : ATTACHMENT_LOAD_OP_LOAD;
				at.StencilStoreOp = ATTACHMENT_STORE_OP_STORE;

				at.InitialState = RESOURCE_STATE_DEPTH_WRITE;
				at.FinalState = RESOURCE_STATE_DEPTH_WRITE;

				attachments.emplace_back(at);

				depthRef = {};
				depthRef.AttachmentIndex = static_cast<uint32>(attachments.size() - 1);
				depthRef.State = RESOURCE_STATE_DEPTH_WRITE;

				rpItem.StaticFBAttachments.emplace_back(pDSV);
				rpItem.ClearValues.emplace_back(findClearValue(a.ResourceId, /*bDepth*/true));

				bHasDepth = true;
			}
			// -------------------------------------------------------------
			// SwapChain backbuffer RTV write (EXTERNAL)
			// -------------------------------------------------------------
			else if (a.Kind == RENDER_RESOURCE_KIND_EXTERNAL &&
				bWrite &&
				a.ResourceId == STRING_HASH("SwapChain.BackBuffer") &&
				a.Usage == RENDER_USAGE_RTV)
			{
				ASSERT(m_pSwapChain, "SwapChain is null.");

				const SwapChainDesc& scDesc = m_pSwapChain->GetDesc();
				const bool bClear = hasClearValue(a.ResourceId);

				RenderPassAttachmentDesc at = {};
				at.Format = scDesc.ColorBufferFormat;
				at.SampleCount = 1;

				at.LoadOp = bClear ? ATTACHMENT_LOAD_OP_CLEAR : ATTACHMENT_LOAD_OP_LOAD;
				at.StoreOp = ATTACHMENT_STORE_OP_STORE;

				at.StencilLoadOp = ATTACHMENT_LOAD_OP_DISCARD;
				at.StencilStoreOp = ATTACHMENT_STORE_OP_DISCARD;

				at.InitialState = RESOURCE_STATE_RENDER_TARGET;
				at.FinalState = RESOURCE_STATE_RENDER_TARGET;

				attachments.emplace_back(at);

				AttachmentReference cr = {};
				cr.AttachmentIndex = static_cast<uint32>(attachments.size() - 1);
				cr.State = RESOURCE_STATE_RENDER_TARGET;
				colorRefs.emplace_back(cr);

				// Framebuffer attachment slot reserved for BB, filled later
				rpItem.bUseSwapChainBackBuffer = true;
				rpItem.StaticFBAttachments.emplace_back(nullptr);

				rpItem.ClearValues.emplace_back(findClearValue(a.ResourceId, /*bDepth*/false));
			}
		}

		// -----------------------------------------------------------------
		// Create RenderPass / Framebuffer
		// -----------------------------------------------------------------
		if (!attachments.empty())
		{
			SubpassDesc subpass = {};
			subpass.RenderTargetAttachmentCount = static_cast<uint32>(colorRefs.size());
			subpass.pRenderTargetAttachments = colorRefs.empty() ? nullptr : colorRefs.data();
			subpass.pDepthStencilAttachment = bHasDepth ? &depthRef : nullptr;

			RenderPassDesc rpDesc = {};
			rpDesc.Name = rpItem.Name.c_str();
			rpDesc.AttachmentCount = static_cast<uint32>(attachments.size());
			rpDesc.pAttachments = attachments.data();
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			m_pDevice->CreateRenderPass(rpDesc, &rpItem.pRHIRenderpass);
			ASSERT(rpItem.pRHIRenderpass, "CreateRenderPass failed.");

			// ClearValues must match attachment count
			ASSERT(rpItem.ClearValues.size() == attachments.size(), "ClearValues mismatch.");

			// Framebuffer:
			// - If it uses swapchain BB, we cannot finalize FB here (need current BB view)
			// - Otherwise create once here.
			if (!rpItem.bUseSwapChainBackBuffer)
			{
				FramebufferDesc fbDesc = {};
				fbDesc.Name = (std::string("FB_") + rpItem.Name).c_str();
				fbDesc.pRenderPass = rpItem.pRHIRenderpass;
				fbDesc.AttachmentCount = static_cast<uint32>(rpItem.StaticFBAttachments.size());
				fbDesc.ppAttachments = rpItem.StaticFBAttachments.data();

				m_pDevice->CreateFramebuffer(fbDesc, &rpItem.pRHIFramebuffer);
				ASSERT(rpItem.pRHIFramebuffer, "CreateFramebuffer failed.");
			}
			else
			{
				// present pass renderpass 기억
				m_pPresentRenderPass = rpItem.pRHIRenderpass;
			}
		}
		else
		{
			// compute-only / no RP
			rpItem.ClearValues.clear();
			rpItem.StaticFBAttachments.clear();
			rpItem.bUseSwapChainBackBuffer = false;
		}

		// -----------------------------------------------------------------
		// Store & mark dirty
		// -----------------------------------------------------------------
		m_PassTable.emplace(passId, std::move(rpItem));
		m_PassAddOrder.emplace_back(passId);
		m_bRenderGraphDirty = true;

		m_PassNameTable[passId] = name;

		// Important: call onCreated after stored so AcquirePipelineState(passId, ...)
		// can fetch rpItem.pRHIRenderpass.
		if (onCreated)
		{
			onCreated();
		}
	}

	// ---------------------------------------------------------------------
	// Resource wrappers
	// ---------------------------------------------------------------------

	RefCntAutoPtr<ITexture> Renderer::CreateTexture(const TextureDesc& desc, const TextureData* pInitData)
	{
		ASSERT(m_pDevice, "Device is null.");
		RefCntAutoPtr<ITexture> tex;
		m_pDevice->CreateTexture(desc, pInitData, &tex);
		return tex;
	}

	RefCntAutoPtr<ITexture> Renderer::CreateTexture(const AssetRef<Texture>& assetRef)
	{
		uint64 key = std::hash<AssetID>{}(assetRef.GetID());
		if (m_pRegistry->HasTexture(key))
		{
			return m_pRegistry->GetTexture(key);
		}

		AssetPtr<Texture> assetPtr = m_pAssetManager->Acquire(assetRef);
		ASSERT(assetPtr, "Failed to acquire TextureAsset.");

		return CreateTexture(key, *assetPtr);
	}

	RefCntAutoPtr<ITexture> Renderer::CreateTexture(const std::string& name, const Texture& texture)
	{
		return CreateTexture(STRING_HASH(name), texture);
	}

	RefCntAutoPtr<ITexture> Renderer::CreateTexture(uint64 id, const Texture& texture)
	{
		const auto& mips = texture.GetMips();
		ASSERT(!mips.empty(), "TextureAsset has no mips.");

		const uint32 width = mips[0].Width;
		const uint32 height = mips[0].Height;

		TextureDesc desc = {};
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = static_cast<uint32>(mips.size());
		desc.ArraySize = 1;
		desc.Format = texture.GetFormat();
		desc.Usage = USAGE_DEFAULT;
		desc.BindFlags = BIND_SHADER_RESOURCE;

		std::vector<TextureSubResData> subres;
		subres.resize(mips.size());

		for (size_t i = 0; i < mips.size(); ++i)
		{
			const TextureMip& mip = mips[i];
			TextureSubResData sr = {};
			sr.pData = mip.Data.data();
			sr.Stride = static_cast<uint64>(mip.Width) * GetTextureFormatAttribs(desc.Format).GetElementSize();
			sr.DepthStride = 0;
			subres[i] = sr;
		}

		TextureData initData = {};
		initData.pSubResources = subres.data();
		initData.NumSubresources = static_cast<uint32>(subres.size());

		m_pRegistry->RegisterTexture(id, CreateTexture(desc, &initData));
		return m_pRegistry->GetTexture(id);
	}

	void Renderer::UpdateTexture2D(
		IDeviceContext* pCtx,
		ITexture* pTexture,
		uint32 arraySlice,
		const Texture& sourceImage,
		RESOURCE_STATE_TRANSITION_MODE transitionMode) const
	{
		ASSERT(pCtx, "Context is null.");
		ASSERT(pTexture, "Texture is null.");

		const auto& mips = sourceImage.GetMips();
		ASSERT(!mips.empty(), "TextureAsset has no mips.");

		for (uint32 mipLevel = 0; mipLevel < static_cast<uint32>(mips.size()); ++mipLevel)
		{
			TextureSubResData subResData = {};
			subResData.pData = mips[mipLevel].Data.data();
			subResData.Stride = static_cast<uint64>(mips[mipLevel].Width) * GetTextureFormatAttribs(sourceImage.GetFormat()).GetElementSize();
			subResData.DepthStride = 0;

			// Update entire subresource (no box)
			IBox box = {}; // empty -> full resource
			pCtx->UpdateTexture(
				pTexture,
				mipLevel,
				arraySlice,
				box,
				subResData,
				transitionMode,
				RESOURCE_STATE_TRANSITION_MODE_NONE
			);
		}
	}

	RefCntAutoPtr<IBuffer> Renderer::CreateBuffer(const BufferDesc& desc, const BufferData* pInitData)
	{
		ASSERT(m_pDevice, "Device is null.");
		RefCntAutoPtr<IBuffer> buf;
		m_pDevice->CreateBuffer(desc, pInitData, &buf);
		return buf;
	}

	RefCntAutoPtr<IBuffer> Renderer::CreateVertexBuffer(const BufferDesc& desc, const BufferData* pInitData)
	{
		ASSERT(m_pDevice, "Device is null.");
		RefCntAutoPtr<IBuffer> buf;
		m_pDevice->CreateBuffer(desc, pInitData, &buf);
		pushBarrier(buf, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
		return buf;
	}

	RefCntAutoPtr<IBuffer> Renderer::CreateIndexBuffer(const BufferDesc& desc, const BufferData* pInitData)
	{
		ASSERT(m_pDevice, "Device is null.");
		RefCntAutoPtr<IBuffer> buf;
		m_pDevice->CreateBuffer(desc, pInitData, &buf);
		pushBarrier(buf, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);
		return buf;
	}

	// ---------------------------------------------------------------------
	// Resource registry wrappers
	// ---------------------------------------------------------------------

	RefCntAutoPtr<ITexture> Renderer::GetTexture(uint64 id) const
	{
		ASSERT(m_pRegistry, "RenderResourceRegistry is null. Initialize renderer first.");
		return m_pRegistry->GetTexture(id);
	}

	RefCntAutoPtr<ITextureView> Renderer::GetTextureSRV(uint64 id) const
	{
		ASSERT(m_pRegistry, "RenderResourceRegistry is null. Initialize renderer first.");
		return m_pRegistry->GetTextureSRV(id);
	}

	RefCntAutoPtr<ITextureView> Renderer::GetTextureRTV(uint64 id) const
	{
		ASSERT(m_pRegistry, "RenderResourceRegistry is null. Initialize renderer first.");
		return m_pRegistry->GetTextureRTV(id);
	}

	RefCntAutoPtr<ITextureView> Renderer::GetTextureDSV(uint64 id) const
	{
		ASSERT(m_pRegistry, "RenderResourceRegistry is null. Initialize renderer first.");
		return m_pRegistry->GetTextureDSV(id);
	}

	RefCntAutoPtr<ITextureView> Renderer::GetTextureUAV(uint64 id) const
	{
		ASSERT(m_pRegistry, "RenderResourceRegistry is null. Initialize renderer first.");
		return m_pRegistry->GetTextureUAV(id);
	}

	RefCntAutoPtr<IBuffer> Renderer::GetBuffer(uint64 id) const
	{
		ASSERT(m_pRegistry, "RenderResourceRegistry is null. Initialize renderer first.");
		return m_pRegistry->GetBuffer(id);
	}

	RefCntAutoPtr<IBufferView> Renderer::GetBufferSRV(uint64 id) const
	{
		ASSERT(m_pRegistry, "RenderResourceRegistry is null. Initialize renderer first.");
		return m_pRegistry->GetBufferSRV(id);
	}

	RefCntAutoPtr<IBufferView> Renderer::GetBufferUAV(uint64 id) const
	{
		ASSERT(m_pRegistry, "RenderResourceRegistry is null. Initialize renderer first.");
		return m_pRegistry->GetBufferUAV(id);
	}

	uint64 Renderer::AddTexture(const std::string& name, const TextureDesc& desc, const TextureData* pInitData)
	{
		ASSERT(!name.empty(), "Name is empty.");
		return AddTexture(STRING_HASH(name), desc, pInitData);
	}

	uint64 Renderer::AddTexture(uint64 id, const TextureDesc& desc, const TextureData* pInitData)
	{
		ASSERT(m_pRegistry, "Registry is null.");
		RefCntAutoPtr<ITexture> tex = CreateTexture(desc, pInitData);
		ASSERT(tex, "AddTexture: CreateTexture failed.");
		m_pRegistry->RegisterTexture(id, std::move(tex));
		return id;
	}

	uint64 Renderer::AddTexture(const std::string& name, RefCntAutoPtr<ITexture>&& tex)
	{
		ASSERT(!name.empty(), "Name is empty.");
		return AddTexture(STRING_HASH(name), std::move(tex));
	}

	uint64 Renderer::AddTexture(uint64 id, RefCntAutoPtr<ITexture>&& tex)
	{
		ASSERT(m_pRegistry, "Registry is null.");
		ASSERT(tex, "AddTexture: tex is null.");
		m_pRegistry->RegisterTexture(id, std::move(tex));
		return id;
	}

	void Renderer::AddTextureView(const std::string& textureName, const TextureViewDesc& viewDesc)
	{
		ASSERT(m_pRegistry, "Registry is null.");
		ASSERT(!textureName.empty(), "Name is empty.");
		AddTextureView(STRING_HASH(textureName), viewDesc);
	}

	void Renderer::AddTextureView(uint64 textureId, const TextureViewDesc& viewDesc)
	{
		ASSERT(m_pRegistry, "Registry is null.");
		m_pRegistry->CreateTextureView(textureId, viewDesc);
	}

	void Renderer::AddTextureView(const std::string& textureName, const std::string& viewName, const TextureViewDesc& viewDesc)
	{
		ASSERT(m_pRegistry, "Registry is null.");
		ASSERT(!textureName.empty(), "Texture name is empty.");
		ASSERT(!viewName.empty(), "View name is empty.");
		AddTextureView(STRING_HASH(textureName), STRING_HASH(viewName), viewDesc);
	}

	void Renderer::AddTextureView(uint64 textureId, uint64 viewId, const TextureViewDesc& viewDesc)
	{
		ASSERT(m_pRegistry, "Registry is null.");
		m_pRegistry->CreateTextureView(textureId, viewId, viewDesc);
	}

	uint64 Renderer::AddBuffer(const std::string& name, const BufferDesc& desc, const BufferData* pInitData)
	{
		ASSERT(!name.empty(), "Name is empty.");
		return AddBuffer(STRING_HASH(name), desc, pInitData);
	}

	uint64 Renderer::AddBuffer(uint64 id, const BufferDesc& desc, const BufferData* pInitData)
	{
		ASSERT(m_pRegistry, "Registry is null.");
		RefCntAutoPtr<IBuffer> buf = CreateBuffer(desc, pInitData);
		ASSERT(buf, "AddBuffer: CreateBuffer failed.");
		m_pRegistry->RegisterBuffer(id, std::move(buf));
		return id;
	}

	uint64 Renderer::AddBuffer(const std::string& name, RefCntAutoPtr<IBuffer>&& buf)
	{
		ASSERT(!name.empty(), "Name is empty.");
		return AddBuffer(STRING_HASH(name), std::move(buf));
	}

	uint64 Renderer::AddBuffer(uint64 id, RefCntAutoPtr<IBuffer>&& buf)
	{
		ASSERT(m_pRegistry, "Registry is null.");
		ASSERT(buf, "AddBuffer: buf is null.");
		m_pRegistry->RegisterBuffer(id, std::move(buf));
		return id;
	}

	uint64 Renderer::AddUniformBuffer(const std::string& name, uint64 sizeBytes)
	{
		ASSERT(!name.empty(), "Name is empty.");
		RefCntAutoPtr<IBuffer> buf;
		CreateUniformBuffer(m_pDevice, sizeBytes, name.c_str(), &buf);
		ASSERT(buf, "AddUniformBuffer: CreateUniformBuffer failed.");

		uint64 id = STRING_HASH(name);
		m_pRegistry->RegisterBuffer(id, std::move(buf));
		return id;
	}

	uint64 Renderer::AddUniformBuffer(uint64 id, uint64 sizeBytes)
	{
		RefCntAutoPtr<IBuffer> buf;
		CreateUniformBuffer(m_pDevice, sizeBytes, "UnnamedBuffer", &buf);
		ASSERT(buf, "AddUniformBuffer: CreateUniformBuffer failed.");

		m_pRegistry->RegisterBuffer(id, std::move(buf));
		return id;
	}

	void Renderer::AddResourceAlias(uint64 src, uint64 alias)
	{
		m_pRegistry->AddAlias(src, alias);
	}

	void Renderer::RemoveResourceAlias(uint64 alias)
	{
		m_pRegistry->RemoveAlias(alias);
	}

	void Renderer::RegisterStaticTextureResource(const std::string& name, RenderResourceId id)
	{
		ASSERT(m_pRegistry->HasTexture(id), "Unnkown resource.");
		m_pPipelineStateManager->RegisterStaticTextureResource(name, id);
	}

	void Renderer::RegisterStaticBufferCBV(const std::string& name, RenderResourceId id)
	{
		ASSERT(m_pRegistry->HasBuffer(id), "Unnkown resource.");
		m_pPipelineStateManager->RegisterStaticBufferCBV(name, id);
	}

	void Renderer::RegisterStaticBufferSRV(const std::string& name, RenderResourceId id)
	{
		ASSERT(m_pRegistry->HasBuffer(id), "Unnkown resource.");
		m_pPipelineStateManager->RegisterStaticBufferSRV(name, id);
	}

	void Renderer::RegisterStaticBufferUAV(const std::string& name, RenderResourceId id)
	{
		ASSERT(m_pRegistry->HasBuffer(id), "Unnkown resource.");
		m_pPipelineStateManager->RegisterStaticBufferUAV(name, id);
	}

	// ---------------------------------------------------------------------
	// RenderData 
	// ---------------------------------------------------------------------

	const StaticMeshRenderData& Renderer::CreateStaticMeshRenderData(const AssetRef<StaticMesh>& assetRef, const std::string& name)
	{
		uint64 key = std::hash<AssetID>{}(assetRef.GetID());

		if (m_StaticMeshCache.Contains(key))
		{
			return m_StaticMeshCache.Acquire(key);
		}

		AssetPtr<StaticMesh> assetPtr = m_pAssetManager->Acquire(assetRef);
		ASSERT(assetPtr, "Failed to acquire StaticMeshAsset.");

		if (name == "")
		{
			return CreateStaticMeshRenderData(*assetPtr, key, assetPtr.GetSourcePath());
		}
		else
		{
			return CreateStaticMeshRenderData(*assetPtr, key, name);
		}
	}

	const StaticMeshRenderData& Renderer::CreateStaticMeshRenderData(const StaticMesh& mesh, uint64 key, const std::string& name)
	{
		if (key == 0)
		{
			key = std::rand(); // TODO: better hash or REMOVE CreateStaticMesh overload
		}

		StaticMeshRenderData out = {};

		struct PackedStaticVertex final
		{
			float3 Pos;
			float2 UV;
			float3 Normal;
			float3 Tangent;
		};

		for (const auto& level : mesh.GetLevels())
		{

			std::vector<PackedStaticVertex> packed;
			// Build packed vertex buffer data
			{
				const uint32 vtxCount = level.GetVertexCount();
				packed.resize(vtxCount);

				const std::vector<float3>& positions = level.GetPositions();
				const std::vector<float3>& normals = level.GetNormals();
				const std::vector<float3>& tangents = level.GetTangents();
				const std::vector<float2>& texCoords = level.GetTexCoords();

				const bool bHasNormals = (!normals.empty() && normals.size() == positions.size());
				const bool bHasTangents = (!tangents.empty() && tangents.size() == positions.size());
				const bool bHasUV = (!texCoords.empty() && texCoords.size() == positions.size());

				for (uint32 i = 0; i < vtxCount; ++i)
				{
					PackedStaticVertex v{};
					v.Pos = positions[i];
					v.Normal = bHasNormals ? normals[i] : float3(0.0f, 1.0f, 0.0f);
					v.Tangent = bHasTangents ? tangents[i] : float3(1.0f, 0.0f, 0.0f);
					v.UV = bHasUV ? texCoords[i] : float2(0.0f, 0.0f);
					packed[i] = v;
				}
			}

			auto createImmutableBuffer = [](IRenderDevice* device, const char* name, BIND_FLAGS bindFlags, const void* pData, uint32 dataSize) -> RefCntAutoPtr<IBuffer>
				{
					BufferDesc desc = {};
					desc.Name = name;
					desc.Size = dataSize;
					desc.Usage = USAGE_IMMUTABLE;
					desc.BindFlags = bindFlags;
					BufferData initData = {};
					initData.pData = pData;
					initData.DataSize = dataSize;
					RefCntAutoPtr<IBuffer> pBuffer;
					device->CreateBuffer(desc, &initData, &pBuffer);
					return pBuffer;
				};

			const uint32 vbBytes = static_cast<uint32>(packed.size() * sizeof(PackedStaticVertex));
			RefCntAutoPtr<IBuffer> pVB = createImmutableBuffer(m_pDevice, "StaticMesh_VB", BIND_VERTEX_BUFFER, packed.data(), vbBytes);
			ASSERT(pVB, "Failed to create vertex buffer for StaticMesh.");

			const void* pIndexData = level.GetIndexData();
			const uint32 ibBytes = level.GetIndexDataSizeBytes();
			ASSERT(pIndexData && ibBytes > 0, "Invalid index data in StaticMeshAsset.");

			RefCntAutoPtr<IBuffer> pIB = createImmutableBuffer(m_pDevice, "StaticMesh_IB", BIND_INDEX_BUFFER, pIndexData, ibBytes);
			ASSERT(pIB, "Failed to create index buffer for StaticMesh.");

			StaticMeshLevelRenderData levelRenderData = {};
			levelRenderData.VertexBuffer = pVB;
			levelRenderData.IndexBuffer = pIB;
			levelRenderData.VertexStride = static_cast<uint32>(sizeof(PackedStaticVertex));
			levelRenderData.VertexCount = level.GetVertexCount();
			levelRenderData.IndexCount = level.GetIndexCount();
			levelRenderData.IndexType = level.GetIndexType();
			levelRenderData.LocalBounds = level.GetBounds();

			levelRenderData.Sections.reserve(level.GetSections().size());
			for (const auto& s : level.GetSections())
			{
				StaticMeshLevelRenderData::Section d{};
				d.FirstIndex = s.FirstIndex;
				d.IndexCount = s.IndexCount;
				d.BaseVertex = s.BaseVertex;
				d.LocalBounds = s.LocalBounds;
				d.MaterialId = level.GetMaterialSlot(s.MaterialSlot);
				levelRenderData.Sections.push_back(d);
			}

			pushBarrier(levelRenderData.VertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
			pushBarrier(levelRenderData.IndexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

			out.Levels.emplace_back(std::move(levelRenderData));
		}
		out.LODScreenSizes = mesh.GetLodScreenSizes();

		m_StaticMeshCache.Store(key, std::move(out));
		return m_StaticMeshCache.Acquire(key);
	}

	const StaticMeshRenderData& Renderer::CreateStaticMeshRenderData(const AssetRef<AssimpAsset>& assimpRef, const std::string& materialTempalteName, const std::string& name)
	{
		StaticMesh staticMesh;
		const AssimpAsset& assimpAsset = *m_pAssetManager->LoadBlocking(assimpRef);
		StaticMeshLevel level;
		BuildStaticMeshAsset(assimpAsset, &level, {}, materialTempalteName, nullptr, m_pAssetManager);

		staticMesh.AddLevel(std::move(level), 1.0f);
		return CreateStaticMeshRenderData(staticMesh);
	}

	const StaticMeshRenderData& Renderer::CreateStaticMeshRenderData(const std::vector<AssetRef<AssimpAsset>>& assimpRefs, const std::string& materialTempalteName, const std::string& name)
	{
		ASSERT(!assimpRefs.empty(), "Assimp asset list is empty.");

		StaticMesh staticMesh;
		float lodFactor = 0.5f;
		for (const AssetRef<AssimpAsset>& assimpRef : assimpRefs)
		{
			const AssimpAsset& assimpAsset = *m_pAssetManager->LoadBlocking(assimpRef);
			StaticMeshLevel level;
			BuildStaticMeshAsset(assimpAsset, &level, {}, materialTempalteName, nullptr, m_pAssetManager);
			staticMesh.AddLevel(std::move(level), lodFactor);
			lodFactor *= 0.4f;
		}

		return CreateStaticMeshRenderData(staticMesh);
	}
	const StaticMeshRenderData& Renderer::CreateStaticMeshRenderData(const std::vector<AssetRef<AssimpAsset>>& assimpRefs, const std::vector<std::string>& materialTempalteNames, const std::string& name)
	{
		ASSERT(!assimpRefs.empty(), "Assimp asset list is empty.");
		ASSERT(assimpRefs.size() == materialTempalteNames.size(), "Assimp asset count and material template name count must match.");

		StaticMesh staticMesh;
		float lodFactor = 0.5f;
		for (size_t i = 0; i < assimpRefs.size(); ++i)
		{
			const AssetRef<AssimpAsset>& assimpRef = assimpRefs[i];
			const std::string& materialTemplateName = (i < materialTempalteNames.size()) ? materialTempalteNames[i] : "";
			const AssimpAsset& assimpAsset = *m_pAssetManager->LoadBlocking(assimpRef);
			StaticMeshLevel level;
			BuildStaticMeshAsset(assimpAsset, &level, {}, materialTemplateName, nullptr, m_pAssetManager);
			staticMesh.AddLevel(std::move(level), lodFactor);
			lodFactor *= 0.4f;
		}

		return CreateStaticMeshRenderData(staticMesh);
	}

	void Renderer::CreateShader(ShaderCreateInfo& sci, IShader** ppOutShader)
	{
		// TODO: 중복 생성 제거
		sci.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
		sci.ShaderCompiler = SHADER_COMPILER_DXC;

		m_pDevice->CreateShader(sci, ppOutShader);
		ASSERT(*ppOutShader, "Failed to create shader.");
	}

	RefCntAutoPtr<IPipelineState> Renderer::AcquirePipelineState(const GraphicsPipelineStateCreateInfo& desc, bool bBindCommonResources)
	{
		ASSERT(m_pPipelineStateManager, "Renderer is not initialized yet.");
		ASSERT(desc.GraphicsPipeline.pRenderPass != nullptr, "Render pass is not set.");
		return m_pPipelineStateManager->AcquireGraphics(desc, bBindCommonResources);
	}

	RefCntAutoPtr<IPipelineState> Renderer::AcquirePipelineState(const ComputePipelineStateCreateInfo& desc, bool bBindCommonResources)
	{
		ASSERT(m_pPipelineStateManager, "Renderer is not initialized yet.");
		return m_pPipelineStateManager->AcquireCompute(desc, bBindCommonResources);
	}

	RefCntAutoPtr<IPipelineState> Renderer::AcquirePipelineState(uint64 passId, GraphicsPipelineStateCreateInfo& desc, bool bBindCommonResources)
	{
		ASSERT(m_pPipelineStateManager, "Renderer is not initialized yet.");
		ASSERT(m_PassTable.contains(passId), "Unknown render pass.");

		desc.GraphicsPipeline.pRenderPass = m_PassTable.at(passId).pRHIRenderpass;
		desc.GraphicsPipeline.SubpassIndex = 0;
		desc.GraphicsPipeline.NumRenderTargets = 0;
		desc.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
		desc.GraphicsPipeline.DSVFormat = TEX_FORMAT_UNKNOWN;

		return m_pPipelineStateManager->AcquireGraphics(desc, bBindCommonResources);
	}

	RefCntAutoPtr<IPipelineState> Renderer::AcquirePipelineState(uint64 passId, ComputePipelineStateCreateInfo& desc, bool bBindCommonResources)
	{
		ASSERT(m_pPipelineStateManager, "Renderer is not initialized yet.");
		// Compute pass does not require render pass.
		return m_pPipelineStateManager->AcquireCompute(desc, bBindCommonResources);
	}

	const MaterialPipelineBinding& Renderer::AcquireMaterialPipelineBinding(MaterialId materialId, uint64 renderPassKey)
	{
		auto hashCombine64 = [](uint64 h, uint64 v)
			{
				return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2));
			};

		const uint64 hash = hashCombine64(materialId, renderPassKey);
		auto it = m_PipelineBindingCache.find(hash);
		if (it != m_PipelineBindingCache.end())
		{
			return it->second;
		}

		MaterialPipelineBinding out = {};

		MaterialManager* pMaterialManager = MaterialManager::GetInstance();
		ASSERT(pMaterialManager, "MaterialManager is null.");
		ASSERT(pMaterialManager->HasMaterial(materialId), "Material is not found.");

		Material& material = pMaterialManager->GetMaterial(materialId);

		// ---------------------------------------------------------------------
		// PSO acquire
		// ---------------------------------------------------------------------
		EMaterialPass pass = EMaterialPass::Base;
		if (renderPassKey == STRING_HASH("Shadow"))
		{
			renderPassKey = STRING_HASH("Shadow.Cascade0");
			pass = EMaterialPass::ShadowDepth;
			material.SetBufferResource("g_ObjectTable", STRING_HASH("ShadowPassObjectTable"));
		}
		else if (renderPassKey == STRING_HASH("DepthPrepass"))
		{
			pass = EMaterialPass::DepthOnly;
			material.SetBufferResource("g_ObjectTable", STRING_HASH("BasePassObjectTable"));
		}
		else if (renderPassKey == STRING_HASH("Forward"))
		{
			pass = EMaterialPass::Forward;
			material.SetBufferResource("g_ObjectTable", STRING_HASH("ForwardPassObjectTable"));
		}
		else
		{
			pass = EMaterialPass::Base;
			material.SetBufferResource("g_ObjectTable", STRING_HASH("BasePassObjectTable"));
		}

		ASSERT(renderPassKey != 0, "Render pass must be set in graphics pipeline.");
		ASSERT(m_PassTable.contains(renderPassKey), "Render pass not found.");

		GraphicsPipelineStateCreateInfo psoCI = material.BuildGraphicsPipelineStateCreateInfo(m_PassTable.at(renderPassKey).pRHIRenderpass, pass);

		out.pPSO = m_pPipelineStateManager->AcquireGraphics(psoCI);
		ASSERT(out.pPSO, "Failed to acquire PSO.");

		// Create SRB
		out.pPSO->CreateShaderResourceBinding(&out.pSRB, true);
		ASSERT(out.pSRB, "Failed to create SRB.");

		// Stage list helper
		const SHADER_TYPE kStages[] =
		{
			SHADER_TYPE_VERTEX,
			SHADER_TYPE_PIXEL,
			SHADER_TYPE_GEOMETRY,
			SHADER_TYPE_HULL,
			SHADER_TYPE_DOMAIN,
			SHADER_TYPE_AMPLIFICATION,
			SHADER_TYPE_MESH,
			SHADER_TYPE_COMPUTE
		};

		auto bindVarByStages = [&](const char* name, SHADER_TYPE stageMask, auto&& setterFn) -> SHADER_TYPE
			{
				SHADER_TYPE actuallyBound = SHADER_TYPE_UNKNOWN;
				bool anyBound = false;

				// If mask is known, only try those stages.
				if (stageMask != SHADER_TYPE_UNKNOWN)
				{
					for (SHADER_TYPE st : kStages)
					{
						if ((stageMask & st) == 0)
							continue;

						IShaderResourceVariable* var = out.pSRB->GetVariableByName(st, name);
						if (var)
						{
							setterFn(var);
							anyBound = true;
							actuallyBound = (SHADER_TYPE)(actuallyBound | st);
						}
					}
				}

				// Fallback: probe all shaders used by the material/template.
				if (!anyBound)
				{
					for (const RefCntAutoPtr<IShader>& shader : material.GetShaders(pass))
					{
						ASSERT(shader, "Shader in source instance is null.");
						const SHADER_TYPE st = shader->GetDesc().ShaderType;

						IShaderResourceVariable* var = out.pSRB->GetVariableByName(st, name);
						if (var)
						{
							setterFn(var);
							actuallyBound = (SHADER_TYPE)(actuallyBound | st);
						}
					}
				}

				return actuallyBound;
			};

		// ---------------------------------------------------------------------
		// Constant Buffers (bind ALL reflected CBs + store them)
		// ---------------------------------------------------------------------
		const uint32 cbCount = material.GetTemplate().GetCBufferCount();
		for (uint32 cbIndex = 0; cbIndex < cbCount; ++cbIndex)
		{
			const MaterialCBufferDesc& cb = material.GetTemplate().GetCBuffer(cbIndex);

			BufferDesc desc = {};
			desc.Name = cb.Name.c_str();
			desc.Usage = cb.IsDynamic ? USAGE_DYNAMIC : USAGE_DEFAULT;
			desc.BindFlags = BIND_UNIFORM_BUFFER;
			desc.CPUAccessFlags = cb.IsDynamic ? CPU_ACCESS_WRITE : CPU_ACCESS_NONE;
			desc.Size = cb.ByteSize;

			RefCntAutoPtr<IBuffer> pConstantBuffer;
			m_pDevice->CreateBuffer(desc, nullptr, &pConstantBuffer);
			ASSERT(pConstantBuffer, "Failed to create constant buffer: %s", cb.Name.c_str());

			const SHADER_TYPE boundStages = bindVarByStages(
				cb.Name.c_str(),
				cb.ShaderStages,
				[&](IShaderResourceVariable* var)
				{
					var->Set(pConstantBuffer);
				});

			// Upload initial blob
			const uint8* pBlob = material.GetCBufferBlobData(cbIndex);
			const uint32 blobSize = material.GetCBufferBlobSize(cbIndex);

			ASSERT(pBlob, "Invalid blob pointer. cb=%s", cb.Name.c_str());
			ASSERT(blobSize == cb.ByteSize, "Blob size mismatch. cb=%s blob=%u expected=%u", cb.Name.c_str(), blobSize, cb.ByteSize);

			if (desc.Usage == USAGE_DYNAMIC)
			{
				void* pMapped = nullptr;
				m_pImmediateContext->MapBuffer(pConstantBuffer, MAP_WRITE, MAP_FLAG_DISCARD, pMapped);
				ASSERT(pMapped, "Failed to map dynamic CB: %s", cb.Name.c_str());
				std::memcpy(pMapped, pBlob, blobSize);
				m_pImmediateContext->UnmapBuffer(pConstantBuffer, MAP_WRITE);
			}
			else
			{
				m_pImmediateContext->UpdateBuffer(
					pConstantBuffer,
					0,
					blobSize,
					pBlob,
					RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			}

			pushBarrier(pConstantBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

			BoundConstantBuffer bc = {};
			bc.pBuffer = pConstantBuffer;
			bc.CBufferIndex = cbIndex;
			bc.ByteSize = cb.ByteSize;
			bc.ShaderStages = boundStages;

			out.ConstantBuffers[cb.Name] = std::move(bc);
		}

		// ---------------------------------------------------------------------
		// Resources (Textures + StructuredBuffers + RWStructuredBuffers)
		// - Uses new runtime binding list: material.GetResourceBinding(i)
		// - Texture: AssetRef or ResourceId
		// - Buffer : ResourceId only
		// ---------------------------------------------------------------------
		const uint32 resCount = material.GetTemplate().GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const MaterialResourceDesc& resDesc = material.GetTemplate().GetResource(i);

			// New binding (1:1 with template resource index)
			const MaterialResourceBinding& binding = material.GetResourceBinding(i);

			// -----------------------------
			// Texture SRV
			// -----------------------------
			if (IsTextureType(resDesc.Type))
			{
				RefCntAutoPtr<ITexture> pTexture;
				ITextureView* pSRV = nullptr;

				// 1) ResourceId path (registry-backed)
				if (binding.HasResourceId())
				{
					// NOTE: You must implement/own these registry APIs.
					// Common patterns:
					// - GetTexture(uint64 id) -> RefCntAutoPtr<ITexture>
					// - GetTextureViewSRV(uint64 id) -> ITextureView*
					// We'll use GetTexture(id) + Default SRV for minimal assumptions.
					pTexture = m_pRegistry->GetTexture(binding.ResourceId);
					ASSERT(pTexture, "Registry texture not found. name=%s id=%llu", resDesc.Name.c_str(), (unsigned long long)binding.ResourceId);
					pSRV = pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
				}
				// 2) AssetRef path
				else if (binding.HasAssetRef())
				{
					ASSERT(binding.TextureRef.has_value(), "TextureRef missing. name=%s", resDesc.Name.c_str());
					ASSERT(binding.TextureRef->IsValid(), "Material texture ref invalid: %s", resDesc.Name.c_str());

					pTexture = CreateTexture(*binding.TextureRef);
					ASSERT(pTexture, "CreateTexture failed. name=%s", resDesc.Name.c_str());
					pSRV = pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
				}
				// 3) Fallback
				else
				{
					pTexture = m_pRegistry->GetTexture(STRING_HASH("ErrorTex"));
					ASSERT(pTexture, "ErrorTex missing in registry.");
					pSRV = pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
				}

				ASSERT(pSRV, "Texture SRV is null: %s", resDesc.Name.c_str());

				const SHADER_TYPE boundStages = bindVarByStages(
					resDesc.Name.c_str(),
					resDesc.ShaderStages,
					[&](IShaderResourceVariable* var)
					{
						var->Set(pSRV);
					});

				pushBarrier(pTexture, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

				BoundTexture bt = {};
				bt.pTexture = pTexture;
				bt.pSRV = pSRV;
				bt.ResourceIndex = i;
				bt.Type = resDesc.Type;
				bt.ShaderStages = boundStages;

				out.Textures[resDesc.Name] = std::move(bt);
				continue;
			}

			// -----------------------------
			// StructuredBuffer SRV / RWStructuredBuffer UAV
			// -----------------------------
			if (resDesc.Type == MATERIAL_RESOURCE_TYPE_STRUCTUREDBUFFER ||
				resDesc.Type == MATERIAL_RESOURCE_TYPE_RWSTRUCTUREDBUFFER)
			{
				ASSERT(binding.HasResourceId(), "Buffer resource must be bound by ResourceId. name=%s", resDesc.Name.c_str());

				RefCntAutoPtr<IBuffer> pBuffer = m_pRegistry->GetBuffer(binding.ResourceId);
				ASSERT(pBuffer, "Registry buffer not found. name=%s id=%llu", resDesc.Name.c_str(), (unsigned long long)binding.ResourceId);

				IDeviceObject* pViewObj = nullptr;

				if (resDesc.Type == MATERIAL_RESOURCE_TYPE_STRUCTUREDBUFFER)
				{
					RefCntAutoPtr<IBufferView> pSRV = m_pRegistry->GetBufferSRV(binding.ResourceId);

					const SHADER_TYPE boundStages = bindVarByStages(resDesc.Name.c_str(), resDesc.ShaderStages,
						[&](IShaderResourceVariable* var) {var->Set(pSRV); });

					pushBarrier(pBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

					BoundBuffer bb = {};
					bb.pBuffer = pBuffer;
					bb.pView = pSRV;
					bb.ResourceIndex = i;
					bb.Type = resDesc.Type;
					bb.ShaderStages = boundStages;

					out.Buffers[resDesc.Name] = std::move(bb);
				}
				else // RWSTRUCTUREDBUFFER
				{
					RefCntAutoPtr<IBufferView> pUAV = m_pRegistry->GetBufferUAV(binding.ResourceId);

					const SHADER_TYPE boundStages = bindVarByStages(resDesc.Name.c_str(), resDesc.ShaderStages,
						[&](IShaderResourceVariable* var) {var->Set(pUAV); });

					pushBarrier(pBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS);

					BoundBuffer bb = {};
					bb.pBuffer = pBuffer;
					bb.pView = pUAV;
					bb.ResourceIndex = i;
					bb.Type = resDesc.Type;
					bb.ShaderStages = boundStages;

					out.Buffers[resDesc.Name] = std::move(bb);
				}

				continue;
			}

			// Unknown/unhandled resource types are ignored
		}

		// Cache and return
		m_PipelineBindingCache[hash] = std::move(out);
		return m_PipelineBindingCache[hash];
	}

	// ---------------------------------------------------------------------
	// Material templates
	// ---------------------------------------------------------------------

	void Renderer::RegisterMaterialTemplate(const MaterialTemplateCreateInfo& createInfo, MATERIAL_BLEND_MODE blendMode, bool bRegisterDepthOnly, bool bRegisterShadow)
	{
		ASSERT(createInfo.ShaderStages.size() >= 1, "At least one shader stage must be specified.");
		ASSERT(createInfo.TemplateName != "", "Material template name is empty.");

		bool bResult = false;
		ShaderMacroHelper macros;
		switch (blendMode)
		{
		case MATERIAL_BLEND_MODE_OPAQUE:
			macros.AddShaderMacro("OPAQUE", 1);
			break;
		case MATERIAL_BLEND_MODE_MASKED:
			macros.AddShaderMacro("MASKED", 1);
			break;
		case MATERIAL_BLEND_MODE_TRANSPARENT:
			macros.AddShaderMacro("TRANSPARENT", 1);
			break;
		}

		MaterialTemplateCreateInfo baseCreateInfo = createInfo;

		// Base pass
		{
			const std::string& name = baseCreateInfo.TemplateName;
			ASSERT(m_TemplateLibrary.find(name) == m_TemplateLibrary.end(), "Material template already exists: %s", name.c_str());

			baseCreateInfo.MacroArray = macros;

			MaterialTemplate baseTemplate;
			bResult = baseTemplate.Initialize(*this, baseCreateInfo);
			ASSERT(bResult, "Failed to create material template: %s", name.c_str());
			m_TemplateLibrary[name] = std::move(baseTemplate);
		}

		// Depth only
		if (bRegisterDepthOnly)
		{
			const std::string name = baseCreateInfo.TemplateName + "_DepthOnly";
			ASSERT(m_TemplateLibrary.find(name) == m_TemplateLibrary.end(), "Material template already exists: %s", name.c_str());

			MaterialTemplateCreateInfo depthOnlyCreateInfo = baseCreateInfo;
			depthOnlyCreateInfo.TemplateName = name;

			MaterialTemplate depthOnlyTemplate;
			macros.AddShaderMacro("DEPTH_ONLY", 1);
			depthOnlyCreateInfo.MacroArray = macros;

			bResult = depthOnlyTemplate.Initialize(*this, depthOnlyCreateInfo);
			ASSERT(bResult, "Failed to create material template: %s", name.c_str());
			m_TemplateLibrary[name] = std::move(depthOnlyTemplate);
		}

		// Shadow
		if (bRegisterShadow)
		{
			const std::string name = baseCreateInfo.TemplateName + "_Shadow";
			ASSERT(m_TemplateLibrary.find(name) == m_TemplateLibrary.end(), "Material template already exists: %s", name.c_str());

			MaterialTemplateCreateInfo shadowCreateInfo = baseCreateInfo;
			shadowCreateInfo.TemplateName = name;

			MaterialTemplate shadowTemplate;
			macros.AddShaderMacro("SHADOW", 1);
			shadowCreateInfo.MacroArray = macros;

			bResult = shadowTemplate.Initialize(*this, shadowCreateInfo);
			ASSERT(bResult, "Failed to create material template: %s", name.c_str());
			m_TemplateLibrary[name] = std::move(shadowTemplate);
		}
	}

	void Renderer::RegisterMaterialTemplate(const std::string& name, const std::string& vsPath, const std::string& psPath, MATERIAL_BLEND_MODE blendMode, bool bRegisterDepthOnly, bool bRegisterShadow)
	{
		MaterialTemplateCreateInfo createInfo = {};
		createInfo.TemplateName = name;

		MaterialShaderStageDesc vsDesc = {};
		vsDesc.ShaderType = SHADER_TYPE_VERTEX;
		vsDesc.FilePath = vsPath;
		vsDesc.EntryPoint = "main";
		createInfo.ShaderStages.push_back(vsDesc);

		MaterialShaderStageDesc psDesc = {};
		psDesc.ShaderType = SHADER_TYPE_PIXEL;
		psDesc.FilePath = psPath;
		psDesc.EntryPoint = "main";
		createInfo.ShaderStages.push_back(psDesc);

		RegisterMaterialTemplate(createInfo, blendMode, bRegisterDepthOnly, bRegisterShadow);
	}

	void Renderer::RegisterMaterialTemplate(const std::string& name, const std::string& vsPath, const std::string& vsEntry, const std::string& psPath, const std::string& psEntry, MATERIAL_BLEND_MODE blendMode, bool bRegisterDepthOnly, bool bRegisterShadow)
	{
		MaterialTemplateCreateInfo createInfo = {};
		createInfo.TemplateName = name;

		MaterialShaderStageDesc vsDesc = {};
		vsDesc.ShaderType = SHADER_TYPE_VERTEX;
		vsDesc.FilePath = vsPath;
		vsDesc.EntryPoint = vsEntry;
		createInfo.ShaderStages.push_back(vsDesc);

		MaterialShaderStageDesc psDesc = {};
		psDesc.ShaderType = SHADER_TYPE_PIXEL;
		psDesc.FilePath = psPath;
		psDesc.EntryPoint = psEntry;
		createInfo.ShaderStages.push_back(psDesc);

		RegisterMaterialTemplate(createInfo, blendMode, bRegisterDepthOnly, bRegisterShadow);
	}

	const MaterialTemplate& Renderer::GetMaterialTemplate(const std::string& name) const
	{
		auto it = m_TemplateLibrary.find(name);
		ASSERT(m_TemplateLibrary.find(name) != m_TemplateLibrary.end(), "Material template not found: %s", name.c_str());
		return it->second;
	}

	std::vector<std::string> Renderer::GetAllMaterialTemplateNames() const
	{
		std::vector<std::string> names;
		for (const auto& pair : m_TemplateLibrary)
		{
			names.push_back(pair.first);
		}
		return names;
	}

	void Renderer::UpdateViewConstantBuffer(const View& view)
	{
		IBuffer* pViewCB = m_pRegistry->GetBuffer(STRING_HASH("VIEW_CONSTANTS"));
		MapHelper<hlsl::ViewConstants> viewCB(m_pImmediateContext, pViewCB, MAP_WRITE, MAP_FLAG_DISCARD);

		viewCB->View = view.ViewMatrix;
		viewCB->Proj = view.ProjMatrix;
		viewCB->ViewProj = view.ViewProjMatrix;
		viewCB->InvViewProj = view.ViewProjMatrix.Inversed();
		viewCB->PrevViewProj = view.PrevViewProjMatrix;

		m_bViewCBDirty = true;
	}

	// ---------------------------------------------------------------------
	// Barrier helper
	// ---------------------------------------------------------------------

	void Renderer::pushBarrier(IDeviceObject* pObj, RESOURCE_STATE from, RESOURCE_STATE to)
	{
		ASSERT(pObj, "Device object is null.");

		PendingBarrier pb = {};
		pb.Hold = pObj;

		pb.Desc = {};
		pb.Desc.pResource = pb.Hold;
		pb.Desc.OldState = from;
		pb.Desc.NewState = to;
		pb.Desc.Flags = STATE_TRANSITION_FLAG_UPDATE_STATE;

		m_PendingBarriers.push_back(std::move(pb));
	}

	RESOURCE_STATE Renderer::mapUsageToState(const RenderPassResourceAccess& a) const
	{
		switch (a.Usage)
		{
		case RENDER_USAGE_CBV:          return RESOURCE_STATE_CONSTANT_BUFFER;
		case RENDER_USAGE_SRV:          return RESOURCE_STATE_SHADER_RESOURCE;
		case RENDER_USAGE_UAV:          return RESOURCE_STATE_UNORDERED_ACCESS;
		case RENDER_USAGE_RTV:          return RESOURCE_STATE_RENDER_TARGET;
		case RENDER_USAGE_DSV_WRITE:    return RESOURCE_STATE_DEPTH_WRITE;
		case RENDER_USAGE_DSV_READ:     return RESOURCE_STATE_DEPTH_READ;
		case RENDER_USAGE_VERTEX_BUFFER:return RESOURCE_STATE_VERTEX_BUFFER;
		case RENDER_USAGE_INDEX_BUFFER: return RESOURCE_STATE_INDEX_BUFFER;
		case RENDER_USAGE_INDIRECT_ARGUMENT: return RESOURCE_STATE_INDIRECT_ARGUMENT;
		case RENDER_USAGE_PRESENT:      return RESOURCE_STATE_PRESENT;
		default:                        return RESOURCE_STATE_UNKNOWN;
		}
	}

	IDeviceObject* Renderer::resolveDeviceObject(const RenderPassResourceAccess& a) const
	{
		ASSERT(m_pRegistry, "Registry is null.");

		if (a.Kind == RENDER_RESOURCE_KIND_TEXTURE)
		{
			return m_pRegistry->HasTexture(a.ResourceId) ? m_pRegistry->GetTexture(a.ResourceId) : nullptr;
		}
		else if (a.Kind == RENDER_RESOURCE_KIND_BUFFER)
		{
			return m_pRegistry->HasBuffer(a.ResourceId) ? m_pRegistry->GetBuffer(a.ResourceId) : nullptr;
		}
		else if (a.Kind == RENDER_RESOURCE_KIND_EXTERNAL)
		{
			// SwapChain backbuffer RTV (special-cased)
			if (a.ResourceId == STRING_HASH("SwapChain.BackBuffer") &&
				(a.Usage == RENDER_USAGE_RTV || a.Usage == RENDER_USAGE_SRV))
			{
				ASSERT(m_pSwapChain, "SwapChain is null.");
				ITextureView* bbRtv = m_pSwapChain->GetCurrentBackBufferRTV();
				ASSERT(bbRtv, "BackBuffer RTV is null.");
				return bbRtv->GetTexture();
			}

			ASSERT(false, "Unknown external resource id.");
			return nullptr;
		}

		ASSERT(false, "Unknown resource kind.");
		return nullptr;
	}

	void Renderer::compileRenderGraphOrder()
	{
		m_CompiledPassOrder.clear();

		// ------------------------------------------------------------
		// Collect passes
		// ------------------------------------------------------------
		ASSERT(!m_PassAddOrder.empty(), "Requires at least one render pass.");

		const uint32 n = static_cast<uint32>(m_PassAddOrder.size());

		// Access classification
		auto isWrite = [](const RenderPassResourceAccess& a) -> bool
			{
				if (a.Access == RENDER_ACCESS_WRITE)     return true;
				if (a.Access == RENDER_ACCESS_READ)      return false;
				if (a.Access == RENDER_ACCESS_READWRITE) return true;

				switch (a.Usage)
				{
				case RENDER_USAGE_RTV:
				case RENDER_USAGE_DSV_WRITE:
				case RENDER_USAGE_UAV:
					return true;
				default:
					return false;
				}
			};

		auto isRead = [](const RenderPassResourceAccess& a) -> bool
			{
				if (a.Access == RENDER_ACCESS_READ)      return true;
				if (a.Access == RENDER_ACCESS_WRITE)     return false;
				if (a.Access == RENDER_ACCESS_READWRITE) return true;

				switch (a.Usage)
				{
				case RENDER_USAGE_SRV:
				case RENDER_USAGE_CBV:
				case RENDER_USAGE_DSV_READ:
				case RENDER_USAGE_INDIRECT_ARGUMENT:
					return true;

				case RENDER_USAGE_UAV:
					return false;

				default:
					return false;
				}
			};

		// ------------------------------------------------------------
		// Build per-resource use lists (deterministic key order via std::map)
		// ------------------------------------------------------------
		struct UseList final
		{
			std::vector<uint32> Readers;
			std::vector<uint32> Writers;
		};

		std::map<uint64, UseList> uses;

		for (uint32 i = 0; i < n; ++i)
		{
			const auto& accesses = m_PassTable.at(m_PassAddOrder[i]).ResourceAccess;

			// Per-pass per-resource flags:
			// bit0 = read, bit1 = write
			std::unordered_map<uint64, uint8> localFlags;
			localFlags.reserve(accesses.size());

			for (const auto& a : accesses)
			{
				uint8& f = localFlags[a.ResourceId];
				if (isRead(a))
				{
					f |= 1;
				}
				if (isWrite(a))
				{
					f |= 2;
				}
			}

			for (const auto& kv : localFlags)
			{
				const uint64 rid = kv.first;
				const uint8  f = kv.second;

				UseList& ul = uses[rid];
				if (f & 1)
				{
					ul.Readers.push_back(i);
				}
				if (f & 2)
				{
					ul.Writers.push_back(i);
				}
			}
		}

		// ------------------------------------------------------------
		// Build adjacency (edges) WITHOUT per-insert de-dup.
		// ------------------------------------------------------------
		std::vector<std::vector<uint32>> adj;
		adj.resize(n);

		auto pushEdge = [&](uint32 u, uint32 v)
			{
				if (u == v) return;
				adj[u].push_back(v);
			};

		// ------------------------------------------------------------
		// Dependency rules (simple RenderGraph-lite):
		//
		// 1) Serialize writers for each resource (WAW):
		//    writers[0] -> writers[1] -> ... (by pass index; index is deterministic)
		//
		// 2) Readers that do NOT write (pure readers) depend on the LAST writer:
		//    lastWriter -> reader
		//
		// Why "last writer"?
		// - Without resource versioning, we assume "reads want the final produced value".
		// - If you need intermediate versions, treat them as different ResourceId's.
		//
		// NOTE:
		// - ReadWrite counts as both reader and writer, but it participates in writer
		//   serialization (so it will be ordered with other writers).
		// - Pure readers are kept out of writer chain to avoid over-serializing RW passes.
		// ------------------------------------------------------------
		for (auto& kv : uses)
		{
			UseList& ul = kv.second;

			// Sort+unique indices
			std::sort(ul.Readers.begin(), ul.Readers.end());
			ul.Readers.erase(std::unique(ul.Readers.begin(), ul.Readers.end()), ul.Readers.end());

			std::sort(ul.Writers.begin(), ul.Writers.end());
			ul.Writers.erase(std::unique(ul.Writers.begin(), ul.Writers.end()), ul.Writers.end());

			// 1. Serialize writers
			if (ul.Writers.size() >= 2)
			{
				for (uint32 wi = 1; wi < static_cast<uint32>(ul.Writers.size()); ++wi)
				{
					pushEdge(ul.Writers[wi - 1], ul.Writers[wi]);
				}
			}

			// 2. lastWriter -> pure readers
			if (!ul.Writers.empty() && !ul.Readers.empty())
			{
				const uint32 lastWriter = ul.Writers.back();

				for (uint32 r : ul.Readers)
				{
					// "pure reader" only (exclude RW passes that are in writer list)
					if (std::binary_search(ul.Writers.begin(), ul.Writers.end(), r))
					{
						continue;
					}

					pushEdge(lastWriter, r);
				}
			}
		}

		auto passName = [&](uint32 passIndex) -> const char*
			{
				return m_PassTable.at(m_PassAddOrder[passIndex]).Name.c_str();
			};

		auto dumpUse = [&](uint64 rid, const UseList& ul)
			{
				std::cout << "RID=" << rid << "\n  Writers:";
				for (uint32 w : ul.Writers) std::cout << " " << passName(w);
				std::cout << "\n  Readers:";
				for (uint32 r : ul.Readers) std::cout << " " << passName(r);
				std::cout << "\n\n";
			};

		// ------------------------------------------------------------
		// Finalize adjacency: sort+unique each list, then compute indegree
		// ------------------------------------------------------------
		std::vector<uint32> indeg;
		indeg.resize(n, 0);

		for (uint32 u = 0; u < n; ++u)
		{
			auto& list = adj[u];
			if (list.empty())
			{
				continue;
			}

			std::sort(list.begin(), list.end());
			list.erase(std::unique(list.begin(), list.end()), list.end());

			for (uint32 v : list)
			{
				ASSERT(v < n, "Adjacency index out of bounds.");
				indeg[v] += 1;
			}
		}

		// ------------------------------------------------------------
		// Kahn topo sort (deterministic):
		// - Use min-heap so the next selected node is always the smallest index.
		// ------------------------------------------------------------
		std::priority_queue<uint32, std::vector<uint32>, std::greater<uint32>> ready;

		for (uint32 i = 0; i < n; ++i)
		{
			if (indeg[i] == 0)
			{
				ready.push(i);
			}
		}

		std::vector<uint32> order;
		order.reserve(n);

		while (!ready.empty())
		{
			const uint32 u = ready.top();
			ready.pop();

			order.push_back(u);

			for (uint32 v : adj[u])
			{
				ASSERT(indeg[v] > 0, "Invalid indegree.");
				indeg[v] -= 1;

				if (indeg[v] == 0)
				{
					ready.push(v);
				}
			}
		}

		// ------------------------------------------------------------
		// Cycle fallback
		// ------------------------------------------------------------
		if (order.size() != n)
		{
			std::cout << "\n[RenderGraph] cycle suspected. Remaining passes:\n";
			for (uint32 i = 0; i < n; ++i)
			{
				if (indeg[i] > 0)
				{
					const auto& p = m_PassTable.at(m_PassAddOrder[i]);
					std::cout << " - " << p.Name << " (indeg=" << indeg[i] << ")\n";
				}
			}

			std::cout << "\n[RenderGraph] edges among remaining:\n";
			for (uint32 u = 0; u < n; ++u)
			{
				if (indeg[u] == 0) continue;
				const auto& pu = m_PassTable.at(m_PassAddOrder[u]);

				for (uint32 v : adj[u])
				{
					if (indeg[v] == 0) continue;
					const auto& pv = m_PassTable.at(m_PassAddOrder[v]);
					std::cout << "   " << pu.Name << " -> " << pv.Name << "\n";
				}
			}

			ASSERT(false, "Compile failed (cycle).");
		}


		for (uint32 idx : order)
		{
			ASSERT(idx < n, "Topo order index out of bounds.");
			m_CompiledPassOrder.push_back(m_PassAddOrder[idx]);
		}
	}

	void Renderer::buildTransitionsForPass(uint64 passId, std::vector<StateTransitionDesc>& outBarriers)
	{
		ASSERT(passId != 0, "Invalid pass ID.");
		outBarriers.clear();

		const auto& accesses = m_PassTable.at(passId).ResourceAccess;

		struct Agg final
		{
			RenderPassResourceAccess A;
			bool bHas = false;
		};

		std::unordered_map<uint64, Agg> agg;
		agg.reserve(accesses.size());

		auto isWrite = [](const RenderPassResourceAccess& a)
			{
				if (a.Access == RENDER_ACCESS_WRITE || a.Access == RENDER_ACCESS_READWRITE) return true;
				if (a.Usage == RENDER_USAGE_RTV || a.Usage == RENDER_USAGE_DSV_WRITE || a.Usage == RENDER_USAGE_UAV) return true;
				return false;
			};

		for (const auto& a : accesses)
		{
			auto& slot = agg[a.ResourceId];
			if (!slot.bHas)
			{
				slot.A = a;
				slot.bHas = true;
			}
			else
			{
				if (isWrite(a) && !isWrite(slot.A))
				{
					slot.A = a;
				}
			}
		}

		for (auto& kv : agg)
		{
			const RenderPassResourceAccess& a = kv.second.A;

			IDeviceObject* pObj = resolveDeviceObject(a);
			if (!pObj) continue;

			const RESOURCE_STATE desired = mapUsageToState(a);

			RESOURCE_STATE prev = RESOURCE_STATE_UNKNOWN;

			if (a.Kind == RENDER_RESOURCE_KIND_EXTERNAL)
			{
				auto it = m_ExternalStates.find(pObj);
				if (it != m_ExternalStates.end())
				{
					prev = it->second;
				}

				if (prev != desired)
				{
					StateTransitionDesc b = {};
					b.pResource = pObj;
					b.OldState = prev;
					b.NewState = desired;
					b.Flags = STATE_TRANSITION_FLAG_UPDATE_STATE;
					outBarriers.push_back(b);

					m_ExternalStates[pObj] = desired;
				}
			}
			else
			{
				auto it = m_ResourceStates.find(a.ResourceId);
				if (it != m_ResourceStates.end())
				{
					prev = it->second;
				}

				if (prev != desired)
				{
					StateTransitionDesc b = {};
					b.pResource = pObj;
					b.OldState = prev;
					b.NewState = desired;
					b.Flags = STATE_TRANSITION_FLAG_UPDATE_STATE;
					outBarriers.push_back(b);

					m_ResourceStates[a.ResourceId] = desired;
				}
			}
		}
	}

} // namespace shz


