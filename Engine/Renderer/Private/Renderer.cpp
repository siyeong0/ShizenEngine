#include "pch.h"
#include "Engine/Renderer/Public/Renderer.h"

#include <unordered_map>

#include "Engine/AssetRuntime/Public/AssetManager.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	// ------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------

	bool Renderer::Initialize(const RendererCreateInfo& createInfo)
	{
		ASSERT(createInfo.pDevice, "Device is null.");
		ASSERT(createInfo.pImmediateContext, "ImmediateContext is null.");
		ASSERT(createInfo.pSwapChain, "SwapChain is null.");
		ASSERT(createInfo.pAssetManager, "AssetManager is null.");
		ASSERT(createInfo.pShaderSourceFactory, "ShaderSourceFactory is null.");

		m_CreateInfo = createInfo;
		m_pAssetManager = createInfo.pAssetManager;
		m_pShaderSourceFactory = createInfo.pShaderSourceFactory;

		m_pCache = std::make_unique<RenderResourceCache>();
		m_pCache->Initialize(m_CreateInfo.pDevice.RawPtr());

		const SwapChainDesc& scDesc = m_CreateInfo.pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : scDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : scDesc.Height;

		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::FrameConstants), "Frame constants", &m_pFrameCB);
		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ObjectConstants), "Object constants", &m_pObjectCB);
		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ShadowConstants), "Shadow constants", &m_pShadowCB);

		m_ShadowDirty = true;
		m_DeferredDirty = true;

		// Post FB is per-frame (swapchain-backed). Keep it null here.
		m_FrameBufferPostCurrent.Release();

		if (!recreateShadowResources())
			return false;

		if (!recreateSizeDependentResources())
			return false;

		return true;
	}

	void Renderer::Cleanup()
	{
		// Swapchain-backed refs must be released first.
		ReleaseSwapChainBuffers();

		m_ShadowPSO.Release();
		m_GBufferPSO.Release();
		m_LightingPSO.Release();
		m_PostPSO.Release();

		m_ShadowSRB.Release();
		m_LightingSRB.Release();
		m_PostSRB.Release();

		m_ShadowMapTex.Release();
		m_ShadowMapDsv.Release();
		m_ShadowMapSrv.Release();

		m_GBufferDepthTex.Release();
		m_GBufferDepthDSV.Release();
		m_GBufferDepthSRV.Release();

		for (uint32 i = 0; i < NUM_GBUFFERS; ++i)
		{
			m_GBufferTex[i].Release();
			m_GBufferRtv[i].Release();
			m_GBufferSrv[i].Release();
		}

		m_LightingTex.Release();
		m_LightingRTV.Release();
		m_LightingSRV.Release();

		m_RenderPassShadow.Release();
		m_FrameBufferShadow.Release();
		m_RenderPassGBuffer.Release();
		m_FrameBufferGBuffer.Release();
		m_RenderPassLighting.Release();
		m_FrameBufferLighting.Release();
		m_RenderPassPost.Release();

		m_pShadowCB.Release();
		m_pFrameCB.Release();
		m_pObjectCB.Release();

		if (m_pCache)
		{
			m_pCache->Shutdown();
			m_pCache.reset();
		}

		m_pShaderSourceFactory.Release();
		m_pAssetManager = nullptr;

		m_CreateInfo = {};
		m_Width = 0;
		m_Height = 0;
		m_DeferredWidth = 0;
		m_DeferredHeight = 0;
	}

	void Renderer::ReleaseSwapChainBuffers()
	{
		// IMPORTANT:
		// Release anything that can hold references to swapchain backbuffers.
		// If any framebuffer references backbuffer RTV, DXGI fullscreen/ResizeBuffers may fail. :contentReference[oaicite:1]{index=1}
		m_FrameBufferPostCurrent.Release();

		// If you ever add per-backbuffer caches, clear them here as well.

		// Note:
		// Offscreen resources are not swapchain buffers, so they don't have to be released for fullscreen toggle.
		// But if your platform path also recreates them, releasing is fine.
	}

	void Renderer::OnResize(uint32 width, uint32 height)
	{
		m_Width = width;
		m_Height = height;

		m_DeferredDirty = true;

		// Size-dependent resources must be rebuilt after swapchain resize is done.
		recreateSizeDependentResources();
	}

	// ------------------------------------------------------------
	// Frame
	// ------------------------------------------------------------

	void Renderer::BeginFrame()
	{
		// Ensure we don't keep swapchain backbuffer refs across frames.
		// This makes fullscreen toggle much more robust even if the app toggles right after Present.
		m_FrameBufferPostCurrent.Release();

		// Build swapchain-backed framebuffer for the current backbuffer in BeginFrame (NOT in Render).
		// If this fails, Render() should early-out via ASSERT checks or nullptr checks.
		buildPostFramebufferForCurrentBackBuffer();
	}

	void Renderer::EndFrame()
	{
		// Release swapchain-backed framebuffer at end of the frame
		// so that DXGI can freely resize/toggle fullscreen next frame. :contentReference[oaicite:2]{index=2}
		m_FrameBufferPostCurrent.Release();
	}

	bool Renderer::buildPostFramebufferForCurrentBackBuffer()
	{
		IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();
		ISwapChain* sc = m_CreateInfo.pSwapChain.RawPtr();

		ASSERT(dev, "Render device is null.");
		ASSERT(sc, "SwapChain is null.");
		ASSERT(m_RenderPassPost, "Post render pass is null.");

		ITextureView* bbRtv = sc->GetCurrentBackBufferRTV();
		if (!bbRtv)
		{
			// Some backends (e.g. GL path) may handle this differently.
			// For now, enforce having RTV.
			ASSERT(false, "Current backbuffer RTV is null.");
			return false;
		}

		FramebufferDesc fb = {};
		fb.Name = "FB_Post_CurrentBackBuffer";
		fb.pRenderPass = m_RenderPassPost;
		fb.AttachmentCount = 1;
		fb.ppAttachments = &bbRtv;

		m_FrameBufferPostCurrent.Release();
		dev->CreateFramebuffer(fb, &m_FrameBufferPostCurrent);

		if (!m_FrameBufferPostCurrent)
		{
			ASSERT(false, "Failed to create post framebuffer for current backbuffer.");
			return false;
		}

		// Bind post SRB input here (per-frame is ok).
		if (m_PostSRB)
		{
			if (auto* v = m_PostSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_InputColor"))
				v->Set(m_LightingSRV);
		}

		return true;
	}

	// ------------------------------------------------------------
	// Render
	// ------------------------------------------------------------

	void Renderer::Render(const RenderScene& scene, const ViewFamily& viewFamily)
	{
		IDeviceContext* ctx = m_CreateInfo.pImmediateContext.RawPtr();
		ISwapChain* sc = m_CreateInfo.pSwapChain.RawPtr();
		IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();

		ASSERT(ctx, "Immediate context is null.");
		ASSERT(sc, "Swap chain is null.");
		ASSERT(dev, "Render device is null.");
		ASSERT(m_pCache, "RenderResourceCache is null.");

		ASSERT(!viewFamily.Views.empty(), "ViewFamily has no views.");
		ASSERT(viewFamily.Views.size() <= 1, "Only single view is supported currently.");

		// Post FB must be created in BeginFrame.
		ASSERT(m_RenderPassPost, "Post-process render pass is null.");
		ASSERT(m_PostPSO, "Post-process PSO is null.");
		ASSERT(m_PostSRB, "Post-process SRB is null.");
		ASSERT(m_FrameBufferPostCurrent, "Post framebuffer (current backbuffer) is null. Call BeginFrame() first.");

		ASSERT(m_ShadowMapTex, "Shadow map texture is null.");
		ASSERT(m_RenderPassShadow, "Shadow render pass is null.");
		ASSERT(m_ShadowPSO, "Shadow PSO is null.");
		ASSERT(m_ShadowSRB, "Shadow SRB is null.");

		ASSERT(m_GBufferTex[0], "G-Buffer texture is null.");
		ASSERT(m_GBufferDepthTex, "G-Buffer depth texture is null.");
		ASSERT(m_RenderPassGBuffer, "G-Buffer render pass is null.");
		ASSERT(m_GBufferPSO, "G-Buffer PSO is null.");

		ASSERT(m_LightingTex, "Lighting texture is null.");
		ASSERT(m_RenderPassLighting, "Lighting render pass is null.");
		ASSERT(m_LightingPSO, "Lighting PSO is null.");
		ASSERT(m_LightingSRB, "Lighting SRB is null.");

		const View& view = viewFamily.Views[0];

		// ------------------------------------------------------------
		// Update frame constants
		// ------------------------------------------------------------
		Matrix4x4 lightViewProj = {};

		{
			MapHelper<hlsl::FrameConstants> cb(ctx, m_pFrameCB, MAP_WRITE, MAP_FLAG_DISCARD);

			cb->View = view.ViewMatrix;
			cb->Proj = view.ProjMatrix;
			cb->ViewProj = view.ViewMatrix * view.ProjMatrix;
			cb->InvViewProj = cb->ViewProj.Inversed();

			cb->CameraPosition = view.CameraPosition;

			cb->ViewportSize =
			{
				static_cast<float>(view.Viewport.right - view.Viewport.left),
				static_cast<float>(view.Viewport.bottom - view.Viewport.top)
			};

			cb->InvViewportSize =
			{
				1.f / cb->ViewportSize.x,
				1.f / cb->ViewportSize.y
			};

			cb->NearPlane = view.NearPlane;
			cb->FarPlane = view.FarPlane;
			cb->DeltaTime = viewFamily.DeltaTime;
			cb->CurrTime = viewFamily.CurrentTime;

			// Light (pick the first one)
			const RenderScene::LightObject* globalLight = nullptr;
			for (const auto& l : scene.GetLights())
			{
				globalLight = &l;
				break;
			}

			float3 lightDirWs = globalLight ? globalLight->Direction.Normalized() : float3(0, -1, 0);
			float3 lightColor = globalLight ? globalLight->Color : float3(1, 1, 1);
			float  lightIntensity = globalLight ? globalLight->Intensity : 1.0f;

			const float3 lightForward = lightDirWs;
			const float3 centerWs = view.CameraPosition;

			const float shadowDistance = 20.0f;
			const float3 lightPosWs = centerWs - lightForward * shadowDistance;

			float3 up = float3(0, 1, 0);
			if (Abs(Vector3::Dot(up, lightForward)) > 0.99f)
				up = float3(0, 0, 1);

			const Matrix4x4 lightView = Matrix4x4::LookAtLH(lightPosWs, centerWs, up);

			const float r = 25.0f;
			const Matrix4x4 lightProj = Matrix4x4::OrthoOffCenter(-r, +r, -r, +r, -r, +r);
			lightViewProj = lightView * lightProj;

			cb->LightViewProj = lightViewProj;
			cb->LightDirWS = lightDirWs;
			cb->LightColor = lightColor;
			cb->LightIntensity = lightIntensity;
		}

		{
			MapHelper<hlsl::ShadowConstants> cb(ctx, m_pShadowCB, MAP_WRITE, MAP_FLAG_DISCARD);
			cb->LightViewProj = lightViewProj;
		}

		auto drawFullScreenTriangle = [&]()
			{
				DrawAttribs da = {};
				da.NumVertices = 3;
				da.Flags = DRAW_FLAG_VERIFY_ALL;
				ctx->Draw(da);
			};

		// ============================================================
		// Pre-Transition (VERIFY safe)
		// ============================================================

		std::vector<StateTransitionDesc> preBarriers;
		preBarriers.reserve(512);

		auto pushBarrier = [&](IDeviceObject* pObj, RESOURCE_STATE from, RESOURCE_STATE to)
			{
				if (!pObj) return;
				StateTransitionDesc b = {};
				b.pResource = pObj;
				b.OldState = from;
				b.NewState = to;
				b.Flags = STATE_TRANSITION_FLAG_UPDATE_STATE;
				preBarriers.push_back(b);
			};

		pushBarrier(m_pFrameCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		pushBarrier(m_pObjectCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		pushBarrier(m_pShadowCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

		std::unordered_map<uint64, Handle<MaterialRenderData>> frameMat;
		frameMat.reserve(256);

		for (const auto& obj : scene.GetObjects())
		{
			const StaticMeshRenderData* mesh = m_pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
			if (!mesh || !mesh->IsValid())
				continue;

			pushBarrier(mesh->GetVertexBuffer(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
			pushBarrier(mesh->GetIndexBuffer(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

			for (const auto& sec : mesh->GetSections())
			{
				if (sec.IndexCount == 0)
					continue;

				const MaterialInstance* inst = &obj.Materials[sec.MaterialSlot];
				if (!inst)
					continue;

				const MaterialTemplate* tmpl = inst->GetTemplate();
				if (!tmpl)
					continue;

				const uint64 key = reinterpret_cast<uint64>(inst);

				Handle<MaterialRenderData> hRD = {};
				if (auto it = frameMat.find(key); it != frameMat.end())
				{
					hRD = it->second;
				}
				else
				{
					hRD = m_pCache->GetOrCreateMaterialRenderData(inst, m_GBufferPSO.RawPtr(), tmpl);
					frameMat.emplace(key, hRD);
				}

				MaterialRenderData* rd = m_pCache->TryGetMaterialRenderData(hRD);
				if (rd)
				{
					if (auto* matCB = rd->GetMaterialConstantsBuffer())
						pushBarrier(matCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

					rd->Apply(m_pCache.get(), *inst, ctx);

					for (const auto& hTexRD : rd->GetBoundTextures())
					{
						if (auto* texRD = m_pCache->TryGetTextureRenderData(hTexRD))
							pushBarrier(texRD->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
					}
				}
			}
		}

		if (!preBarriers.empty())
			ctx->TransitionResourceStates(static_cast<uint32>(preBarriers.size()), preBarriers.data());

		// ============================================================
		// PASS 0: Shadow
		// ============================================================

		{
			StateTransitionDesc tr =
			{
				m_ShadowMapTex,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_DEPTH_WRITE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			ctx->TransitionResourceStates(1, &tr);

			Viewport vp = {};
			vp.Width = float(SHADOW_MAP_SIZE);
			vp.Height = float(SHADOW_MAP_SIZE);
			vp.MinDepth = 0.f;
			vp.MaxDepth = 1.f;
			ctx->SetViewports(1, &vp, 0, 0);

			OptimizedClearValue clearVals[1] = {};
			clearVals[0].DepthStencil.Depth = 1.f;
			clearVals[0].DepthStencil.Stencil = 0;

			BeginRenderPassAttribs rp = {};
			rp.pRenderPass = m_RenderPassShadow;
			rp.pFramebuffer = m_FrameBufferShadow;
			rp.ClearValueCount = 1;
			rp.pClearValues = clearVals;

			ctx->BeginRenderPass(rp);

			ctx->SetPipelineState(m_ShadowPSO);
			ctx->CommitShaderResources(m_ShadowSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			for (const auto& obj : scene.GetObjects())
			{
				const StaticMeshRenderData* mesh = m_pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				if (!mesh || !mesh->IsValid())
					continue;

				{
					MapHelper<hlsl::ObjectConstants> ocb(ctx, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
					ocb->World = obj.Transform;
					ocb->WorldInvTranspose = obj.Transform.Inversed().Transposed();
				}

				{
					IBuffer* vbs[] = { mesh->GetVertexBuffer() };
					uint64   offs[] = { 0 };
					ctx->SetVertexBuffers(0, 1, vbs, offs,
						RESOURCE_STATE_TRANSITION_MODE_VERIFY,
						SET_VERTEX_BUFFERS_FLAG_RESET);

					ctx->SetIndexBuffer(mesh->GetIndexBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
				}

				const VALUE_TYPE indexType = mesh->GetIndexType();

				for (const auto& sec : mesh->GetSections())
				{
					if (sec.IndexCount == 0)
						continue;

					DrawIndexedAttribs dia = {};
					dia.NumIndices = sec.IndexCount;
					dia.IndexType = indexType;
					dia.Flags = DRAW_FLAG_VERIFY_ALL;
					dia.FirstIndexLocation = sec.FirstIndex;
					dia.BaseVertex = static_cast<int32>(sec.BaseVertex);

					ctx->DrawIndexed(dia);
				}
			}

			ctx->EndRenderPass();

			StateTransitionDesc tr2 =
			{
				m_ShadowMapTex,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_SHADER_RESOURCE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			ctx->TransitionResourceStates(1, &tr2);

			setViewportFromView(view);
		}

		// ============================================================
		// PASS 1: GBuffer
		// ============================================================

		{
			StateTransitionDesc tr[] =
			{
				{ m_GBufferTex[0],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[1],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[2],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[3],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferDepthTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_DEPTH_WRITE,   STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			ctx->TransitionResourceStates(_countof(tr), tr);

			OptimizedClearValue clearVals[5] = {};
			for (int i = 0; i < 4; ++i)
			{
				clearVals[i].Color[0] = 0.f;
				clearVals[i].Color[1] = 0.f;
				clearVals[i].Color[2] = 0.f;
				clearVals[i].Color[3] = 0.f;
			}
			clearVals[4].DepthStencil.Depth = 1.f;
			clearVals[4].DepthStencil.Stencil = 0;

			BeginRenderPassAttribs rp = {};
			rp.pRenderPass = m_RenderPassGBuffer;
			rp.pFramebuffer = m_FrameBufferGBuffer;
			rp.ClearValueCount = 5;
			rp.pClearValues = clearVals;

			ctx->BeginRenderPass(rp);

			for (const auto& obj : scene.GetObjects())
			{
				const StaticMeshRenderData* mesh = m_pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				if (!mesh || !mesh->IsValid())
					continue;

				{
					MapHelper<hlsl::ObjectConstants> ocb(ctx, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
					ocb->World = obj.Transform;
					ocb->WorldInvTranspose = obj.Transform.Inversed().Transposed();
				}

				{
					IBuffer* vbs[] = { mesh->GetVertexBuffer() };
					uint64   offs[] = { 0 };
					ctx->SetVertexBuffers(0, 1, vbs, offs,
						RESOURCE_STATE_TRANSITION_MODE_VERIFY,
						SET_VERTEX_BUFFERS_FLAG_RESET);

					ctx->SetIndexBuffer(mesh->GetIndexBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
				}

				const VALUE_TYPE indexType = mesh->GetIndexType();

				for (const auto& sec : mesh->GetSections())
				{
					if (sec.IndexCount == 0)
						continue;

					const MaterialInstance* inst = &obj.Materials[sec.MaterialSlot];
					if (!inst)
						continue;

					const uint64 key = reinterpret_cast<uint64>(inst);

					Handle<MaterialRenderData> hRD = {};
					if (auto it = frameMat.find(key); it != frameMat.end())
					{
						hRD = it->second;
					}
					else
					{
						const MaterialTemplate* tmpl = inst->GetTemplate();
						if (!tmpl)
							continue;

						hRD = m_pCache->GetOrCreateMaterialRenderData(inst, m_GBufferPSO.RawPtr(), tmpl);
						frameMat.emplace(key, hRD);
					}

					MaterialRenderData* rd = m_pCache->TryGetMaterialRenderData(hRD);
					if (!rd || !rd->GetPSO() || !rd->GetSRB())
						continue;

					if (auto* v = rd->GetSRB()->GetVariableByName(SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS"))
						v->Set(m_pObjectCB);

					ctx->SetPipelineState(rd->GetPSO());
					ctx->CommitShaderResources(rd->GetSRB(), RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					DrawIndexedAttribs dia = {};
					dia.NumIndices = sec.IndexCount;
					dia.IndexType = indexType;
					dia.Flags = DRAW_FLAG_VERIFY_ALL;
					dia.FirstIndexLocation = sec.FirstIndex;
					dia.BaseVertex = static_cast<int32>(sec.BaseVertex);

					ctx->DrawIndexed(dia);
				}
			}

			ctx->EndRenderPass();

			StateTransitionDesc tr2[] =
			{
				{ m_GBufferTex[0],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[1],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[2],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[3],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferDepthTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
			};
			ctx->TransitionResourceStates(_countof(tr2), tr2);
		}

		// ============================================================
		// PASS 2: Lighting
		// ============================================================

		{
			StateTransitionDesc tr =
			{
				m_LightingTex,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_RENDER_TARGET,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			ctx->TransitionResourceStates(1, &tr);

			OptimizedClearValue cv[1] = {};
			cv[0].Color[0] = 0.f;
			cv[0].Color[1] = 0.f;
			cv[0].Color[2] = 0.f;
			cv[0].Color[3] = 1.f;

			BeginRenderPassAttribs rp = {};
			rp.pRenderPass = m_RenderPassLighting;
			rp.pFramebuffer = m_FrameBufferLighting;
			rp.ClearValueCount = 1;
			rp.pClearValues = cv;

			ctx->BeginRenderPass(rp);
			ctx->SetPipelineState(m_LightingPSO);
			ctx->CommitShaderResources(m_LightingSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			drawFullScreenTriangle();
			ctx->EndRenderPass();

			StateTransitionDesc tr2 =
			{
				m_LightingTex,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_SHADER_RESOURCE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			ctx->TransitionResourceStates(1, &tr2);
		}

		// ============================================================
		// PASS 3: Post (swapchain backbuffer)
		// ============================================================

		{
			// Ensure backbuffer is in RT state before render pass.
			ITextureView* bbRtv = sc->GetCurrentBackBufferRTV();
			ASSERT(bbRtv, "Backbuffer RTV is null.");

			StateTransitionDesc tr =
			{
				bbRtv->GetTexture(),
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_RENDER_TARGET,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			ctx->TransitionResourceStates(1, &tr);

			OptimizedClearValue cv[1] = {};
			cv[0].Color[0] = 0.f;
			cv[0].Color[1] = 0.f;
			cv[0].Color[2] = 0.f;
			cv[0].Color[3] = 1.f;

			BeginRenderPassAttribs rp = {};
			rp.pRenderPass = m_RenderPassPost;
			rp.pFramebuffer = m_FrameBufferPostCurrent;
			rp.ClearValueCount = 1;
			rp.pClearValues = cv;

			ctx->BeginRenderPass(rp);
			ctx->SetPipelineState(m_PostPSO);
			ctx->CommitShaderResources(m_PostSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			drawFullScreenTriangle();
			ctx->EndRenderPass();
		}
	}

	// ------------------------------------------------------------
	// Assets -> RD
	// ------------------------------------------------------------

	Handle<StaticMeshRenderData> Renderer::CreateStaticMesh(const StaticMeshAsset& asset)
	{
		ASSERT(m_pCache, "RenderResourceCache is null.");
		return m_pCache->GetOrCreateStaticMeshRenderData(asset, m_CreateInfo.pImmediateContext.RawPtr());
	}

	bool Renderer::DestroyStaticMesh(Handle<StaticMeshRenderData> hMesh)
	{
		ASSERT(m_pCache, "RenderResourceCache is null.");
		return m_pCache->DestroyStaticMeshRenderData(hMesh);
	}

	// ============================================================
	// Targets / RenderPass
	// ============================================================

	bool Renderer::createShadowTargets()
	{
		auto* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "device null");

		if (m_ShadowMapTex && m_ShadowMapDsv && m_ShadowMapSrv && m_pShadowCB)
			return true;

		TextureDesc td = {};
		td.Name = "ShadowMap";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = SHADOW_MAP_SIZE;
		td.Height = SHADOW_MAP_SIZE;
		td.MipLevels = 1;
		td.SampleCount = 1;
		td.Usage = USAGE_DEFAULT;
		td.Format = TEX_FORMAT_R32_TYPELESS;
		td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

		m_ShadowMapTex.Release();
		m_ShadowMapDsv.Release();
		m_ShadowMapSrv.Release();

		device->CreateTexture(td, nullptr, &m_ShadowMapTex);
		if (!m_ShadowMapTex)
			return false;

		{
			TextureViewDesc vd = {};
			vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			vd.Format = TEX_FORMAT_D32_FLOAT;
			m_ShadowMapTex->CreateView(vd, &m_ShadowMapDsv);
		}

		{
			TextureViewDesc vd = {};
			vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
			vd.Format = TEX_FORMAT_R32_FLOAT;
			m_ShadowMapTex->CreateView(vd, &m_ShadowMapSrv);
		}

		return (m_ShadowMapDsv && m_ShadowMapSrv);
	}

	bool Renderer::createDeferredTargets()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ISwapChain* swapChain = m_CreateInfo.pSwapChain.RawPtr();
		ASSERT(device && swapChain, "createDeferredTargets(): device/swapchain is null.");

		const SwapChainDesc& sc = swapChain->GetDesc();

		const uint32 w = (m_Width != 0) ? m_Width : sc.Width;
		const uint32 h = (m_Height != 0) ? m_Height : sc.Height;

		const bool needRebuild =
			(m_DeferredWidth != w) ||
			(m_DeferredHeight != h) ||
			!m_GBufferTex[0] || !m_GBufferTex[1] || !m_GBufferTex[2] || !m_GBufferTex[3] ||
			!m_GBufferDepthTex ||
			!m_LightingTex;

		if (!needRebuild)
			return true;

		m_DeferredWidth = w;
		m_DeferredHeight = h;

		auto createRtTexture2d = [&](uint32 width, uint32 height, TEXTURE_FORMAT fmt, const char* name,
			RefCntAutoPtr<ITexture>& outTex,
			RefCntAutoPtr<ITextureView>& outRtv,
			RefCntAutoPtr<ITextureView>& outSrv)
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
				outRtv.Release();
				outSrv.Release();

				device->CreateTexture(td, nullptr, &outTex);
				ASSERT(outTex, "Failed to create RT texture.");

				outRtv = outTex->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
				outSrv = outTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
				ASSERT(outRtv && outSrv, "RTV/SRV is null.");
			};

		createRtTexture2d(w, h, TEX_FORMAT_RGBA8_UNORM, "GBuffer0_AlbedoA", m_GBufferTex[0], m_GBufferRtv[0], m_GBufferSrv[0]);
		createRtTexture2d(w, h, TEX_FORMAT_RGBA16_FLOAT, "GBuffer1_NormalWS", m_GBufferTex[1], m_GBufferRtv[1], m_GBufferSrv[1]);
		createRtTexture2d(w, h, TEX_FORMAT_RGBA8_UNORM, "GBuffer2_MRAO", m_GBufferTex[2], m_GBufferRtv[2], m_GBufferSrv[2]);
		createRtTexture2d(w, h, TEX_FORMAT_RGBA16_FLOAT, "GBuffer3_Emissive", m_GBufferTex[3], m_GBufferRtv[3], m_GBufferSrv[3]);

		createRtTexture2d(w, h, sc.ColorBufferFormat, "LightingColor", m_LightingTex, m_LightingRTV, m_LightingSRV);

		{
			TextureDesc td = {};
			td.Name = "GBufferDepth";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = w;
			td.Height = h;
			td.MipLevels = 1;
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.Format = TEX_FORMAT_R32_TYPELESS;
			td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

			m_GBufferDepthTex.Release();
			m_GBufferDepthDSV.Release();
			m_GBufferDepthSRV.Release();

			device->CreateTexture(td, nullptr, &m_GBufferDepthTex);
			ASSERT(m_GBufferDepthTex, "Failed to create GBufferDepth texture.");

			{
				TextureViewDesc vd = {};
				vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
				vd.Format = TEX_FORMAT_D32_FLOAT;
				m_GBufferDepthTex->CreateView(vd, &m_GBufferDepthDSV);
				ASSERT(m_GBufferDepthDSV, "Failed to create GBufferDepth DSV.");
			}

			{
				TextureViewDesc vd = {};
				vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
				vd.Format = TEX_FORMAT_R32_FLOAT;
				m_GBufferDepthTex->CreateView(vd, &m_GBufferDepthSRV);
				ASSERT(m_GBufferDepthSRV, "Failed to create GBufferDepth SRV.");
			}
		}

		// Size-dependent pipeline objects should be re-created.
		m_RenderPassGBuffer.Release();
		m_FrameBufferGBuffer.Release();
		m_RenderPassLighting.Release();
		m_FrameBufferLighting.Release();
		m_RenderPassPost.Release();

		m_GBufferPSO.Release();
		m_LightingPSO.Release();
		m_PostPSO.Release();

		m_LightingSRB.Release();
		m_PostSRB.Release();

		// Swapchain-backed fb is per-frame.
		m_FrameBufferPostCurrent.Release();

		m_DeferredDirty = false;
		return true;
	}

	bool Renderer::createShadowRenderPasses()
	{
		auto* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "device null");

		if (m_RenderPassShadow && m_FrameBufferShadow)
			return true;

		{
			RenderPassAttachmentDesc at[1] = {};
			at[0].Format = TEX_FORMAT_D32_FLOAT;
			at[0].SampleCount = 1;
			at[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			at[0].StoreOp = ATTACHMENT_STORE_OP_STORE;
			at[0].StencilLoadOp = ATTACHMENT_LOAD_OP_DISCARD;
			at[0].StencilStoreOp = ATTACHMENT_STORE_OP_DISCARD;
			at[0].InitialState = RESOURCE_STATE_DEPTH_WRITE;
			at[0].FinalState = RESOURCE_STATE_DEPTH_WRITE;

			AttachmentReference depthRef = {};
			depthRef.AttachmentIndex = 0;
			depthRef.State = RESOURCE_STATE_DEPTH_WRITE;

			SubpassDesc sp = {};
			sp.RenderTargetAttachmentCount = 0;
			sp.pDepthStencilAttachment = &depthRef;

			RenderPassDesc rp = {};
			rp.Name = "RP_Shadow";
			rp.AttachmentCount = 1;
			rp.pAttachments = at;
			rp.SubpassCount = 1;
			rp.pSubpasses = &sp;

			m_RenderPassShadow.Release();
			device->CreateRenderPass(rp, &m_RenderPassShadow);
			if (!m_RenderPassShadow)
				return false;
		}

		{
			ITextureView* atch[1] = { m_ShadowMapDsv };

			FramebufferDesc fb = {};
			fb.Name = "FB_Shadow";
			fb.pRenderPass = m_RenderPassShadow;
			fb.AttachmentCount = 1;
			fb.ppAttachments = atch;

			m_FrameBufferShadow.Release();
			device->CreateFramebuffer(fb, &m_FrameBufferShadow);
			if (!m_FrameBufferShadow)
				return false;
		}

		return true;
	}

	bool Renderer::createDeferredRenderPasses()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ISwapChain* swapChain = m_CreateInfo.pSwapChain.RawPtr();
		ASSERT(device && swapChain, "createDeferredRenderPasses(): device/swapchain is null.");

		const SwapChainDesc& scDesc = swapChain->GetDesc();

		// ----------------------------
		// GBuffer render pass + FB
		// ----------------------------
		if (!m_RenderPassGBuffer || !m_FrameBufferGBuffer)
		{
			RenderPassAttachmentDesc attachments[5] = {};

			// GBuffer color attachments.
			for (uint32 i = 0; i < 4; ++i)
			{
				ASSERT(m_GBufferTex[i], "createDeferredRenderPasses(): GBuffer texture is null.");

				attachments[i].Format = m_GBufferTex[i]->GetDesc().Format;
				attachments[i].SampleCount = 1;
				attachments[i].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
				attachments[i].StoreOp = ATTACHMENT_STORE_OP_STORE;
				attachments[i].StencilLoadOp = ATTACHMENT_LOAD_OP_DISCARD;
				attachments[i].StencilStoreOp = ATTACHMENT_STORE_OP_DISCARD;
				attachments[i].InitialState = RESOURCE_STATE_RENDER_TARGET;
				attachments[i].FinalState = RESOURCE_STATE_RENDER_TARGET;
			}

			// Depth attachment.
			attachments[4].Format = TEX_FORMAT_D32_FLOAT;
			attachments[4].SampleCount = 1;
			attachments[4].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			attachments[4].StoreOp = ATTACHMENT_STORE_OP_STORE;
			attachments[4].StencilLoadOp = ATTACHMENT_LOAD_OP_DISCARD;
			attachments[4].StencilStoreOp = ATTACHMENT_STORE_OP_DISCARD;
			attachments[4].InitialState = RESOURCE_STATE_DEPTH_WRITE;
			attachments[4].FinalState = RESOURCE_STATE_DEPTH_WRITE;

			AttachmentReference colorRefs[4] = {};
			for (uint32 i = 0; i < 4; ++i)
			{
				colorRefs[i].AttachmentIndex = i;
				colorRefs[i].State = RESOURCE_STATE_RENDER_TARGET;
			}

			AttachmentReference depthRef = {};
			depthRef.AttachmentIndex = 4;
			depthRef.State = RESOURCE_STATE_DEPTH_WRITE;

			SubpassDesc subpass = {};
			subpass.RenderTargetAttachmentCount = 4;
			subpass.pRenderTargetAttachments = colorRefs;
			subpass.pDepthStencilAttachment = &depthRef;

			RenderPassDesc rpDesc = {};
			rpDesc.Name = "RP_GBuffer";
			rpDesc.AttachmentCount = 5;
			rpDesc.pAttachments = attachments;
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			m_RenderPassGBuffer.Release();
			device->CreateRenderPass(rpDesc, &m_RenderPassGBuffer);
			if (!m_RenderPassGBuffer)
			{
				ASSERT(false, "createDeferredRenderPasses(): CreateRenderPass(RP_GBuffer) failed.");
				return false;
			}

			ITextureView* atch[5] =
			{
				m_GBufferRtv[0],
				m_GBufferRtv[1],
				m_GBufferRtv[2],
				m_GBufferRtv[3],
				m_GBufferDepthDSV
			};

			FramebufferDesc fbDesc = {};
			fbDesc.Name = "FB_GBuffer";
			fbDesc.pRenderPass = m_RenderPassGBuffer;
			fbDesc.AttachmentCount = 5;
			fbDesc.ppAttachments = atch;

			m_FrameBufferGBuffer.Release();
			device->CreateFramebuffer(fbDesc, &m_FrameBufferGBuffer);
			if (!m_FrameBufferGBuffer)
			{
				ASSERT(false, "createDeferredRenderPasses(): CreateFramebuffer(FB_GBuffer) failed.");
				return false;
			}
		}

		// ----------------------------
		// Lighting render pass + FB
		// ----------------------------
		if (!m_RenderPassLighting || !m_FrameBufferLighting)
		{
			ASSERT(m_LightingTex, "createDeferredRenderPasses(): Lighting texture is null.");

			RenderPassAttachmentDesc attachments[1] = {};
			attachments[0].Format = m_LightingTex->GetDesc().Format;
			attachments[0].SampleCount = 1;
			attachments[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			attachments[0].StoreOp = ATTACHMENT_STORE_OP_STORE;

			attachments[0].InitialState = RESOURCE_STATE_RENDER_TARGET;
			attachments[0].FinalState = RESOURCE_STATE_RENDER_TARGET;

			AttachmentReference colorRef = {};
			colorRef.AttachmentIndex = 0;
			colorRef.State = RESOURCE_STATE_RENDER_TARGET;

			SubpassDesc subpass = {};
			subpass.RenderTargetAttachmentCount = 1;
			subpass.pRenderTargetAttachments = &colorRef;

			RenderPassDesc rpDesc = {};
			rpDesc.Name = "RP_Lighting";
			rpDesc.AttachmentCount = 1;
			rpDesc.pAttachments = attachments;
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			m_RenderPassLighting.Release();
			device->CreateRenderPass(rpDesc, &m_RenderPassLighting);
			if (!m_RenderPassLighting)
			{
				ASSERT(false, "createDeferredRenderPasses(): CreateRenderPass(RP_Lighting) failed.");
				return false;
			}

			ITextureView* atch[1] = { m_LightingRTV };

			FramebufferDesc fbDesc = {};
			fbDesc.Name = "FB_Lighting";
			fbDesc.pRenderPass = m_RenderPassLighting;
			fbDesc.AttachmentCount = 1;
			fbDesc.ppAttachments = atch;

			m_FrameBufferLighting.Release();
			device->CreateFramebuffer(fbDesc, &m_FrameBufferLighting);
			if (!m_FrameBufferLighting)
			{
				ASSERT(false, "createDeferredRenderPasses(): CreateFramebuffer(FB_Lighting) failed.");
				return false;
			}
		}

		// ----------------------------
		// Post render pass (FB is rebuilt per-frame using current backbuffer RTV)
		// ----------------------------
		if (!m_RenderPassPost)
		{
			RenderPassAttachmentDesc attachments[1] = {};
			attachments[0].Format = scDesc.ColorBufferFormat;
			attachments[0].SampleCount = 1;
			attachments[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			attachments[0].StoreOp = ATTACHMENT_STORE_OP_STORE;

			attachments[0].InitialState = RESOURCE_STATE_RENDER_TARGET;
			attachments[0].FinalState = RESOURCE_STATE_RENDER_TARGET;

			AttachmentReference colorRef = {};
			colorRef.AttachmentIndex = 0;
			colorRef.State = RESOURCE_STATE_RENDER_TARGET;

			SubpassDesc subpass = {};
			subpass.RenderTargetAttachmentCount = 1;
			subpass.pRenderTargetAttachments = &colorRef;

			RenderPassDesc rpDesc = {};
			rpDesc.Name = "RP_Post";
			rpDesc.AttachmentCount = 1;
			rpDesc.pAttachments = attachments;
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			m_RenderPassPost.Release();
			device->CreateRenderPass(rpDesc, &m_RenderPassPost);
			if (!m_RenderPassPost)
			{
				ASSERT(false, "createDeferredRenderPasses(): CreateRenderPass(RP_Post) failed.");
				return false;
			}
		}

		return true;
	}

	// ============================================================
	// PSO
	// ============================================================

	bool Renderer::createShadowPso()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "createShadowPso(): device is null.");

		if (m_ShadowPSO)
		{
			return true;
		}

		GraphicsPipelineStateCreateInfo psoCi = {};
		psoCi.PSODesc.Name = "Shadow Depth PSO";
		psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;

		gp.pRenderPass = m_RenderPassShadow;
		gp.SubpassIndex = 0;

		// Render targets are defined by the render pass.
		gp.NumRenderTargets = 0;
		gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
		gp.DSVFormat = TEX_FORMAT_UNKNOWN;

		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
		gp.RasterizerDesc.FrontCounterClockwise = true;

		gp.DepthStencilDesc.DepthEnable = true;
		gp.DepthStencilDesc.DepthWriteEnable = true;
		gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

		LayoutElement layoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // ATTRIB0 Position
		};

		// NOTE: Your vertex format is interleaved (pos/uv/normal/tangent...), so explicit stride is required.
		layoutElems[0].Stride = sizeof(float) * 11;

		gp.InputLayout.LayoutElements = layoutElems;
		gp.InputLayout.NumElements = _countof(layoutElems);

		ShaderCreateInfo sci = {};
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		sci.EntryPoint = "main";
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR | SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION;

		RefCntAutoPtr<IShader> vs;
		{
			sci.Desc = {};
			sci.Desc.Name = "Shadow VS";
			sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
			sci.FilePath = "Shadow.vsh";
			sci.Desc.UseCombinedTextureSamplers = false;

			device->CreateShader(sci, &vs);
			if (!vs)
			{
				ASSERT(false, "Failed to create Shadow VS.");
				return false;
			}
		}

		RefCntAutoPtr<IShader> ps;
		{
			sci.Desc = {};
			sci.Desc.Name = "Shadow PS";
			sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
			sci.FilePath = "Shadow.psh";
			sci.Desc.UseCombinedTextureSamplers = false;

			device->CreateShader(sci, &ps);
			if (!ps)
			{
				ASSERT(false, "Failed to create Shadow PS.");
				return false;
			}
		}

		psoCi.pVS = vs;
		psoCi.pPS = ps;

		psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc vars[] =
		{
			{ SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
			{ SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_STATIC  },
		};
		psoCi.PSODesc.ResourceLayout.Variables = vars;
		psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

		device->CreateGraphicsPipelineState(psoCi, &m_ShadowPSO);
		if (!m_ShadowPSO)
		{
			ASSERT(false, "Failed to create Shadow PSO.");
			return false;
		}

		// Bind SHADOW_CONSTANTS as static.
		{
			if (auto* var = m_ShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS"))
			{
				var->Set(m_pShadowCB);
			}
		}

		m_ShadowPSO->CreateShaderResourceBinding(&m_ShadowSRB, true);
		if (!m_ShadowSRB)
		{
			ASSERT(false, "Failed to create SRB_Shadow.");
			return false;
		}

		// OBJECT_CONSTANTS is dynamic, so it is bound on the SRB.
		{
			if (auto* var = m_ShadowSRB->GetVariableByName(SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS"))
			{
				var->Set(m_pObjectCB);
			}
		}

		return true;
	}

	bool Renderer::createGBufferPso()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "Device is null.");

		if (m_GBufferPSO)
		{
			return true;
		}

		GraphicsPipelineStateCreateInfo psoCi = {};
		psoCi.PSODesc.Name = "GBuffer PSO";
		psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;

		gp.pRenderPass = m_RenderPassGBuffer;
		gp.SubpassIndex = 0;

		// Render targets are defined by the render pass.
		gp.NumRenderTargets = 0;
		gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
		gp.RTVFormats[1] = TEX_FORMAT_UNKNOWN;
		gp.RTVFormats[2] = TEX_FORMAT_UNKNOWN;
		gp.RTVFormats[3] = TEX_FORMAT_UNKNOWN;
		gp.DSVFormat = TEX_FORMAT_UNKNOWN;

		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
		gp.RasterizerDesc.FrontCounterClockwise = true;

		gp.DepthStencilDesc.DepthEnable = true;
		gp.DepthStencilDesc.DepthWriteEnable = true;
		gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

		LayoutElement layoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // ATTRIB0 Position
			LayoutElement{1, 0, 2, VT_FLOAT32, false}, // ATTRIB1 UV
			LayoutElement{2, 0, 3, VT_FLOAT32, false}, // ATTRIB2 Normal
			LayoutElement{3, 0, 3, VT_FLOAT32, false}, // ATTRIB3 Tangent
		};
		gp.InputLayout.LayoutElements = layoutElems;
		gp.InputLayout.NumElements = _countof(layoutElems);

		ShaderCreateInfo sci = {};
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		sci.EntryPoint = "main";
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR | SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION;

		RefCntAutoPtr<IShader> vs;
		{
			sci.Desc = {};
			sci.Desc.Name = "GBuffer VS";
			sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
			sci.FilePath = "GBuffer.vsh";
			sci.Desc.UseCombinedTextureSamplers = false;

			device->CreateShader(sci, &vs);
			if (!vs)
			{
				ASSERT(false, "Failed to create GBuffer VS.");
				return false;
			}
		}

		RefCntAutoPtr<IShader> ps;
		{
			sci.Desc = {};
			sci.Desc.Name = "GBuffer PS";
			sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
			sci.FilePath = "GBuffer.psh";
			sci.Desc.UseCombinedTextureSamplers = false;

			device->CreateShader(sci, &ps);
			if (!ps)
			{
				ASSERT(false, "Failed to create GBuffer PS.");
				return false;
			}
		}

		psoCi.pVS = vs;
		psoCi.pPS = ps;

		psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc vars[] =
		{
			{ SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS",   SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
			{ SHADER_TYPE_PIXEL,  "MATERIAL_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },

			{ SHADER_TYPE_PIXEL,  "g_BaseColorTex",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_NormalTex",            SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_MetallicRoughnessTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_AOTex",                SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_EmissiveTex",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		psoCi.PSODesc.ResourceLayout.Variables = vars;
		psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

		SamplerDesc linearWrap =
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
		};

		ImmutableSamplerDesc samplers[] =
		{
			{ SHADER_TYPE_PIXEL, "g_LinearWrapSampler", linearWrap }
		};
		psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

		device->CreateGraphicsPipelineState(psoCi, &m_GBufferPSO);
		if (!m_GBufferPSO)
		{
			ASSERT(false, "Failed to create GBuffer PSO.");
			return false;
		}

		// Bind FRAME_CONSTANTS as static.
		{
			if (auto* var = m_GBufferPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FRAME_CONSTANTS"))
			{
				var->Set(m_pFrameCB);
			}
			if (auto* var = m_GBufferPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
			{
				var->Set(m_pFrameCB);
			}
		}

		return true;
	}


	bool Renderer::createLightingPso()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "createLightingPso(): device is null.");

		if (m_LightingPSO && m_LightingSRB)
		{
			return true;
		}

		GraphicsPipelineStateCreateInfo psoCi = {};
		psoCi.PSODesc.Name = "Deferred Lighting PSO";
		psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;

		gp.pRenderPass = m_RenderPassLighting;
		gp.SubpassIndex = 0;

		// Render targets are defined by the render pass.
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

		RefCntAutoPtr<IShader> vs;
		{
			sci.Desc = {};
			sci.Desc.Name = "DeferredLighting VS";
			sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
			sci.FilePath = "DeferredLighting.vsh";
			sci.Desc.UseCombinedTextureSamplers = false;

			device->CreateShader(sci, &vs);
			if (!vs)
			{
				ASSERT(false, "Failed to create DeferredLighting VS.");
				return false;
			}
		}

		RefCntAutoPtr<IShader> ps;
		{
			sci.Desc = {};
			sci.Desc.Name = "DeferredLighting PS";
			sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
			sci.FilePath = "DeferredLighting.psh";
			sci.Desc.UseCombinedTextureSamplers = false;

			device->CreateShader(sci, &ps);
			if (!ps)
			{
				ASSERT(false, "Failed to create DeferredLighting PS.");
				return false;
			}
		}

		psoCi.pVS = vs;
		psoCi.pPS = ps;

		psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc vars[] =
		{
			{ SHADER_TYPE_PIXEL, "g_GBuffer0",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBuffer1",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBuffer2",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBuffer3",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_GBufferDepth", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_ShadowMap",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		psoCi.PSODesc.ResourceLayout.Variables = vars;
		psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

		SamplerDesc linearClamp =
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
		};

		SamplerDesc shadowClamp = {};
		shadowClamp.MinFilter = FILTER_TYPE_COMPARISON_LINEAR;
		shadowClamp.MagFilter = FILTER_TYPE_COMPARISON_LINEAR;
		shadowClamp.MipFilter = FILTER_TYPE_COMPARISON_LINEAR;
		shadowClamp.AddressU = TEXTURE_ADDRESS_CLAMP;
		shadowClamp.AddressV = TEXTURE_ADDRESS_CLAMP;
		shadowClamp.AddressW = TEXTURE_ADDRESS_CLAMP;
		shadowClamp.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;

		ImmutableSamplerDesc samplers[] =
		{
			{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
			{ SHADER_TYPE_PIXEL, "g_ShadowCmpSampler",   shadowClamp },
		};
		psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

		device->CreateGraphicsPipelineState(psoCi, &m_LightingPSO);
		if (!m_LightingPSO)
		{
			ASSERT(false, "Failed to create Lighting PSO.");
			return false;
		}

		// Bind FRAME_CONSTANTS as static.
		{
			if (auto* var = m_LightingPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
			{
				var->Set(m_pFrameCB);
			}
		}

		m_LightingPSO->CreateShaderResourceBinding(&m_LightingSRB, true);
		if (!m_LightingSRB)
		{
			ASSERT(false, "Failed to create SRB_Lighting.");
			return false;
		}

		// Bind SRVs (mutable).
		{
			if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer0"))
			{
				var->Set(m_GBufferSrv[0]);
			}
			if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer1"))
			{
				var->Set(m_GBufferSrv[1]);
			}
			if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer2"))
			{
				var->Set(m_GBufferSrv[2]);
			}
			if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBuffer3"))
			{
				var->Set(m_GBufferSrv[3]);
			}
			if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap"))
			{
				var->Set(m_ShadowMapSrv);
			}
			if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_GBufferDepth"))
			{
				var->Set(m_GBufferDepthSRV);
			}
		}

		return true;
	}

	bool Renderer::createPostPso()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ISwapChain* swapChain = m_CreateInfo.pSwapChain.RawPtr();
		ASSERT(device && swapChain, "createPostPso(): device/swapchain is null.");

		if (m_PostPSO && m_PostSRB)
		{
			return true;
		}

		GraphicsPipelineStateCreateInfo psoCi = {};
		psoCi.PSODesc.Name = "Post Copy PSO";
		psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;

		gp.pRenderPass = m_RenderPassPost;
		gp.SubpassIndex = 0;

		// Render targets are defined by the render pass.
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

		RefCntAutoPtr<IShader> vs;
		{
			sci.Desc = {};
			sci.Desc.Name = "PostCopy VS";
			sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
			sci.FilePath = "PostCopy.vsh";
			sci.Desc.UseCombinedTextureSamplers = false;

			device->CreateShader(sci, &vs);
			if (!vs)
			{
				ASSERT(false, "Failed to create PostCopy VS.");
				return false;
			}
		}

		RefCntAutoPtr<IShader> ps;
		{
			sci.Desc = {};
			sci.Desc.Name = "PostCopy PS";
			sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
			sci.FilePath = "PostCopy.psh";
			sci.Desc.UseCombinedTextureSamplers = false;

			device->CreateShader(sci, &ps);
			if (!ps)
			{
				ASSERT(false, "Failed to create PostCopy PS.");
				return false;
			}
		}

		psoCi.pVS = vs;
		psoCi.pPS = ps;

		psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc vars[] =
		{
			{ SHADER_TYPE_PIXEL, "g_InputColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		psoCi.PSODesc.ResourceLayout.Variables = vars;
		psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

		SamplerDesc linearClamp =
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
		};

		ImmutableSamplerDesc samplers[] =
		{
			{ SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
		};
		psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

		device->CreateGraphicsPipelineState(psoCi, &m_PostPSO);
		if (!m_PostPSO)
		{
			ASSERT(false, "Failed to create Post PSO.");
			return false;
		}

		m_PostPSO->CreateShaderResourceBinding(&m_PostSRB, true);
		if (!m_PostSRB)
		{
			ASSERT(false, "Failed to create SRB_Post.");
			return false;
		}

		return true;
	}

	bool Renderer::recreateShadowResources()
	{
		if (!createShadowTargets())
			return false;

		if (!createShadowRenderPasses())
			return false;

		if (!createShadowPso())
			return false;

		return true;
	}

	bool Renderer::recreateSizeDependentResources()
	{
		if (!m_CreateInfo.pDevice || !m_CreateInfo.pImmediateContext || !m_CreateInfo.pSwapChain)
			return false;

		if (!createDeferredTargets())
			return false;

		if (!createDeferredRenderPasses())
			return false;

		if (!createGBufferPso())
			return false;

		if (!createLightingPso())
			return false;

		if (!createPostPso())
			return false;

		return true;
	}

	void Renderer::setViewportFromView(const View& view)
	{
		Viewport vp = {};
		vp.TopLeftX = float(view.Viewport.left);
		vp.TopLeftY = float(view.Viewport.top);
		vp.Width = float(view.Viewport.right - view.Viewport.left);
		vp.Height = float(view.Viewport.bottom - view.Viewport.top);
		vp.MinDepth = 0.f;
		vp.MaxDepth = 1.f;

		m_CreateInfo.pImmediateContext->SetViewports(1, &vp, 0, 0);
	}

} // namespace shz
