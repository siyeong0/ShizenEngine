#include "pch.h"
#include "Renderer.h"
#include "ViewFamily.h"

#include "Engine/AssetRuntime/Public/AssetManager.h"

#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	bool Renderer::Initialize(const RendererCreateInfo& createInfo)
	{
		ASSERT(createInfo.pDevice, "Device is null.");
		ASSERT(createInfo.pImmediateContext, "ImmediateContext is null.");
		ASSERT(createInfo.pSwapChain, "SwapChain is null.");
		ASSERT(createInfo.pAssetManager, "AssetManager is null.");

		m_CreateInfo = createInfo;
		m_pAssetManager = m_CreateInfo.pAssetManager;

		const SwapChainDesc& swapChainDesc = m_CreateInfo.pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : swapChainDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : swapChainDesc.Height;

		// Shader source factory
		m_CreateInfo.pEngineFactory->CreateDefaultShaderSourceStreamFactory(m_CreateInfo.ShaderRootDir, &m_pShaderSourceFactory);

		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::FrameConstants), "Frame constants CB", &m_pFrameCB);
		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ObjectConstants), "Object constants CB", &m_pObjectCB);

		if (!createShadowTargets())
		{
			ASSERT(false, "Failed to create shadow targets.");
			return false;
		}

		if (!createDeferredTargets())
		{
			ASSERT(false, "Failed to create deferred render targets.");
			return false;
		}

		if (!createDeferredRenderPasses())
		{
			ASSERT(false, "Failed to create deferred render passes.");
			return false;
		}

		if (!createShadowRenderPasses())
		{
			ASSERT(false, "Failed to create shadow render passes.");
			return false;
		}

		if (!createBasicPSO())
		{
			ASSERT(false, "Failed to create Basic PSO.");
			return false;
		}
		if (!createGBufferPSO())
		{
			ASSERT(false, "Failed to create G-Buffer PSO.");
			return false;
		}
		if (!createLightingPSO())
		{
			ASSERT(false, "Failed to create Lighting PSO.");
			return false;
		}
		if (!createPostPSO())
		{
			ASSERT(false, "Failed to create Post PSO.");
			return false;
		}
		if (!createShadowPSO())
		{
			ASSERT(false, "Failed to create shadow PSO.");
			return false;
		}

		// Create resource cache (owned)
		{
			ASSERT(!m_pRenderResourceCache, "Resource cache already created.");

			m_pRenderResourceCache = std::make_unique<RenderResourceCache>();

			RenderResourceCacheCreateInfo RCI = {};
			RCI.pDevice = m_CreateInfo.pDevice;
			RCI.pAssetManager = m_pAssetManager;

			if (!m_pRenderResourceCache->Initialize(RCI))
			{
				ASSERT(false, "Failed to initialzie RenderResourceCache.");
				return false;
			}
		}

		return true;
	}

	void Renderer::Cleanup()
	{
		m_pBasicPSO.Release();
		m_PSO_Shadow.Release();
		m_PSO_GBuffer.Release();
		m_PSO_Lighting.Release();
		m_PSO_Post.Release();

		m_ShadowMapTex.Release();
		m_ShadowMapDSV.Release();
		m_ShadowMapSRV.Release();
		m_SRB_Shadow.Release();

		m_SRB_Lighting.Release();
		m_SRB_Post.Release();

		m_RP_Shadow.Release();
		m_FB_Shadow.Release();
		m_RP_GBuffer.Release();
		m_FB_GBuffer.Release();
		m_RP_Lighting.Release();
		m_FB_Lighting.Release();
		m_RP_Post.Release();
		m_FB_Post.Release();

		m_GBufferDepthTex.Release();
		m_GBufferDepthDSV.Release();
		m_GBufferDepthSRV.Release();

		for (uint32 i = 0; i < kGBufferCount; ++i)
		{
			m_GBufferTex[i].Release();
			m_GBufferRTV[i].Release();
			m_GBufferSRV[i].Release();
		}

		m_LightingTex.Release();
		m_LightingRTV.Release();
		m_LightingSRV.Release();

		m_pShadowCB.Release();
		m_pFrameCB.Release();
		m_pObjectCB.Release();

		m_pShaderSourceFactory.Release();

		m_pAssetManager = nullptr;
		m_CreateInfo = {};
		m_Width = 0;
		m_Height = 0;
	}

	void Renderer::OnResize(uint32 width, uint32 height)
	{
		m_Width = width;
		m_Height = height;
	}

	void Renderer::BeginFrame() {}
	void Renderer::EndFrame() {}

	// ============================================================
	// Resource forwarding API
	// ============================================================

	Handle<StaticMeshRenderData> Renderer::CreateStaticMesh(Handle<StaticMeshAsset> h)
	{
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		return m_pRenderResourceCache->CreateStaticMesh(h);
	}

	bool Renderer::DestroyStaticMesh(Handle<StaticMeshRenderData> h)
	{
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		return m_pRenderResourceCache->DestroyStaticMesh(h);
	}

	bool Renderer::DestroyMaterialInstance(Handle<MaterialInstance> h)
	{
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		return m_pRenderResourceCache->DestroyMaterialInstance(h);
	}

	bool Renderer::DestroyTextureGPU(Handle<ITexture> h)
	{
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		return m_pRenderResourceCache->DestroyTextureGPU(h);
	}

	// ============================================================
	// Render
	// ============================================================

	static void CreateRTTexture2D(
		IRenderDevice* pDevice,
		uint32 width,
		uint32 height,
		TEXTURE_FORMAT fmt,
		const char* name,
		RefCntAutoPtr<ITexture>& outTex,
		RefCntAutoPtr<ITextureView>& outRTV,
		RefCntAutoPtr<ITextureView>& outSRV)
	{
		TextureDesc td = {};
		td.Name = name;
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = width;
		td.Height = height;
		td.MipLevels = 1;
		td.Format = fmt;
		td.SampleCount = 1;
		td.Usage = USAGE_DEFAULT;
		td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

		outTex.Release();
		outRTV.Release();
		outSRV.Release();

		pDevice->CreateTexture(td, nullptr, &outTex);
		ASSERT(outTex, "Failed to create RT texture.");

		outRTV = outTex->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		outSRV = outTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		ASSERT(outRTV && outSRV, "RTV/SRV is null.");
	}

	void Renderer::Render(const RenderScene& scene, const ViewFamily& viewFamily)
	{
		IDeviceContext* pCtx = m_CreateInfo.pImmediateContext.RawPtr();
		ISwapChain* pSwap = m_CreateInfo.pSwapChain.RawPtr();
		IRenderDevice* pDev = m_CreateInfo.pDevice.RawPtr();

		ASSERT(pCtx && pSwap && pDev, "Render(): context/swap/device null.");
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		if (viewFamily.Views.empty())
			return;

		// ensureDeferredTargets/RenderPass/PSO... 는 이전 답변 그대로 사용한다고 가정
		if (!createShadowTargets())          return;
		if (!createShadowRenderPasses())     return;
		if (!createShadowPSO())              return;
		if (!createDeferredTargets())       return;
		if (!createDeferredRenderPasses())  return;
		if (!createLightingPSO())           return;
		if (!createPostPSO())               return;

		const View& view = viewFamily.Views[0];
		float3 cameraWS = view.CameraPosition;


		const RenderScene::LightObject* pGlobalLight = nullptr;
		for (auto h : scene.GetLightHandles())
		{
			pGlobalLight = scene.TryGetLight(h);
			// ASSERT 1 globla light
			break;
		}

		float3 lightDirWS = pGlobalLight->Direction.Normalized();
		float3 lightColor = pGlobalLight->Color;
		float lightIntensity = pGlobalLight->Intensity;
		Matrix4x4 lightViewProj = {};

		// viewport + frame CB update (너 코드 그대로)
		{
			MapHelper<hlsl::FrameConstants> cbData(pCtx, m_pFrameCB, MAP_WRITE, MAP_FLAG_DISCARD);
			cbData->View = view.ViewMatrix;
			cbData->Proj = view.ProjMatrix;
			cbData->ViewProj = view.ViewMatrix * view.ProjMatrix;
			cbData->InvViewProj = cbData->ViewProj.Inversed();
			cbData->CameraPosition = cameraWS;
			cbData->ViewportSize = {
				static_cast<float>(view.Viewport.right - view.Viewport.left),
				static_cast<float>(view.Viewport.bottom - view.Viewport.top)
			};
			cbData->InvViewportSize = { 1.f / cbData->ViewportSize.x, 1.f / cbData->ViewportSize.y };
			cbData->NearPlane = view.NearPlane;
			cbData->FarPlane = view.FarPlane;
			cbData->DeltaTime = viewFamily.DeltaTime;
			cbData->CurrTime = viewFamily.CurrentTime;

			const float3 L = lightDirWS.Normalized();
			const float3 lightForward = L;
			const float3 centerWS = cameraWS;
			float shadowDistance = 20.0f;
			const float3 lightPosWS = centerWS - lightForward * shadowDistance;
			float3 up = float3(0, 1, 0);
			if (Abs(Vector3::Dot(up, lightForward)) > 0.99f)
				up = float3(0, 0, 1);
			const Matrix4x4 lightView = Matrix4x4::LookAtLH(lightPosWS, centerWS, up);
			const Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix;
			const Matrix4x4 invViewProj = viewProj.Inversed();

			float3 cornersWS[8];
			{
				const float xs[2] = { -1.f, +1.f };
				const float ys[2] = { -1.f, +1.f };
				const float zs[2] = { 0.f, +1.f };
				int idx = 0;
				for (int iz = 0; iz < 2; ++iz)
				{
					for (int iy = 0; iy < 2; ++iy)
					{
						for (int ix = 0; ix < 2; ++ix)
						{
							const float4 ndc = float4(xs[ix], ys[iy], zs[iz], 1.f);
							float4 ws = invViewProj.MulVector4(ndc);
							const float invW = (ws.w != 0.f) ? (1.f / ws.w) : 0.f;
							ws.x *= invW; ws.y *= invW; ws.z *= invW;
							cornersWS[idx++] = float3(ws.x, ws.y, ws.z);
						}
					}
				}
			}
			float minX = +FLT_MAX, minY = +FLT_MAX, minZ = +FLT_MAX;
			float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

			for (int i = 0; i < 8; ++i)
			{
				const float4 p = lightView.MulVector4(float4(cornersWS[i].x, cornersWS[i].y, cornersWS[i].z, 1.f));
				minX = std::min(minX, p.x);  maxX = std::max(maxX, p.x);
				minY = std::min(minY, p.y);  maxY = std::max(maxY, p.y);
				minZ = std::min(minZ, p.z);  maxZ = std::max(maxZ, p.z);
			}
			float xyPadding = 1.f;
			float zPadding = 2.f;
			minX -= xyPadding; maxX += xyPadding;
			minY -= xyPadding; maxY += xyPadding;
			minZ -= zPadding;  maxZ += zPadding;
			const float l = minX;
			const float r = maxX;
			const float b = minY;
			const float t = maxY;
			const float zn = minZ;
			const float zf = maxZ;

			const Matrix4x4 lightProj = Matrix4x4::OrthoOffCenter(l, r, b, t, zn, zf);
			lightViewProj = lightView * lightProj;

			cbData->LightViewProj = lightViewProj;
			cbData->LightDirWS = lightDirWS;
			cbData->LightColor = lightColor;
			cbData->LightIntensity = lightIntensity;
		}

		{
			MapHelper<hlsl::ShadowConstants> CB(pCtx, m_pShadowCB, MAP_WRITE, MAP_FLAG_DISCARD);
			CB->LightViewProj = lightViewProj;
		}

		auto drawFullScreenTriangle = [&]()
		{
			DrawAttribs DA = {};
			DA.NumVertices = 3;
			DA.Flags = DRAW_FLAG_VERIFY_ALL;
			pCtx->Draw(DA);
		};

		// 1) RenderPass 들어가기 전에:
		//    - 이번 프레임에 필요한 MaterialRenderData를 "미리" 준비
		//    - VB/IB 상태를 명시적으로 VERTEX/INDEX로 전환
		//    - 머티리얼 텍스처 SRV들도 SHADER_RESOURCE로 전환 (필요 시)
		//
		//    이렇게 하면 RenderPass 안에서는 VERIFY-only로 안전.
		std::vector<StateTransitionDesc> PreBarriers;
		PreBarriers.reserve(256);

		// material render data 캐시(이번 프레임 동안 재사용)
		// key: material handle -> matRD
		std::unordered_map<uint64, MaterialRenderData*> FrameMatCache;
		FrameMatCache.reserve(128);

		for (const Handle<RenderScene::RenderObject>& hObj : scene.GetObjectHandles())
		{
			const RenderScene::RenderObject* renderObject = scene.TryGetObject(hObj);
			if (!renderObject) continue;

			const StaticMeshRenderData* pMesh = m_pRenderResourceCache->TryGetMesh(renderObject->MeshHandle);
			if (!pMesh) continue;

			// VB/IB가 COPY_DEST에 머물러 있으면 여기서 상태 전환을 해줘야 함
			// (업로드 후에 너가 상태를 다시 안 바꿔놨기 때문)
			if (pMesh->VertexBuffer)
			{
				PreBarriers.push_back(
					StateTransitionDesc{ pMesh->VertexBuffer,
										 RESOURCE_STATE_UNKNOWN,
										 RESOURCE_STATE_VERTEX_BUFFER,
										 STATE_TRANSITION_FLAG_UPDATE_STATE });
			}

			for (const MeshSection& section : pMesh->Sections)
			{
				if (section.IndexBuffer)
				{
					PreBarriers.push_back(
						StateTransitionDesc{ section.IndexBuffer,
											 RESOURCE_STATE_UNKNOWN,
											 RESOURCE_STATE_INDEX_BUFFER,
											 STATE_TRANSITION_FLAG_UPDATE_STATE });
				}

				// MaterialRenderData를 RenderPass 밖에서 준비
				const Handle<MaterialInstance> matHandle = section.Material;
				const uint64 matKey = static_cast<uint64>(matHandle.GetValue()); // Handle 구현에 맞게 수정

				if (FrameMatCache.find(matKey) == FrameMatCache.end())
				{
					MaterialRenderData* matRD =
						m_pRenderResourceCache->GetOrCreateMaterialRenderData(
							matHandle,
							m_PSO_GBuffer,
							m_pObjectCB,
							pCtx /* 여기서 TRANSITION 발생해도 RenderPass 밖이니 OK */);

					ASSERT(matRD, "GetOrCreateMaterialRenderData failed.");
					FrameMatCache[matKey] = matRD;

					// 머티리얼 CB는 Diligent에서 보통 CONSTANT_BUFFER 상태 필요
					if (matRD->pMaterialCB)
					{
						PreBarriers.push_back(
							StateTransitionDesc{ matRD->pMaterialCB,
												 RESOURCE_STATE_UNKNOWN,
												 RESOURCE_STATE_CONSTANT_BUFFER,
												 STATE_TRANSITION_FLAG_UPDATE_STATE });
					}

					// (선택) 머티리얼 텍스처들도 SRV 상태로 보장
					// 네 MaterialRenderData가 실제 ITexture*를 들고 있다면 여기서 Transition 걸어주면 더 안전.
					// (SRV view만으로도 Transition이 가능하긴 한데, Diligent는 리소스 기준이 더 일반적)
				}
			}
		}

		// Frame/Object CB도 명시적으로 상태 보장(안전)
		if (m_pFrameCB)
			PreBarriers.push_back(StateTransitionDesc{ m_pFrameCB,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE });
		if (m_pObjectCB)
			PreBarriers.push_back(StateTransitionDesc{ m_pObjectCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE });
		if (m_pShadowCB)
			PreBarriers.push_back(StateTransitionDesc{ m_pShadowCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE });
		if (!PreBarriers.empty())
			pCtx->TransitionResourceStates(static_cast<uint32>(PreBarriers.size()), PreBarriers.data());


		// ============================================================
		// PASS 0: Shadow
		// ============================================================

		{
			StateTransitionDesc B = { m_ShadowMapTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_DEPTH_WRITE, STATE_TRANSITION_FLAG_UPDATE_STATE };
			pCtx->TransitionResourceStates(1, &B);
		}

		// Shadow viewport
		{
			Viewport vp;
			vp.TopLeftX = 0.f;
			vp.TopLeftY = 0.f;
			vp.Width = static_cast<float>(kShadowMapSize);
			vp.Height = static_cast<float>(kShadowMapSize);
			vp.MinDepth = 0.f;
			vp.MaxDepth = 1.f;
			pCtx->SetViewports(1, &vp, 0, 0);
		}

		// Begin RP
		{
			OptimizedClearValue ClearVals[1] = {};
			ClearVals[0].DepthStencil.Depth = 1.f;
			ClearVals[0].DepthStencil.Stencil = 0;

			BeginRenderPassAttribs RPBegin = {};
			RPBegin.pRenderPass = m_RP_Shadow;
			RPBegin.pFramebuffer = m_FB_Shadow;
			RPBegin.ClearValueCount = 1;
			RPBegin.pClearValues = ClearVals;

			pCtx->BeginRenderPass(RPBegin);

			pCtx->SetPipelineState(m_PSO_Shadow);
			pCtx->CommitShaderResources(m_SRB_Shadow, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			for (const Handle<RenderScene::RenderObject>& hObj : scene.GetObjectHandles())
			{
				const RenderScene::RenderObject* renderObject = scene.TryGetObject(hObj);
				if (!renderObject) continue;

				const StaticMeshRenderData* pMesh = m_pRenderResourceCache->TryGetMesh(renderObject->MeshHandle);
				if (!pMesh) continue;

				// Object CB (ShadowDepth VS가 OBJECT_CONSTANTS를 dynamic으로 받는다고 가정)
				{
					MapHelper<hlsl::ObjectConstants> CBData(pCtx, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
					CBData->World = renderObject->Transform;
					CBData->WorldInvTranspose = renderObject->Transform.Inversed().Transposed();
				}

				// Shadow PSO는 input layout이 Pos만이라서,
				// 네 VertexBuffer가 Pos+UV+Normal+Tangent interleaved면 그대로 SetVertexBuffers하면 됨.
				// (ATTRIB0만 읽음)
				IBuffer* pVBs[] = { pMesh->VertexBuffer };
				uint64 Offsets[] = { 0 };
				pCtx->SetVertexBuffers(0, 1, pVBs, Offsets, RESOURCE_STATE_TRANSITION_MODE_VERIFY, SET_VERTEX_BUFFERS_FLAG_RESET);
				for (const MeshSection& section : pMesh->Sections)
				{
					pCtx->SetIndexBuffer(section.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					DrawIndexedAttribs DIA = {};
					DIA.NumIndices = section.NumIndices;
					DIA.IndexType = (section.IndexType == VT_UINT16) ? VT_UINT16 : VT_UINT32;
					DIA.Flags = DRAW_FLAG_VERIFY_ALL;
					DIA.FirstIndexLocation = section.StartIndex;

					pCtx->DrawIndexed(DIA);
				}
			}

			pCtx->EndRenderPass();
		}

		// ShadowMap -> SRV
		{
			StateTransitionDesc B = { m_ShadowMapTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE };
			pCtx->TransitionResourceStates(1, &B);
		}

		{
			Viewport vp;
			vp.TopLeftX = static_cast<float>(view.Viewport.left);
			vp.TopLeftY = static_cast<float>(view.Viewport.top);
			vp.Width = static_cast<float>(view.Viewport.right - view.Viewport.left);
			vp.Height = static_cast<float>(view.Viewport.bottom - view.Viewport.top);
			vp.MinDepth = 0.f;
			vp.MaxDepth = 1.f;
			pCtx->SetViewports(1, &vp, 0, 0);
		}

		// ============================================================
		// PASS 1: GBuffer
		// ============================================================
		// // GBuffer attachments: SRV -> RT/DSV (frame-to-frame fix)
		{
			StateTransitionDesc B[] =
			{
				{ m_GBufferTex[0],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[1],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[2],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[3],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferDepthTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_DEPTH_WRITE,   STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pCtx->TransitionResourceStates(_countof(B), B);
		}
		{
			OptimizedClearValue ClearVals[5] = {};
			for (int i = 0; i < 4; ++i)
			{
				ClearVals[i].Color[0] = 0.f;
				ClearVals[i].Color[1] = 0.f;
				ClearVals[i].Color[2] = 0.f;
				ClearVals[i].Color[3] = 0.f;
			}
			ClearVals[4].DepthStencil.Depth = 1.f;
			ClearVals[4].DepthStencil.Stencil = 0;

			BeginRenderPassAttribs RPBegin = {};
			RPBegin.pRenderPass = m_RP_GBuffer;
			RPBegin.pFramebuffer = m_FB_GBuffer;
			RPBegin.ClearValueCount = 5;
			RPBegin.pClearValues = ClearVals;

			pCtx->BeginRenderPass(RPBegin);

			for (const Handle<RenderScene::RenderObject>& hObj : scene.GetObjectHandles())
			{
				const RenderScene::RenderObject* renderObject = scene.TryGetObject(hObj);
				if (!renderObject) continue;

				const StaticMeshRenderData* pMesh = m_pRenderResourceCache->TryGetMesh(renderObject->MeshHandle);
				if (!pMesh) continue;

				// Object CB update (RenderPass 안에서 map은 OK)
				{
					MapHelper<hlsl::ObjectConstants> CBData(pCtx, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
					CBData->World = renderObject->Transform;
					CBData->WorldInvTranspose = renderObject->Transform.Inversed().Transposed();
				}

				IBuffer* pVBs[] = { pMesh->VertexBuffer };
				uint64 Offsets[] = { 0 };
				pCtx->SetVertexBuffers(0, 1, pVBs, Offsets, RESOURCE_STATE_TRANSITION_MODE_VERIFY, SET_VERTEX_BUFFERS_FLAG_RESET);

				Handle<MaterialInstance> lastMat = {};

				for (const MeshSection& section : pMesh->Sections)
				{
					pCtx->SetIndexBuffer(section.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					const Handle<MaterialInstance> matHandle = section.Material;
					const uint64 matKey = static_cast<uint64>(matHandle.GetValue());

					MaterialRenderData* matRD = FrameMatCache[matKey];
					ASSERT(matRD, "FrameMatCache missing matRD.");

					if (matHandle != lastMat)
					{
						// ⚠️ 여기서 CommitRenderTargets를 내부 호출하는 코드가 있으면 터짐.
						// matRD->pPSO는 "그냥 PSO"여야 하고, 내부에서 SetRenderTargets 하지 않아야 함.
						pCtx->SetPipelineState(matRD->pPSO);
						pCtx->CommitShaderResources(matRD->pSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
						lastMat = matHandle;
					}

					// material constants update
					{
						MapHelper<hlsl::MaterialConstants> CB(pCtx, matRD->pMaterialCB, MAP_WRITE, MAP_FLAG_DISCARD);
						CB->BaseColorFactor = matRD->BaseColor;
						CB->EmissiveFactor = matRD->Emissive;
						CB->MetallicFactor = matRD->Metallic;
						CB->RoughnessFactor = matRD->Roughness;
						CB->NormalScale = matRD->NormalScale;
						CB->OcclusionStrength = matRD->OcclusionStrength;
						CB->AlphaCutoff = matRD->AlphaCutoff;
						CB->Flags = matRD->MaterialFlags;
					}

					DrawIndexedAttribs DIA = {};
					DIA.NumIndices = section.NumIndices;
					DIA.IndexType = (section.IndexType == VT_UINT16) ? VT_UINT16 : VT_UINT32;
					DIA.Flags = DRAW_FLAG_VERIFY_ALL;
					DIA.FirstIndexLocation = section.StartIndex;
					pCtx->DrawIndexed(DIA);
				}
			}

			pCtx->EndRenderPass();
		}

		// 3) GBuffer RT -> SRV
		{
			StateTransitionDesc Barriers[] =
			{
				{ m_GBufferTex[0], RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[1], RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[2], RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[3], RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferDepthTex,   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			pCtx->TransitionResourceStates(_countof(Barriers), Barriers);
		}

		// ============================================================
		// PASS 2: Lighting
		// ============================================================
		{
			OptimizedClearValue ClearVals[1] = {};
			ClearVals[0].Color[3] = 1.f;

			BeginRenderPassAttribs RPBegin = {};
			RPBegin.pRenderPass = m_RP_Lighting;
			RPBegin.pFramebuffer = m_FB_Lighting;
			RPBegin.ClearValueCount = 1;
			RPBegin.pClearValues = ClearVals;

			pCtx->BeginRenderPass(RPBegin);

			pCtx->SetPipelineState(m_PSO_Lighting);
			pCtx->CommitShaderResources(m_SRB_Lighting, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			drawFullScreenTriangle();

			pCtx->EndRenderPass();
		}

		// Lighting RT -> SRV
		{
			StateTransitionDesc B = { m_LightingTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE };
			pCtx->TransitionResourceStates(1, &B);
		}

		// ============================================================
		// PASS 3: PostCopy (Lighting -> BackBuffer)
		// ============================================================
		{
			// FB_Post는 BBRTV가 프레임마다 바뀔 수 있어 매 프레임 재생성
			ITextureView* BBRTV = pSwap->GetCurrentBackBufferRTV();
			FramebufferDesc FB = {};
			FB.Name = "FB_Post_Frame";
			FB.pRenderPass = m_RP_Post;
			FB.AttachmentCount = 1;
			FB.ppAttachments = &BBRTV;

			m_FB_Post.Release();
			pDev->CreateFramebuffer(FB, &m_FB_Post);

			// bind input
			m_SRB_Post->GetVariableByName(SHADER_TYPE_PIXEL, "g_InputColor")->Set(m_LightingSRV);

			OptimizedClearValue ClearVals[1] = {};
			ClearVals[0].Color[3] = 1.f;

			BeginRenderPassAttribs RPBegin = {};
			RPBegin.pRenderPass = m_RP_Post;
			RPBegin.pFramebuffer = m_FB_Post;
			RPBegin.ClearValueCount = 1;
			RPBegin.pClearValues = ClearVals;

			pCtx->BeginRenderPass(RPBegin);

			pCtx->SetPipelineState(m_PSO_Post);
			pCtx->CommitShaderResources(m_SRB_Post, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			drawFullScreenTriangle();

			pCtx->EndRenderPass();
		}
	}


	void Renderer::renderForward(const RenderScene& scene, const ViewFamily& viewFamily)
	{
		IDeviceContext* pImmediateContext = m_CreateInfo.pImmediateContext.RawPtr();
		ISwapChain* pSwapChain = m_CreateInfo.pSwapChain.RawPtr();

		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");
		ASSERT(pImmediateContext, "ImmediateContext is null.");
		ASSERT(pSwapChain, "ImmediateContext is null.");

		ITextureView* pRTV = pSwapChain->GetCurrentBackBufferRTV();
		ITextureView* pDSV = pSwapChain->GetDepthBufferDSV();
		pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		const float clearColor[] = { 0.350f, 0.350f, 0.350f, 1.0f };
		pImmediateContext->ClearRenderTarget(pRTV, clearColor, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
		pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		if (viewFamily.Views.empty())
		{
			ASSERT(false, "ViewFamily is empty. Please fill the view infos.");
			return;
		}

		// Frame CB update
		{
			MapHelper<hlsl::FrameConstants> cbData(pImmediateContext, m_pFrameCB, MAP_WRITE, MAP_FLAG_DISCARD);

			const View& view = viewFamily.Views[0];
			cbData->View = view.ViewMatrix;
			cbData->Proj = view.ProjMatrix;
			cbData->ViewProj = view.ViewMatrix * view.ProjMatrix;

			cbData->CameraPosition = { view.ViewMatrix._m03, view.ViewMatrix._m13, view.ViewMatrix._m23 };

			cbData->ViewportSize = {
				static_cast<float>(view.Viewport.right - view.Viewport.left),
				static_cast<float>(view.Viewport.bottom - view.Viewport.top)
			};
			cbData->InvViewportSize = {
				1.f / cbData->ViewportSize.x,
				1.f / cbData->ViewportSize.y
			};

			cbData->NearPlane = view.NearPlane;
			cbData->FarPlane = view.FarPlane;

			cbData->DeltaTime = viewFamily.DeltaTime;
			cbData->CurrTime = viewFamily.CurrentTime;
		}

		ASSERT(m_pBasicPSO, "BasicPSO is null. Initialize it.");

		for (const Handle<RenderScene::RenderObject>& hObj : scene.GetObjectHandles())
		{
			const RenderScene::RenderObject* renderObject = scene.TryGetObject(hObj);
			if (!renderObject)
			{
				ASSERT(false, "RenderObject handle is invalid.");
				continue;
			}

			const StaticMeshRenderData* pMesh = m_pRenderResourceCache->TryGetMesh(renderObject->MeshHandle);
			if (!pMesh)
			{
				ASSERT(false, "StaticMesh handle is invalid.");
				continue;
			}

			// Object CB update
			{
				MapHelper<hlsl::ObjectConstants> CBData(pImmediateContext, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
				CBData->World = renderObject->Transform;
				CBData->WorldInvTranspose = renderObject->Transform.Inversed().Transposed();
			}

			IBuffer* pVBs[] = { pMesh->VertexBuffer };
			uint64 Offsets[] = { 0 };
			pImmediateContext->SetVertexBuffers(0, 1, pVBs, Offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);

			Handle<MaterialInstance> lastMat = {};

			for (const MeshSection& section : pMesh->Sections)
			{
				pImmediateContext->SetIndexBuffer(section.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				const Handle<MaterialInstance> matHandle = section.Material;

				MaterialRenderData* matRD =
					m_pRenderResourceCache->GetOrCreateMaterialRenderData(
						matHandle,
						m_pBasicPSO,
						m_pObjectCB,
						pImmediateContext);
				ASSERT(matRD, "Failed to get or create material render data.");

				if (matHandle != lastMat)
				{
					pImmediateContext->SetPipelineState(matRD->pPSO);
					pImmediateContext->CommitShaderResources(matRD->pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
					lastMat = matHandle;
				}

				// Upload material constants
				{
					MapHelper<hlsl::MaterialConstants> CB(pImmediateContext, matRD->pMaterialCB, MAP_WRITE, MAP_FLAG_DISCARD);

					CB->BaseColorFactor = matRD->BaseColor;
					CB->EmissiveFactor = matRD->Emissive;
					CB->MetallicFactor = matRD->Metallic;

					CB->RoughnessFactor = matRD->Roughness;
					CB->NormalScale = matRD->NormalScale;
					CB->OcclusionStrength = matRD->OcclusionStrength;
					CB->AlphaCutoff = matRD->AlphaCutoff;

					CB->Flags = matRD->MaterialFlags;
				}

				DrawIndexedAttribs DIA = {};
				DIA.NumIndices = section.NumIndices;
				DIA.IndexType = (section.IndexType == VT_UINT16) ? VT_UINT16 : VT_UINT32;
				DIA.Flags = DRAW_FLAG_VERIFY_ALL;
				DIA.FirstIndexLocation = section.StartIndex;

				pImmediateContext->DrawIndexed(DIA);
			}
		}
	}

	// ============================================================
	// PSO
	// ============================================================

	bool Renderer::createBasicPSO()
	{
		IRenderDevice* pDevice = m_CreateInfo.pDevice.RawPtr();
		ASSERT(pDevice, "Device is null.");

		const SwapChainDesc& scDesc = m_CreateInfo.pSwapChain->GetDesc();

		GraphicsPipelineStateCreateInfo psoCreateInfo = {};
		psoCreateInfo.PSODesc.Name = "Debug Basic PSO";
		psoCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		GraphicsPipelineDesc& gp = psoCreateInfo.GraphicsPipeline;
		gp.NumRenderTargets = 1;
		gp.RTVFormats[0] = scDesc.ColorBufferFormat;
		gp.DSVFormat = scDesc.DepthBufferFormat;
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
		gp.RasterizerDesc.FrontCounterClockwise = true;
		gp.DepthStencilDesc.DepthEnable = true;

		LayoutElement layoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false},
			LayoutElement{1, 0, 2, VT_FLOAT32, false},
			LayoutElement{2, 0, 3, VT_FLOAT32, false},
			LayoutElement{3, 0, 3, VT_FLOAT32, false},
		};
		gp.InputLayout.LayoutElements = layoutElems;
		gp.InputLayout.NumElements = _countof(layoutElems);

		ShaderCreateInfo shaderCreateInfo = {};
		shaderCreateInfo.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		shaderCreateInfo.EntryPoint = "main";
		shaderCreateInfo.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		shaderCreateInfo.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR | SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION;

		RefCntAutoPtr<IShader> pVS;
		{
			shaderCreateInfo.Desc = {};
			shaderCreateInfo.Desc.Name = "Basic VS";
			shaderCreateInfo.Desc.ShaderType = SHADER_TYPE_VERTEX;
			shaderCreateInfo.FilePath = "Basic.vsh";
			shaderCreateInfo.Desc.UseCombinedTextureSamplers = false;

			pDevice->CreateShader(shaderCreateInfo, &pVS);
			if (!pVS)
			{
				ASSERT(false, "Failed to create a basic vertex shader.");
				return false;
			}
		}

		RefCntAutoPtr<IShader> pPS;
		{
			shaderCreateInfo.Desc = {};
			shaderCreateInfo.Desc.Name = "Basic PS";
			shaderCreateInfo.Desc.ShaderType = SHADER_TYPE_PIXEL;
			shaderCreateInfo.FilePath = "Basic.psh";
			shaderCreateInfo.Desc.UseCombinedTextureSamplers = false;

			pDevice->CreateShader(shaderCreateInfo, &pPS);
			if (!pPS)
			{
				ASSERT(false, "Failed to create a basic pixel shader.");
				return false;
			}
		}

		psoCreateInfo.pVS = pVS;
		psoCreateInfo.pPS = pPS;

		psoCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc shaderResVars[] =
		{
			{ SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS",   SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
			{ SHADER_TYPE_PIXEL,  "MATERIAL_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },

			{ SHADER_TYPE_PIXEL,  "g_BaseColorTex",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_NormalTex",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_MetallicRoughnessTex",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_AOTex",                 SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_EmissiveTex",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		psoCreateInfo.PSODesc.ResourceLayout.Variables = shaderResVars;
		psoCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(shaderResVars);

		SamplerDesc linearWrapSamplerDesc
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
		};
		ImmutableSamplerDesc ImtblSamplers[] =
		{
			{ SHADER_TYPE_PIXEL, "g_LinearWrapSampler", linearWrapSamplerDesc}
		};
		psoCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = ImtblSamplers;
		psoCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);

		pDevice->CreateGraphicsPipelineState(psoCreateInfo, &m_pBasicPSO);
		if (!m_pBasicPSO)
		{
			ASSERT(false, "Failed to create Basic PSO.");
			return false;
		}

		{
			if (auto* Var = m_pBasicPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FRAME_CONSTANTS"))
				Var->Set(m_pFrameCB);

			if (auto* Var = m_pBasicPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
				Var->Set(m_pFrameCB);
		}

		return true;
	}

	bool Renderer::createGBufferPSO()
	{
		IRenderDevice* pDevice = m_CreateInfo.pDevice.RawPtr();
		ASSERT(pDevice, "Device is null.");

		GraphicsPipelineStateCreateInfo psoCreateInfo = {};
		psoCreateInfo.PSODesc.Name = "GBuffer PSO";
		psoCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		GraphicsPipelineDesc& gp = psoCreateInfo.GraphicsPipeline;

		gp.pRenderPass = m_RP_GBuffer;
		gp.SubpassIndex = 0;

		// ------------------------------------------------------------
		// Render targets (MRT)
		// ------------------------------------------------------------
		gp.NumRenderTargets = 0;
		gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;   // GBuffer0
		gp.RTVFormats[1] = TEX_FORMAT_UNKNOWN;  // GBuffer1
		gp.RTVFormats[2] = TEX_FORMAT_UNKNOWN;   // GBuffer2
		gp.RTVFormats[3] = TEX_FORMAT_UNKNOWN;  // GBuffer3

		gp.DSVFormat = TEX_FORMAT_UNKNOWN;

		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
		gp.RasterizerDesc.FrontCounterClockwise = true;
		gp.DepthStencilDesc.DepthEnable = true;
		gp.DepthStencilDesc.DepthWriteEnable = true;
		gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

		// ------------------------------------------------------------
		// Input layout 
		// ------------------------------------------------------------
		LayoutElement layoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // ATTRIB0 Pos
			LayoutElement{1, 0, 2, VT_FLOAT32, false}, // ATTRIB1 UV
			LayoutElement{2, 0, 3, VT_FLOAT32, false}, // ATTRIB2 Normal
			LayoutElement{3, 0, 3, VT_FLOAT32, false}, // ATTRIB3 Tangent
		};
		gp.InputLayout.LayoutElements = layoutElems;
		gp.InputLayout.NumElements = _countof(layoutElems);

		// ------------------------------------------------------------
		// Shaders
		// ------------------------------------------------------------
		ShaderCreateInfo shaderCreateInfo = {};
		shaderCreateInfo.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		shaderCreateInfo.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		shaderCreateInfo.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR | SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION;

		RefCntAutoPtr<IShader> pVS;
		{
			shaderCreateInfo.Desc = {};
			shaderCreateInfo.Desc.Name = "GBuffer VS";
			shaderCreateInfo.EntryPoint = "main";
			shaderCreateInfo.Desc.ShaderType = SHADER_TYPE_VERTEX;

			shaderCreateInfo.FilePath = "GBuffer.vsh";
			shaderCreateInfo.Desc.UseCombinedTextureSamplers = false;

			pDevice->CreateShader(shaderCreateInfo, &pVS);
			if (!pVS)
			{
				ASSERT(false, "Failed to create GBuffer vertex shader.");
				return false;
			}
		}

		RefCntAutoPtr<IShader> pPS;
		{
			shaderCreateInfo.Desc = {};
			shaderCreateInfo.Desc.Name = "GBuffer PS";
			shaderCreateInfo.EntryPoint = "main";
			shaderCreateInfo.Desc.ShaderType = SHADER_TYPE_PIXEL;

			shaderCreateInfo.EntryPoint = "main";
			shaderCreateInfo.FilePath = "GBuffer.psh";
			shaderCreateInfo.Desc.UseCombinedTextureSamplers = false;

			pDevice->CreateShader(shaderCreateInfo, &pPS);
			if (!pPS)
			{
				ASSERT(false, "Failed to create GBuffer pixel shader.");
				return false;
			}
		}

		psoCreateInfo.pVS = pVS;
		psoCreateInfo.pPS = pPS;

		// ------------------------------------------------------------
		// Resource layout
		// ------------------------------------------------------------
		psoCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc shaderResVars[] =
		{
			{ SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS",   SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
			{ SHADER_TYPE_PIXEL,  "MATERIAL_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },

			{ SHADER_TYPE_PIXEL,  "g_BaseColorTex",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_NormalTex",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_MetallicRoughnessTex",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_AOTex",                 SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_EmissiveTex",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		psoCreateInfo.PSODesc.ResourceLayout.Variables = shaderResVars;
		psoCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(shaderResVars);

		SamplerDesc linearWrapSamplerDesc
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
		};
		ImmutableSamplerDesc immutableSamples[] =
		{
			{ SHADER_TYPE_PIXEL, "g_LinearWrapSampler", linearWrapSamplerDesc }
		};
		psoCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = immutableSamples;
		psoCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(immutableSamples);

		// ------------------------------------------------------------
		// Create PSO
		// ------------------------------------------------------------
		pDevice->CreateGraphicsPipelineState(psoCreateInfo, &m_PSO_GBuffer);
		if (!m_PSO_GBuffer)
		{
			ASSERT(false, "Failed to create GBuffer PSO.");
			return false;
		}

		// ------------------------------------------------------------
		// Bind static resources
		// ------------------------------------------------------------
		// FRAME_CONSTANTS is assumed STATIC (DefaultVariableType=STATIC), so set it here.
		{
			if (auto* Var = m_PSO_GBuffer->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FRAME_CONSTANTS"))
				Var->Set(m_pFrameCB);

			if (auto* Var = m_PSO_GBuffer->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
				Var->Set(m_pFrameCB);
		}

		return true;
	}

	bool Renderer::createLightingPSO()
	{
		IRenderDevice* pDevice = m_CreateInfo.pDevice.RawPtr();
		ASSERT(pDevice, "ensureLightingPSO(): device null.");

		if (m_PSO_Lighting && m_SRB_Lighting)
			return true;

		GraphicsPipelineStateCreateInfo PSO = {};
		PSO.PSODesc.Name = "Deferred Lighting PSO";
		PSO.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		auto& gp = PSO.GraphicsPipeline;

		gp.pRenderPass = m_RP_Post;
		gp.SubpassIndex = 0;
		gp.NumRenderTargets = 0;

		gp.NumRenderTargets = 0;
		gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
		gp.DSVFormat = TEX_FORMAT_UNKNOWN;

		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
		gp.RasterizerDesc.FrontCounterClockwise = true;
		gp.DepthStencilDesc.DepthEnable = false;

		ShaderCreateInfo sci = {};
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.EntryPoint = "main";
		sci.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR | SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION;

		RefCntAutoPtr<IShader> VS;
		{
			sci.Desc = {};
			sci.Desc.Name = "DeferredLighting VS";
			sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
			sci.FilePath = "DeferredLighting.vsh";
			sci.Desc.UseCombinedTextureSamplers = false;

			pDevice->CreateShader(sci, &VS);
			ASSERT(VS, "Failed to create DeferredLighting VS.");
		}

		RefCntAutoPtr<IShader> PS;
		{
			sci.Desc = {};
			sci.Desc.Name = "DeferredLighting PS";
			sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
			sci.FilePath = "DeferredLighting.psh";
			sci.Desc.UseCombinedTextureSamplers = false;

			pDevice->CreateShader(sci, &PS);
			ASSERT(PS, "Failed to create DeferredLighting PS.");
		}

		PSO.pVS = VS;
		PSO.pPS = PS;

		PSO.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{ SHADER_TYPE_PIXEL, "g_GBuffer0", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBuffer1", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBuffer2", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBuffer3", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

			{ SHADER_TYPE_PIXEL, "g_GBufferDepth", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_ShadowMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		PSO.PSODesc.ResourceLayout.Variables = Vars;
		PSO.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		SamplerDesc LinearClamp
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
		};
		SamplerDesc ShadowClamp = {};
		ShadowClamp.MinFilter = FILTER_TYPE_COMPARISON_LINEAR;
		ShadowClamp.MagFilter = FILTER_TYPE_COMPARISON_LINEAR;
		ShadowClamp.MipFilter = FILTER_TYPE_COMPARISON_LINEAR;
		ShadowClamp.AddressU = TEXTURE_ADDRESS_CLAMP;
		ShadowClamp.AddressV = TEXTURE_ADDRESS_CLAMP;
		ShadowClamp.AddressW = TEXTURE_ADDRESS_CLAMP;
		ShadowClamp.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
		ImmutableSamplerDesc Samplers[] =
		{
			{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", LinearClamp },
			{ SHADER_TYPE_PIXEL, "g_ShadowCmpSampler",   ShadowClamp  },
		};
		PSO.PSODesc.ResourceLayout.ImmutableSamplers = Samplers;
		PSO.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(Samplers);

		pDevice->CreateGraphicsPipelineState(PSO, &m_PSO_Lighting);
		ASSERT(m_PSO_Lighting, "Failed to create Lighting PSO.");

		// Bind FRAME_CONSTANTS as static
		if (auto* Var = m_PSO_Lighting->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
			Var->Set(m_pFrameCB);

		m_PSO_Lighting->CreateShaderResourceBinding(&m_SRB_Lighting, true);
		ASSERT(m_SRB_Lighting, "Failed to create SRB_Lighting.");

		// Set SRVs (mutable)
		if (auto var = m_SRB_Lighting->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer0")) var->Set(m_GBufferSRV[0]);
		if (auto var = m_SRB_Lighting->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer1")) var->Set(m_GBufferSRV[1]);
		if (auto var = m_SRB_Lighting->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer2")) var->Set(m_GBufferSRV[2]);
		if (auto var = m_SRB_Lighting->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer3")) var->Set(m_GBufferSRV[3]);
		if (auto var = m_SRB_Lighting->GetVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap")) var->Set(m_ShadowMapSRV);
		if (auto var = m_SRB_Lighting->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBufferDepth")) var->Set(m_GBufferDepthSRV);
		return true;
	}

	bool Renderer::createPostPSO()
	{
		IRenderDevice* pDevice = m_CreateInfo.pDevice.RawPtr();
		ISwapChain* pSwap = m_CreateInfo.pSwapChain.RawPtr();
		ASSERT(pDevice && pSwap, "ensurePostPSO(): device/swap null.");

		if (m_PSO_Post && m_SRB_Post)
			return true;

		GraphicsPipelineStateCreateInfo PSO = {};
		PSO.PSODesc.Name = "Post Copy PSO";
		PSO.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		auto& gp = PSO.GraphicsPipeline;

		gp.pRenderPass = m_RP_Lighting;
		gp.SubpassIndex = 0;

		gp.NumRenderTargets = 0;
		gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
		gp.DSVFormat = TEX_FORMAT_UNKNOWN;
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
		gp.RasterizerDesc.FrontCounterClockwise = true;
		gp.DepthStencilDesc.DepthEnable = false;

		ShaderCreateInfo sci = {};
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.EntryPoint = "main";
		sci.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR | SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION;

		RefCntAutoPtr<IShader> VS;
		{
			sci.Desc = {};
			sci.Desc.Name = "PostCopy VS";
			sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
			sci.FilePath = "PostCopy.vsh";
			sci.Desc.UseCombinedTextureSamplers = false;
			pDevice->CreateShader(sci, &VS);
			ASSERT(VS, "Failed to create PostCopy VS.");
		}

		RefCntAutoPtr<IShader> PS;
		{
			sci.Desc = {};
			sci.Desc.Name = "PostCopy PS";
			sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
			sci.FilePath = "PostCopy.psh";
			sci.Desc.UseCombinedTextureSamplers = false;
			pDevice->CreateShader(sci, &PS);
			ASSERT(PS, "Failed to create PostCopy PS.");
		}

		PSO.pVS = VS;
		PSO.pPS = PS;

		PSO.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{ SHADER_TYPE_PIXEL, "g_InputColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		PSO.PSODesc.ResourceLayout.Variables = Vars;
		PSO.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		SamplerDesc LinearClamp
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
		};
		ImmutableSamplerDesc Samplers[] =
		{
			{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", LinearClamp }
		};
		PSO.PSODesc.ResourceLayout.ImmutableSamplers = Samplers;
		PSO.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(Samplers);

		pDevice->CreateGraphicsPipelineState(PSO, &m_PSO_Post);
		ASSERT(m_PSO_Post, "Failed to create Post PSO.");

		m_PSO_Post->CreateShaderResourceBinding(&m_SRB_Post, true);
		ASSERT(m_SRB_Post, "Failed to create SRB_Post.");

		return true;
	}

	bool Renderer::createShadowPSO()
	{
		IRenderDevice* pDevice = m_CreateInfo.pDevice.RawPtr();
		ASSERT(pDevice, "createShadowPSO(): device null.");

		if (m_PSO_Shadow)
			return true;

		GraphicsPipelineStateCreateInfo PSO = {};
		PSO.PSODesc.Name = "Shadow Depth PSO";
		PSO.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		auto& gp = PSO.GraphicsPipeline;
		gp.pRenderPass = m_RP_Shadow;
		gp.SubpassIndex = 0;

		gp.NumRenderTargets = 0;
		gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
		gp.DSVFormat = TEX_FORMAT_UNKNOWN; // RenderPass 기반

		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
		gp.RasterizerDesc.FrontCounterClockwise = true;

		gp.DepthStencilDesc.DepthEnable = true;
		gp.DepthStencilDesc.DepthWriteEnable = true;
		gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

		LayoutElement layoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false},
		};
		layoutElems[0].Stride = sizeof(float) * 11;
		gp.InputLayout.LayoutElements = layoutElems;
		gp.InputLayout.NumElements = _countof(layoutElems);


		ShaderCreateInfo sci = {};
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		sci.EntryPoint = "main";
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR | SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION;

		RefCntAutoPtr<IShader> VS;
		{
			sci.Desc = {};
			sci.Desc.Name = "Shadow VS";
			sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
			sci.FilePath = "Shadow.vsh";
			sci.Desc.UseCombinedTextureSamplers = false;

			pDevice->CreateShader(sci, &VS);
			ASSERT(VS, "Failed to create ShadowDepth VS.");
		}

		RefCntAutoPtr<IShader> PS;
		{
			sci.Desc = {};
			sci.Desc.Name = "Shadow PS";
			sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
			sci.FilePath = "Shadow.psh";
			sci.Desc.UseCombinedTextureSamplers = false;

			pDevice->CreateShader(sci, &PS);
			ASSERT(PS, "Failed to create ShadowDepth PS.");
		}

		PSO.pVS = VS;
		PSO.pPS = PS;

		// Resource layout
		PSO.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{ SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
			{ SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_STATIC  },
		};
		PSO.PSODesc.ResourceLayout.Variables = Vars;
		PSO.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		pDevice->CreateGraphicsPipelineState(PSO, &m_PSO_Shadow);
		ASSERT(m_PSO_Shadow, "Failed to create Shadow PSO.");

		// Bind SHADOW_CONSTANTS as static
		if (auto* Var = m_PSO_Shadow->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS"))
			Var->Set(m_pShadowCB);

		m_PSO_Shadow->CreateShaderResourceBinding(&m_SRB_Shadow, true);
		ASSERT(m_SRB_Shadow, "Failed to create SRB_Shadow.");

		// OBJECT_CONSTANTS는 Dynamic이므로 SRB에 세팅
		if (auto* Var = m_SRB_Shadow->GetVariableByName(SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS"))
			Var->Set(m_pObjectCB);

		return true;
	}


	bool Renderer::createShadowTargets()
	{
		IRenderDevice* pDevice = m_CreateInfo.pDevice.RawPtr();
		ASSERT(pDevice, "createShadowTargets(): device null.");

		if (m_ShadowMapTex && m_ShadowMapDSV && m_ShadowMapSRV)
			return true;

		TextureDesc td = {};
		td.Name = "ShadowMap";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = kShadowMapSize;
		td.Height = kShadowMapSize;
		td.MipLevels = 1;
		td.SampleCount = 1;
		td.Usage = USAGE_DEFAULT;

		// 핵심: typeless + depth-stencil + shader-resource
		td.Format = TEX_FORMAT_R32_TYPELESS;
		td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

		m_ShadowMapTex.Release();
		m_ShadowMapDSV.Release();
		m_ShadowMapSRV.Release();

		pDevice->CreateTexture(td, nullptr, &m_ShadowMapTex);
		ASSERT(m_ShadowMapTex, "Failed to create ShadowMap texture.");

		// DSV view
		{
			TextureViewDesc vd = {};
			vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			vd.Format = TEX_FORMAT_D32_FLOAT;
			m_ShadowMapTex->CreateView(vd, &m_ShadowMapDSV);
			ASSERT(m_ShadowMapDSV, "Failed to create ShadowMap DSV.");
		}

		// SRV view (depth compare용으로 sampling할 거라 R32_FLOAT로)
		{
			TextureViewDesc vd = {};
			vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
			vd.Format = TEX_FORMAT_R32_FLOAT;
			m_ShadowMapTex->CreateView(vd, &m_ShadowMapSRV);
			ASSERT(m_ShadowMapSRV, "Failed to create ShadowMap SRV.");
		}

		// Shadow constants CB
		if (!m_pShadowCB)
			CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ShadowConstants), "Shadow constants CB", &m_pShadowCB);

		return true;
	}

	bool Renderer::createShadowRenderPasses()
	{
		IRenderDevice* pDevice = m_CreateInfo.pDevice.RawPtr();
		ASSERT(pDevice, "createShadowRenderPasses(): device null.");

		if (m_RP_Shadow && m_FB_Shadow)
			return true;

		// Depth-only RP
		RenderPassAttachmentDesc Attach[1] = {};
		Attach[0].Format = TEX_FORMAT_D32_FLOAT;
		Attach[0].SampleCount = 1;
		Attach[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Attach[0].StoreOp = ATTACHMENT_STORE_OP_STORE;
		Attach[0].InitialState = RESOURCE_STATE_DEPTH_WRITE;
		Attach[0].FinalState = RESOURCE_STATE_DEPTH_WRITE;

		AttachmentReference DepthRef = {};
		DepthRef.AttachmentIndex = 0;
		DepthRef.State = RESOURCE_STATE_DEPTH_WRITE;

		SubpassDesc Subpass = {};
		Subpass.RenderTargetAttachmentCount = 0;
		Subpass.pDepthStencilAttachment = &DepthRef;

		RenderPassDesc RP = {};
		RP.Name = "RP_Shadow";
		RP.AttachmentCount = 1;
		RP.pAttachments = Attach;
		RP.SubpassCount = 1;
		RP.pSubpasses = &Subpass;

		m_RP_Shadow.Release();
		pDevice->CreateRenderPass(RP, &m_RP_Shadow);
		ASSERT(m_RP_Shadow, "CreateRenderPass(RP_Shadow) failed.");

		// FB
		{
			ITextureView* Atch[1] = { m_ShadowMapDSV };

			FramebufferDesc FB = {};
			FB.Name = "FB_Shadow";
			FB.pRenderPass = m_RP_Shadow;
			FB.AttachmentCount = 1;
			FB.ppAttachments = Atch;

			m_FB_Shadow.Release();
			pDevice->CreateFramebuffer(FB, &m_FB_Shadow);
			ASSERT(m_FB_Shadow, "CreateFramebuffer(FB_Shadow) failed.");
		}

		return true;
	}


	bool Renderer::createDeferredTargets()
	{
		IRenderDevice* pDevice = m_CreateInfo.pDevice.RawPtr();
		ISwapChain* pSwap = m_CreateInfo.pSwapChain.RawPtr();
		ASSERT(pDevice && pSwap, "ensureDeferredTargets(): device/swapchain is null.");

		const SwapChainDesc& sc = pSwap->GetDesc();
		const uint32 W = (m_Width != 0) ? m_Width : sc.Width;
		const uint32 H = (m_Height != 0) ? m_Height : sc.Height;

		const bool needRebuild =
			(m_DeferredWidth != W) ||
			(m_DeferredHeight != H) ||
			!m_GBufferTex[0] || !m_GBufferTex[1] || !m_GBufferTex[2] || !m_GBufferTex[3] ||
			!m_GBufferDepthTex ||
			!m_LightingTex;

		if (!needRebuild)
			return true;

		m_DeferredWidth = W;
		m_DeferredHeight = H;

		// Must match createGBufferPSO() RTVFormats
		CreateRTTexture2D(pDevice, W, H, TEX_FORMAT_RGBA8_UNORM, "GBuffer0_AlbedoA", m_GBufferTex[0], m_GBufferRTV[0], m_GBufferSRV[0]);
		CreateRTTexture2D(pDevice, W, H, TEX_FORMAT_RGBA16_FLOAT, "GBuffer1_NormalWS", m_GBufferTex[1], m_GBufferRTV[1], m_GBufferSRV[1]);
		CreateRTTexture2D(pDevice, W, H, TEX_FORMAT_RGBA8_UNORM, "GBuffer2_MRAO", m_GBufferTex[2], m_GBufferRTV[2], m_GBufferSRV[2]);
		CreateRTTexture2D(pDevice, W, H, TEX_FORMAT_RGBA16_FLOAT, "GBuffer3_Emissive", m_GBufferTex[3], m_GBufferRTV[3], m_GBufferSRV[3]);

		// Lighting intermediate: simplest = swapchain format (LDR). HDR 원하면 RGBA16F로 변경.
		CreateRTTexture2D(pDevice, W, H, sc.ColorBufferFormat, "LightingColor", m_LightingTex, m_LightingRTV, m_LightingSRV);

		// GBuffer depth: typeless + DSV + SRV
		{
			TextureDesc td = {};
			td.Name = "GBufferDepth";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = W;
			td.Height = H;
			td.MipLevels = 1;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;

			// typeless so we can create DSV(D32) and SRV(R32)
			td.Format = TEX_FORMAT_R32_TYPELESS;
			td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

			m_GBufferDepthTex.Release();
			m_GBufferDepthDSV.Release();
			m_GBufferDepthSRV.Release();

			pDevice->CreateTexture(td, nullptr, &m_GBufferDepthTex);
			ASSERT(m_GBufferDepthTex, "Failed to create GBufferDepth texture.");

			// DSV
			{
				TextureViewDesc vd = {};
				vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
				vd.Format = TEX_FORMAT_D32_FLOAT;
				m_GBufferDepthTex->CreateView(vd, &m_GBufferDepthDSV);
				ASSERT(m_GBufferDepthDSV, "Failed to create GBufferDepth DSV.");
			}

			// SRV
			{
				TextureViewDesc vd = {};
				vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
				vd.Format = TEX_FORMAT_R32_FLOAT;
				m_GBufferDepthTex->CreateView(vd, &m_GBufferDepthSRV);
				ASSERT(m_GBufferDepthSRV, "Failed to create GBufferDepth SRV.");
			}
		}

		// RenderPass/Framebuffer는 타겟이 바뀌었으니 재생성 필요
		m_RP_GBuffer.Release();   m_FB_GBuffer.Release();
		m_RP_Lighting.Release();  m_FB_Lighting.Release();
		m_RP_Post.Release();      m_FB_Post.Release();

		// SRB도 입력 뷰가 바뀌었을 수 있으니 재바인딩 위해 유지하되, 안전하게 초기화
		// (PSO는 그대로 써도 되지만, 여기서는 단순하게 SRB는 다시 만들도록)
		m_SRB_Lighting.Release();
		m_SRB_Post.Release();

		return true;
	}

	bool Renderer::createDeferredRenderPasses()
	{
		IRenderDevice* pDevice = m_CreateInfo.pDevice.RawPtr();
		ISwapChain* pSwap = m_CreateInfo.pSwapChain.RawPtr();
		ASSERT(pDevice && pSwap, "ensureDeferredRenderPasses(): device/swapchain null.");

		const SwapChainDesc& sc = pSwap->GetDesc();

		// ----------------------------
		// GBuffer RenderPass/FB
		// ----------------------------
		if (!m_RP_GBuffer || !m_FB_GBuffer)
		{
			RenderPassAttachmentDesc Attach[5] = {};

			for (uint32 i = 0; i < 4; ++i)
			{
				Attach[i].Format = m_GBufferTex[i]->GetDesc().Format;
				Attach[i].SampleCount = 1;
				Attach[i].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
				Attach[i].StoreOp = ATTACHMENT_STORE_OP_STORE;
				Attach[i].InitialState = RESOURCE_STATE_RENDER_TARGET;
				Attach[i].FinalState = RESOURCE_STATE_RENDER_TARGET;
			}

			Attach[4].Format = TEX_FORMAT_D32_FLOAT;
			Attach[4].SampleCount = 1;
			Attach[4].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			Attach[4].StoreOp = ATTACHMENT_STORE_OP_STORE;
			Attach[4].InitialState = RESOURCE_STATE_DEPTH_WRITE;
			Attach[4].FinalState = RESOURCE_STATE_DEPTH_WRITE;

			AttachmentReference ColorRefs[4] = {};
			for (uint32 i = 0; i < 4; ++i)
			{
				ColorRefs[i].AttachmentIndex = i;
				ColorRefs[i].State = RESOURCE_STATE_RENDER_TARGET;
			}

			AttachmentReference DepthRef = {};
			DepthRef.AttachmentIndex = 4;
			DepthRef.State = RESOURCE_STATE_DEPTH_WRITE;

			SubpassDesc Subpass = {};
			Subpass.RenderTargetAttachmentCount = 4;
			Subpass.pRenderTargetAttachments = ColorRefs;
			Subpass.pDepthStencilAttachment = &DepthRef;

			RenderPassDesc RP = {};
			RP.Name = "RP_GBuffer";
			RP.AttachmentCount = 5;
			RP.pAttachments = Attach;
			RP.SubpassCount = 1;
			RP.pSubpasses = &Subpass;

			pDevice->CreateRenderPass(RP, &m_RP_GBuffer);
			ASSERT(m_RP_GBuffer, "CreateRenderPass(RP_GBuffer) failed.");

			ITextureView* Atch[5] =
			{
				m_GBufferRTV[0], m_GBufferRTV[1], m_GBufferRTV[2], m_GBufferRTV[3],
				m_GBufferDepthDSV
			};

			FramebufferDesc FB = {};
			FB.Name = "FB_GBuffer";
			FB.pRenderPass = m_RP_GBuffer;
			FB.AttachmentCount = 5;
			FB.ppAttachments = Atch;

			pDevice->CreateFramebuffer(FB, &m_FB_GBuffer);
			ASSERT(m_FB_GBuffer, "CreateFramebuffer(FB_GBuffer) failed.");
		}

		// ----------------------------
		// Lighting RenderPass/FB
		// ----------------------------
		if (!m_RP_Lighting || !m_FB_Lighting)
		{
			RenderPassAttachmentDesc Attach[1] = {};
			Attach[0].Format = m_LightingTex->GetDesc().Format;
			Attach[0].SampleCount = 1;
			Attach[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			Attach[0].StoreOp = ATTACHMENT_STORE_OP_STORE;
			Attach[0].InitialState = RESOURCE_STATE_RENDER_TARGET;
			Attach[0].FinalState = RESOURCE_STATE_RENDER_TARGET;

			AttachmentReference ColorRef = {};
			ColorRef.AttachmentIndex = 0;
			ColorRef.State = RESOURCE_STATE_RENDER_TARGET;

			SubpassDesc Subpass = {};
			Subpass.RenderTargetAttachmentCount = 1;
			Subpass.pRenderTargetAttachments = &ColorRef;

			RenderPassDesc RP = {};
			RP.Name = "RP_Lighting";
			RP.AttachmentCount = 1;
			RP.pAttachments = Attach;
			RP.SubpassCount = 1;
			RP.pSubpasses = &Subpass;

			pDevice->CreateRenderPass(RP, &m_RP_Lighting);
			ASSERT(m_RP_Lighting, "CreateRenderPass(RP_Lighting) failed.");

			ITextureView* Atch[1] = { m_LightingRTV };

			FramebufferDesc FB = {};
			FB.Name = "FB_Lighting";
			FB.pRenderPass = m_RP_Lighting;
			FB.AttachmentCount = 1;
			FB.ppAttachments = Atch;

			pDevice->CreateFramebuffer(FB, &m_FB_Lighting);
			ASSERT(m_FB_Lighting, "CreateFramebuffer(FB_Lighting) failed.");
		}

		// ----------------------------
		// Post RenderPass (FB는 매 프레임 BBRTV로 재생성)
		// ----------------------------
		if (!m_RP_Post)
		{
			RenderPassAttachmentDesc Attach[1] = {};
			Attach[0].Format = sc.ColorBufferFormat;
			Attach[0].SampleCount = 1;
			Attach[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			Attach[0].StoreOp = ATTACHMENT_STORE_OP_STORE;
			Attach[0].InitialState = RESOURCE_STATE_RENDER_TARGET;
			Attach[0].FinalState = RESOURCE_STATE_RENDER_TARGET;

			AttachmentReference ColorRef = {};
			ColorRef.AttachmentIndex = 0;
			ColorRef.State = RESOURCE_STATE_RENDER_TARGET;

			SubpassDesc Subpass = {};
			Subpass.RenderTargetAttachmentCount = 1;
			Subpass.pRenderTargetAttachments = &ColorRef;

			RenderPassDesc RP = {};
			RP.Name = "RP_Post";
			RP.AttachmentCount = 1;
			RP.pAttachments = Attach;
			RP.SubpassCount = 1;
			RP.pSubpasses = &Subpass;

			pDevice->CreateRenderPass(RP, &m_RP_Post);
			ASSERT(m_RP_Post, "CreateRenderPass(RP_Post) failed.");
		}

		return true;
	}


} // namespace shz
