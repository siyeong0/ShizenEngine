#include "pch.h"
#include "Engine/Renderer/Public/Renderer.h"

#include <unordered_map>

#include "Engine/AssetRuntime/AssetManager/Public/AssetManager.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

#include "Tools/Image/Public/TextureUtilities.h"

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
		m_pCache->Initialize(m_CreateInfo.pDevice.RawPtr(), m_pAssetManager);
		m_pCache->SetErrorTexture("C:/Dev/ShizenEngine/Assets/Error.jpg");

		const SwapChainDesc& scDesc = m_CreateInfo.pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : scDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : scDesc.Height;

		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::FrameConstants), "Frame constants", &m_pFrameCB);
		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ShadowConstants), "Shadow constants", &m_pShadowCB);

		// ObjectIndex instance VB (1x uint, updated per draw)
		if (!ensureObjectIndexInstanceBuffer())
			return false;

		// Object table (StructuredBuffer<ObjectConstants>)
		m_ObjectTableCapacity = 0;
		if (!ensureObjectTableCapacity(256))
			return false;

		m_pMaterialStaticBinder = std::make_unique<RendererMaterialStaticBinder>();
		m_pMaterialStaticBinder->SetFrameConstants(m_pFrameCB);
		m_pMaterialStaticBinder->SetObjectTableSRV(m_pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));

		TextureLoadInfo tli = {};
		CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleEnvHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvTex);
		CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleDiffuseHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvDiffuseTex);
		CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleSpecularHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvSpecularTex);
		CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleBrdf.dds", tli, m_CreateInfo.pDevice, &m_EnvBrdfTex);

		m_ShadowDirty = true;
		m_DeferredDirty = true;

		m_FrameBufferPostCurrent.Release();

		if (!recreateShadowResources())
			return false;

		if (!recreateSizeDependentResources())
			return false;

		m_PreBarriers.reserve(512);
		m_FrameMat.reserve(512);
		m_FrameMatKeys.reserve(512);

		return true;
	}

	void Renderer::Cleanup()
	{
		ReleaseSwapChainBuffers();

		m_ShadowPSO.Release();
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

		m_pObjectIndexVB.Release();
		m_pObjectTableSB.Release();
		m_ObjectTableCapacity = 0;

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


	void Renderer::BeginFrame()
	{
		// Ensure we don't keep swapchain backbuffer refs across frames.
		// This makes fullscreen toggle much more robust even if the app toggles right after Present.
		m_FrameBufferPostCurrent.Release();

		// Build swapchain-backed framebuffer for the current backbuffer in BeginFrame (NOT in Render).
		// If this fails, Render() should early-out via ASSERT checks or nullptr checks.
		buildPostFramebufferForCurrentBackBuffer();
	}

	void Renderer::Render(RenderScene& scene, const ViewFamily& viewFamily)
	{
		IDeviceContext* ctx = m_CreateInfo.pImmediateContext.RawPtr();
		ISwapChain* sc = m_CreateInfo.pSwapChain.RawPtr();
		IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();

		if (!ctx || !sc || !dev || !m_pCache)
			return;

		if (viewFamily.Views.empty())
			return;

		if (!createShadowTargets())        return;
		if (!createShadowRenderPasses())   return;
		if (!createDeferredTargets())      return;
		if (!createDeferredRenderPasses()) return;

		if (!createShadowPso())            return;
		if (!createLightingPso())          return;
		if (!createPostPso())              return;

		const View& view = viewFamily.Views[0];

		// ------------------------------------------------------------
		// Update frame/shadow constants (unchanged)
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

			// -----------------------------
			// Shadow (simple fixed ortho)
			// -----------------------------

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
			cb->LightViewProj = lightViewProj;
		}

		{
			MapHelper<hlsl::ShadowConstants> cb(ctx, m_pShadowCB, MAP_WRITE, MAP_FLAG_DISCARD);
			cb->LightViewProj = lightViewProj;
		}

		// ------------------------------------------------------------
		// Upload ObjectTable once per frame
		// ------------------------------------------------------------
		{
			const uint32 objectCount = static_cast<uint32>(scene.GetObjects().size());
			if (!ensureObjectTableCapacity(objectCount))
				return;

			uploadObjectTable(ctx, scene);
		}

		auto drawFullScreenTriangle = [&]()
		{
			DrawAttribs da = {};
			da.NumVertices = 3;
			da.Flags = DRAW_FLAG_VERIFY_ALL;
			ctx->Draw(da);
		};

		// ------------------------------------------------------------
		// Pre-Transition
		// ------------------------------------------------------------
		m_PreBarriers.clear();
		m_FrameMat.clear();
		m_FrameMatKeys.clear();

		auto pushBarrier = [&](IDeviceObject* pObj, RESOURCE_STATE from, RESOURCE_STATE to)
		{
			if (!pObj) return;
			StateTransitionDesc b = {};
			b.pResource = pObj;
			b.OldState = from;
			b.NewState = to;
			b.Flags = STATE_TRANSITION_FLAG_UPDATE_STATE;
			m_PreBarriers.push_back(b);
		};

		pushBarrier(m_pFrameCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		pushBarrier(m_pShadowCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

		// Object indirection resources
		pushBarrier(m_pObjectTableSB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(m_pObjectIndexVB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);

		pushBarrier(m_EnvTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(m_EnvDiffuseTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(m_EnvSpecularTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(m_EnvBrdfTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		pushBarrier(m_pCache->GetErrorTexture().GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		for (RenderScene::RenderObject& obj : scene.GetObjects())
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

				MaterialInstance* inst = &obj.Materials[sec.MaterialSlot];
				if (!inst)
					continue;

				const MaterialTemplate* tmpl = inst->GetTemplate();
				if (!tmpl)
					continue;

				const uint64 key = reinterpret_cast<uint64>(inst);

				auto it = m_FrameMat.find(key);
				if (it == m_FrameMat.end() || inst->IsPsoDirty())
				{
					Handle<MaterialRenderData> hRD = m_pCache->GetOrCreateMaterialRenderData(inst, ctx, m_pMaterialStaticBinder.get());
					it = m_FrameMat.emplace(key, hRD).first;
					m_FrameMatKeys.push_back(key);
				}

				MaterialRenderData* rd = m_pCache->TryGetMaterialRenderData(it->second);
				if (!rd)
					continue;

				if (auto* matCB = rd->GetMaterialConstantsBuffer())
					pushBarrier(matCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

				for (auto texHandle : rd->GetBoundTextures())
				{
					auto texRD = m_pCache->TryGetTextureRenderData(texHandle);
					pushBarrier(texRD->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
				}
			}
		}

		for (uint64 key : m_FrameMatKeys)
		{
			auto it = m_FrameMat.find(key);
			if (it == m_FrameMat.end())
				continue;

			MaterialRenderData* rd = m_pCache->TryGetMaterialRenderData(it->second);
			if (!rd)
				continue;

			MaterialInstance* inst = reinterpret_cast<MaterialInstance*>(key);
			if (!inst)
				continue;

			rd->Apply(m_pCache.get(), *inst, ctx);

			for (const auto& hTexRD : rd->GetBoundTextures())
			{
				if (auto* texRD = m_pCache->TryGetTextureRenderData(hTexRD))
					pushBarrier(texRD->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
			}
		}

		if (!m_PreBarriers.empty())
			ctx->TransitionResourceStates(static_cast<uint32>(m_PreBarriers.size()), m_PreBarriers.data());

		// ------------------------------------------------------------
		// PASS 0: Shadow
		// ------------------------------------------------------------
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

			const auto& objs = scene.GetObjects();
			for (uint32 objIndex = 0; objIndex < static_cast<uint32>(objs.size()); ++objIndex)
			{
				const auto& obj = objs[objIndex];

				const StaticMeshRenderData* mesh = m_pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				if (!mesh || !mesh->IsValid())
					continue;

				// Upload per-draw instance ObjectIndex (ATTRIB4)
				uploadObjectIndexInstance(ctx, objIndex);

				IBuffer* vbs[] = { mesh->GetVertexBuffer(), m_pObjectIndexVB.RawPtr() };
				uint64 offs[] = { 0, 0 };
				ctx->SetVertexBuffers(0, 2, vbs, offs,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET);

				ctx->SetIndexBuffer(mesh->GetIndexBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

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
					dia.NumInstances = 1;

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

		// ------------------------------------------------------------
		// PASS 1: GBuffer (material batching)
		// ------------------------------------------------------------
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

			const MaterialRenderData* currMat = nullptr;

			const auto& objs = scene.GetObjects();
			for (uint32 objIndex = 0; objIndex < static_cast<uint32>(objs.size()); ++objIndex)
			{
				const auto& obj = objs[objIndex];

				const StaticMeshRenderData* mesh = m_pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				if (!mesh || !mesh->IsValid())
					continue;

				// Upload per-draw instance ObjectIndex (ATTRIB4)
				uploadObjectIndexInstance(ctx, objIndex);

				IBuffer* vbs[] = { mesh->GetVertexBuffer(), m_pObjectIndexVB.RawPtr() };
				uint64 offs[] = { 0, 0 };
				ctx->SetVertexBuffers(0, 2, vbs, offs,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET);

				ctx->SetIndexBuffer(mesh->GetIndexBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				const VALUE_TYPE indexType = mesh->GetIndexType();

				for (const auto& sec : mesh->GetSections())
				{
					if (sec.IndexCount == 0)
						continue;

					const MaterialInstance* inst = &obj.Materials[sec.MaterialSlot];
					if (!inst)
						continue;

					const uint64 key = reinterpret_cast<uint64>(inst);
					auto it = m_FrameMat.find(key);
					if (it == m_FrameMat.end())
						continue;

					MaterialRenderData* rd = m_pCache->TryGetMaterialRenderData(it->second);
					if (!rd || !rd->GetPSO() || !rd->GetSRB())
						continue;

					if (currMat != rd)
					{
						currMat = rd;

						ctx->SetPipelineState(rd->GetPSO());
						ctx->CommitShaderResources(rd->GetSRB(), RESOURCE_STATE_TRANSITION_MODE_VERIFY);
					}

					DrawIndexedAttribs dia = {};
					dia.NumIndices = sec.IndexCount;
					dia.IndexType = indexType;
					dia.Flags = DRAW_FLAG_VERIFY_ALL;
					dia.FirstIndexLocation = sec.FirstIndex;
					dia.BaseVertex = static_cast<int32>(sec.BaseVertex);
					dia.NumInstances = 1;

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

		// ------------------------------------------------------------
		// PASS 2: Lighting
		// ------------------------------------------------------------
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

		// ------------------------------------------------------------
		// PASS 3: Post
		// ------------------------------------------------------------
		{
			{
				const SwapChainDesc& scDesc = sc->GetDesc();

				Viewport bbVp = {};
				bbVp.TopLeftX = 0;
				bbVp.TopLeftY = 0;
				bbVp.Width = float(scDesc.Width);
				bbVp.Height = float(scDesc.Height);
				bbVp.MinDepth = 0.f;
				bbVp.MaxDepth = 1.f;
				ctx->SetViewports(1, &bbVp, 0, 0);
			}

			{
				ASSERT(m_PostSRB, "Post SRB is null.");
				ASSERT(m_LightingSRV, "Lighting SRV is null (post input).");

				if (auto* v = m_PostSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_InputColor"))
					v->Set(m_LightingSRV, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}

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

	void Renderer::EndFrame()
	{
		// Release swapchain-backed framebuffer at end of the frame
		// so that DXGI can freely resize/toggle fullscreen next frame. :contentReference[oaicite:2]{index=2}
		m_FrameBufferPostCurrent.Release();
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

		// --- Recreate GBuffers (size dependent) ---
		createRtTexture2d(w, h, TEX_FORMAT_RGBA8_UNORM, "GBuffer0_AlbedoA", m_GBufferTex[0], m_GBufferRtv[0], m_GBufferSrv[0]);
		createRtTexture2d(w, h, TEX_FORMAT_RGBA16_FLOAT, "GBuffer1_NormalWS", m_GBufferTex[1], m_GBufferRtv[1], m_GBufferSrv[1]);
		createRtTexture2d(w, h, TEX_FORMAT_RGBA8_UNORM, "GBuffer2_MRAO", m_GBufferTex[2], m_GBufferRtv[2], m_GBufferSrv[2]);
		createRtTexture2d(w, h, TEX_FORMAT_RGBA16_FLOAT, "GBuffer3_Emissive", m_GBufferTex[3], m_GBufferRtv[3], m_GBufferSrv[3]);

		// --- Recreate lighting buffer (size dependent) ---
		createRtTexture2d(w, h, sc.ColorBufferFormat, "LightingColor", m_LightingTex, m_LightingRTV, m_LightingSRV);

		// --- Recreate depth (size dependent) ---
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

			TextureViewDesc vd = {};
			vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			vd.Format = TEX_FORMAT_D32_FLOAT;
			m_GBufferDepthTex->CreateView(vd, &m_GBufferDepthDSV);
			ASSERT(m_GBufferDepthDSV, "Failed to create GBufferDepth DSV.");

			vd = {};
			vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
			vd.Format = TEX_FORMAT_R32_FLOAT;
			m_GBufferDepthTex->CreateView(vd, &m_GBufferDepthSRV);
			ASSERT(m_GBufferDepthSRV, "Failed to create GBufferDepth SRV.");
		}

		m_FrameBufferGBuffer.Release();
		m_FrameBufferLighting.Release();
		m_FrameBufferPostCurrent.Release(); // swapchain-backed is per-frame anyway

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
		// GBuffer RenderPass (once)
		// ----------------------------
		if (!m_RenderPassGBuffer)
		{
			RenderPassAttachmentDesc attachments[5] = {};

			// 4 color + 1 depth (formats are fixed by your choices)
			attachments[0].Format = TEX_FORMAT_RGBA8_UNORM;
			attachments[1].Format = TEX_FORMAT_RGBA16_FLOAT;
			attachments[2].Format = TEX_FORMAT_RGBA8_UNORM;
			attachments[3].Format = TEX_FORMAT_RGBA16_FLOAT;

			for (uint32 i = 0; i < 4; ++i)
			{
				attachments[i].SampleCount = 1;
				attachments[i].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
				attachments[i].StoreOp = ATTACHMENT_STORE_OP_STORE;
				attachments[i].InitialState = RESOURCE_STATE_RENDER_TARGET;
				attachments[i].FinalState = RESOURCE_STATE_RENDER_TARGET;
			}

			attachments[4].Format = TEX_FORMAT_D32_FLOAT;
			attachments[4].SampleCount = 1;
			attachments[4].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			attachments[4].StoreOp = ATTACHMENT_STORE_OP_STORE;
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

			device->CreateRenderPass(rpDesc, &m_RenderPassGBuffer);
			ASSERT(m_RenderPassGBuffer, "CreateRenderPass(RP_GBuffer) failed.");
		}

		// ----------------------------
		// Lighting RenderPass (once)
		// ----------------------------
		if (!m_RenderPassLighting)
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
			rpDesc.Name = "RP_Lighting";
			rpDesc.AttachmentCount = 1;
			rpDesc.pAttachments = attachments;
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			device->CreateRenderPass(rpDesc, &m_RenderPassLighting);
			ASSERT(m_RenderPassLighting, "CreateRenderPass(RP_Lighting) failed.");
		}

		// ----------------------------
		// Post RenderPass (once)
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

			device->CreateRenderPass(rpDesc, &m_RenderPassPost);
			ASSERT(m_RenderPassPost, "CreateRenderPass(RP_Post) failed.");
		}

		return true;
	}

	bool Renderer::recreateDeferredFramebuffers()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "recreateDeferredFramebuffers(): device is null.");

		// GBuffer FB
		{
			ASSERT(m_RenderPassGBuffer, "RP_GBuffer is null.");
			ASSERT(m_GBufferRtv[0] && m_GBufferRtv[1] && m_GBufferRtv[2] && m_GBufferRtv[3] && m_GBufferDepthDSV, "GBuffer views are null.");

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
			ASSERT(m_FrameBufferGBuffer, "CreateFramebuffer(FB_GBuffer) failed.");
		}

		// Lighting FB
		{
			ASSERT(m_RenderPassLighting, "RP_Lighting is null.");
			ASSERT(m_LightingRTV, "Lighting RTV is null.");

			ITextureView* atch[1] = { m_LightingRTV };

			FramebufferDesc fbDesc = {};
			fbDesc.Name = "FB_Lighting";
			fbDesc.pRenderPass = m_RenderPassLighting;
			fbDesc.AttachmentCount = 1;
			fbDesc.ppAttachments = atch;

			m_FrameBufferLighting.Release();
			device->CreateFramebuffer(fbDesc, &m_FrameBufferLighting);
			ASSERT(m_FrameBufferLighting, "CreateFramebuffer(FB_Lighting) failed.");
		}

		return true;
	}



	// ============================================================
	// PSO
	// ============================================================

	bool Renderer::createShadowPso()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "Device is null.");

		if (m_ShadowPSO && m_ShadowSRB)
		{
			return true;
		}

		GraphicsPipelineStateCreateInfo psoCi = {};
		psoCi.PSODesc.Name = "Shadow PSO";
		psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;

		gp.pRenderPass = m_RenderPassShadow;
		gp.SubpassIndex = 0;

		gp.NumRenderTargets = 0;
		gp.DSVFormat = TEX_FORMAT_UNKNOWN;

		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
		gp.RasterizerDesc.FrontCounterClockwise = true;

		gp.DepthStencilDesc.DepthEnable = true;
		gp.DepthStencilDesc.DepthWriteEnable = true;
		gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

		LayoutElement layoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // ATTRIB0 Position (vertex stream)
			LayoutElement{4, 1, 1, VT_UINT32,  false, LAYOUT_ELEMENT_AUTO_OFFSET, sizeof(uint32), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1}, // ATTRIB4 ObjectIndex (instance stream)
		};

		// NOTE:
		// Your mesh VB is interleaved with stride 11 floats.
		// Keep the same stride on the vertex stream element as your original code.
		layoutElems[0].Stride = sizeof(float) * 11;
		layoutElems[1].Stride = sizeof(uint32);

		gp.InputLayout.LayoutElements = layoutElems;
		gp.InputLayout.NumElements = _countof(layoutElems);

		ShaderCreateInfo sci = {};
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		sci.EntryPoint = "main";
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

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

		// Only dynamic/mutable need explicit descs; here none.
		psoCi.PSODesc.ResourceLayout.Variables = nullptr;
		psoCi.PSODesc.ResourceLayout.NumVariables = 0;

		device->CreateGraphicsPipelineState(psoCi, &m_ShadowPSO);
		if (!m_ShadowPSO)
		{
			ASSERT(false, "Failed to create Shadow PSO.");
			return false;
		}

		// Bind statics
		{
			if (auto* var = m_ShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SHADOW_CONSTANTS"))
				var->Set(m_pShadowCB);

			// OBJECT_INDEX cbuffer is removed.

			if (auto* var = m_ShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_ObjectTable"))
				var->Set(m_pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
		}

		m_ShadowPSO->CreateShaderResourceBinding(&m_ShadowSRB, true);
		if (!m_ShadowSRB)
		{
			ASSERT(false, "Failed to create SRB_Shadow.");
			return false;
		}

		return true;
	}

	//bool Renderer::createGBufferPso()
	//{
	//	IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
	//	ASSERT(device, "Device is null.");

	//	if (m_GBufferPSO)
	//	{
	//		return true;
	//	}

	//	GraphicsPipelineStateCreateInfo psoCi = {};
	//	psoCi.PSODesc.Name = "GBuffer PSO";
	//	psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

	//	GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;

	//	gp.pRenderPass = m_RenderPassGBuffer;
	//	gp.SubpassIndex = 0;

	//	gp.NumRenderTargets = 0;
	//	gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
	//	gp.RTVFormats[1] = TEX_FORMAT_UNKNOWN;
	//	gp.RTVFormats[2] = TEX_FORMAT_UNKNOWN;
	//	gp.RTVFormats[3] = TEX_FORMAT_UNKNOWN;
	//	gp.DSVFormat = TEX_FORMAT_UNKNOWN;

	//	gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	//	gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
	//	gp.RasterizerDesc.FrontCounterClockwise = true;

	//	gp.DepthStencilDesc.DepthEnable = true;
	//	gp.DepthStencilDesc.DepthWriteEnable = true;
	//	gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

	//	LayoutElement layoutElems[] =
	//	{
	//		LayoutElement{0, 0, 3, VT_FLOAT32, false}, // ATTRIB0 Position
	//		LayoutElement{1, 0, 2, VT_FLOAT32, false}, // ATTRIB1 UV
	//		LayoutElement{2, 0, 3, VT_FLOAT32, false}, // ATTRIB2 Normal
	//		LayoutElement{3, 0, 3, VT_FLOAT32, false}, // ATTRIB3 Tangent

	//		LayoutElement{4, 1, 1, VT_UINT32,  false, LAYOUT_ELEMENT_AUTO_OFFSET, sizeof(uint32), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1}, // ATTRIB4 ObjectIndex (instance stream)
	//	};
	//	layoutElems[4].Stride = sizeof(uint32);

	//	gp.InputLayout.LayoutElements = layoutElems;
	//	gp.InputLayout.NumElements = _countof(layoutElems);

	//	ShaderCreateInfo sci = {};
	//	sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
	//	sci.pShaderSourceStreamFactory = m_pShaderSourceFactory;
	//	sci.EntryPoint = "main";
	//	sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

	//	RefCntAutoPtr<IShader> vs;
	//	{
	//		sci.Desc = {};
	//		sci.Desc.Name = "GBuffer VS";
	//		sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
	//		sci.FilePath = "GBuffer.vsh";
	//		sci.Desc.UseCombinedTextureSamplers = false;

	//		device->CreateShader(sci, &vs);
	//		if (!vs)
	//		{
	//			ASSERT(false, "Failed to create GBuffer VS.");
	//			return false;
	//		}
	//	}

	//	RefCntAutoPtr<IShader> ps;
	//	{
	//		sci.Desc = {};
	//		sci.Desc.Name = "GBuffer PS";
	//		sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
	//		sci.FilePath = "GBuffer.psh";
	//		sci.Desc.UseCombinedTextureSamplers = false;

	//		device->CreateShader(sci, &ps);
	//		if (!ps)
	//		{
	//			ASSERT(false, "Failed to create GBuffer PS.");
	//			return false;
	//		}
	//	}

	//	psoCi.pVS = vs;
	//	psoCi.pPS = ps;

	//	psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

	//	ShaderResourceVariableDesc vars[] =
	//	{
	//		{ SHADER_TYPE_PIXEL,  "MATERIAL_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },

	//		{ SHADER_TYPE_PIXEL,  "g_BaseColorTex",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	//		{ SHADER_TYPE_PIXEL,  "g_NormalTex",            SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	//		{ SHADER_TYPE_PIXEL,  "g_MetallicRoughnessTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	//		{ SHADER_TYPE_PIXEL,  "g_AOTex",                SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	//		{ SHADER_TYPE_PIXEL,  "g_EmissiveTex",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	//	};
	//	psoCi.PSODesc.ResourceLayout.Variables = vars;
	//	psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

	//	SamplerDesc linearWrap =
	//	{
	//		FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
	//		TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
	//	};

	//	ImmutableSamplerDesc samplers[] =
	//	{
	//		{ SHADER_TYPE_PIXEL, "g_LinearWrapSampler", linearWrap }
	//	};
	//	psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
	//	psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

	//	device->CreateGraphicsPipelineState(psoCi, &m_GBufferPSO);
	//	if (!m_GBufferPSO)
	//	{
	//		ASSERT(false, "Failed to create GBuffer PSO.");
	//		return false;
	//	}

	//	// Bind statics (once)
	//	{
	//		if (auto* var = m_GBufferPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FRAME_CONSTANTS"))
	//			var->Set(m_pFrameCB);
	//		if (auto* var = m_GBufferPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
	//			var->Set(m_pFrameCB);

	//		// OBJECT_INDEX cbuffer is removed.

	//		if (auto* var = m_GBufferPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "g_ObjectTable"))
	//			var->Set(m_pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
	//	}

	//	return true;
	//}


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
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

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
			{ SHADER_TYPE_PIXEL, "g_EnvMapTex",		SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_IrradianceIBLTex",	SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_SpecularIBLTex",	SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "g_BrdfIBLTex",		SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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


			if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EnvMapTex"))
			{
				var->Set(m_EnvTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			}
			if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceIBLTex"))
			{
				var->Set(m_EnvDiffuseTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			}
			if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SpecularIBLTex"))
			{
				var->Set(m_EnvSpecularTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			}
			if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_BrdfIBLTex"))
			{
				var->Set(m_EnvBrdfTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
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
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

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

	void Renderer::updateSizeDependentSRBs()
	{
		if (m_LightingSRB)
		{
			auto setTex = [&](const char* name, ITextureView* srv)
			{
				if (auto var = m_LightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, name))
				{
					var->Set(srv, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
			};

			setTex("g_GBuffer0", m_GBufferSrv[0]);
			setTex("g_GBuffer1", m_GBufferSrv[1]);
			setTex("g_GBuffer2", m_GBufferSrv[2]);
			setTex("g_GBuffer3", m_GBufferSrv[3]);
			setTex("g_GBufferDepth", m_GBufferDepthSRV);
			setTex("g_ShadowMap", m_ShadowMapSrv);
		}

		if (m_PostSRB)
		{
			if (auto* v = m_PostSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_InputColor"))
				v->Set(m_LightingSRV, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}
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

		if (!createDeferredRenderPasses())
			return false;

		if (!createDeferredTargets())
			return false;

		if (!recreateDeferredFramebuffers())
			return false;

		updateSizeDependentSRBs();

		if (m_PostSRB)
		{
			if (auto* v = m_PostSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_InputColor"))
				v->Set(m_LightingSRV, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}

		return true;
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

	bool Renderer::ensureObjectTableCapacity(uint32 objectCount)
	{
		IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();
		ASSERT(dev, "ensureObjectTableCapacity(): device is null.");

		if (objectCount == 0)
			objectCount = 1;

		if (m_pObjectTableSB && m_ObjectTableCapacity >= objectCount)
			return true;

		// Grow policy: round up
		uint32 newCap = (m_ObjectTableCapacity == 0) ? 256 : m_ObjectTableCapacity;
		while (newCap < objectCount)
			newCap *= 2;

		BufferDesc desc = {};
		desc.Name = "ObjectTableSB";
		desc.Usage = USAGE_DYNAMIC;
		desc.BindFlags = BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = CPU_ACCESS_WRITE;

		desc.Mode = BUFFER_MODE_STRUCTURED;
		desc.ElementByteStride = sizeof(hlsl::ObjectConstants);
		desc.Size = static_cast<uint64>(desc.ElementByteStride) * static_cast<uint64>(newCap);

		BufferData initData = {};
		RefCntAutoPtr<IBuffer> newBuf;
		dev->CreateBuffer(desc, &initData, &newBuf);

		if (!newBuf)
		{
			ASSERT(false, "Failed to create ObjectTableSB.");
			return false;
		}

		m_pObjectTableSB = newBuf;
		m_ObjectTableCapacity = newCap;

		BufferViewDesc viewDesc = {};
		viewDesc.ViewType = BUFFER_VIEW_SHADER_RESOURCE;
		viewDesc.ByteOffset = 0;
		viewDesc.ByteWidth = 0; // 0 = whole buffer

		return true;
	}

	void Renderer::uploadObjectTable(IDeviceContext* pCtx, const RenderScene& scene)
	{
		ASSERT(pCtx, "uploadObjectTable(): context is null.");
		ASSERT(m_pObjectTableSB, "uploadObjectTable(): object table buffer is null.");

		const auto& objs = scene.GetObjects();
		const uint32 count = static_cast<uint32>(objs.size());
		if (count == 0)
			return;

		MapHelper<hlsl::ObjectConstants> map(pCtx, m_pObjectTableSB, MAP_WRITE, MAP_FLAG_DISCARD);
		hlsl::ObjectConstants* dst = map;

		for (uint32 i = 0; i < count; ++i)
		{
			const auto& obj = objs[i];

			hlsl::ObjectConstants oc = {};
			oc.World = obj.Transform;
			oc.WorldInvTranspose = obj.Transform.Inversed().Transposed();

			dst[i] = oc;
		}
	}

	bool Renderer::ensureObjectIndexInstanceBuffer()
	{
		IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();
		ASSERT(dev, "ensureObjectIndexInstanceBuffer(): device is null.");

		if (m_pObjectIndexVB)
			return true;

		BufferDesc desc = {};
		desc.Name = "ObjectIndexInstanceVB";
		desc.Usage = USAGE_DYNAMIC;
		desc.BindFlags = BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = CPU_ACCESS_WRITE;

		// One uint32 per draw (we use NumInstances = 1 for now).
		desc.Size = sizeof(uint32);

		m_pObjectIndexVB.Release();

		// IMPORTANT:
		// Dynamic buffers must be created with null initial data.
		dev->CreateBuffer(desc, nullptr, &m_pObjectIndexVB);

		if (!m_pObjectIndexVB)
		{
			ASSERT(false, "Failed to create ObjectIndexInstanceVB.");
			return false;
		}

		return true;
	}


	void Renderer::uploadObjectIndexInstance(IDeviceContext* pCtx, uint32 objectIndex)
	{
		ASSERT(pCtx, "uploadObjectIndexInstance(): context is null.");
		ASSERT(m_pObjectIndexVB, "uploadObjectIndexInstance(): instance VB is null.");

		MapHelper<uint32> map(pCtx, m_pObjectIndexVB, MAP_WRITE, MAP_FLAG_DISCARD);
		*map = objectIndex;
	}


} // namespace shz
