// Renderer.cpp
#include "pch.h"
#include "Renderer.h"
#include "ViewFamily.h"

#include <unordered_map>

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

		// Shader source factory.
		m_CreateInfo.pEngineFactory->CreateDefaultShaderSourceStreamFactory(
			m_CreateInfo.ShaderRootDir,
			&m_pShaderSourceFactory
		);

		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::FrameConstants), "Frame constants CB", &m_pFrameCB);
		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ObjectConstants), "Object constants CB", &m_pObjectCB);

		if (!createShadowTargets()) { ASSERT(false, "Failed to create shadow targets."); return false; }
		if (!createDeferredTargets()) { ASSERT(false, "Failed to create deferred render targets."); return false; }
		if (!createDeferredRenderPasses()) { ASSERT(false, "Failed to create deferred render passes."); return false; }
		if (!createShadowRenderPasses()) { ASSERT(false, "Failed to create shadow render passes."); return false; }

		if (!createBasicPso())
		{
			ASSERT(false, "Failed to create Basic PSO.");
			return false;
		}

		if (!createGBufferPso())
		{
			ASSERT(false, "Failed to create G-Buffer PSO.");
			return false;
		}

		if (!createLightingPso())
		{
			ASSERT(false, "Failed to create Lighting PSO.");
			return false;
		}

		if (!createPostPso())
		{
			ASSERT(false, "Failed to create Post PSO.");
			return false;
		}

		if (!createShadowPso())
		{
			ASSERT(false, "Failed to create Shadow PSO.");
			return false;
		}

		// Create resource cache (owned).
		{
			ASSERT(!m_pRenderResourceCache, "Resource cache already created.");

			m_pRenderResourceCache = std::make_unique<RenderResourceCache>();

			RenderResourceCacheCreateInfo rci = {};
			rci.pDevice = m_CreateInfo.pDevice;
			rci.pAssetManager = m_pAssetManager;

			if (!m_pRenderResourceCache->Initialize(rci))
			{
				ASSERT(false, "Failed to initialize RenderResourceCache.");
				return false;
			}
		}

		return true;
	}

	void Renderer::Cleanup()
	{
		m_PsoBasic.Release();
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
		m_FrameBufferPost.Release();

		m_pShadowCB.Release();
		m_pFrameCB.Release();
		m_pObjectCB.Release();

		m_pShaderSourceFactory.Release();

		m_pAssetManager = nullptr;
		m_CreateInfo = {};
		m_Width = 0;
		m_Height = 0;
		m_DeferredWidth = 0;
		m_DeferredHeight = 0;
	}

	void Renderer::OnResize(uint32 width, uint32 height)
	{
		m_Width = width;
		m_Height = height;
	}

	void Renderer::BeginFrame()
	{
	}

	void Renderer::EndFrame()
	{
	}

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

	void Renderer::Render(const RenderScene& scene, const ViewFamily& viewFamily)
	{
		IDeviceContext* ctx = m_CreateInfo.pImmediateContext.RawPtr();
		ISwapChain* swapChain = m_CreateInfo.pSwapChain.RawPtr();
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();

		ASSERT(ctx && swapChain && device, "Render(): context/swap/device null.");
		ASSERT(m_pRenderResourceCache, "RenderResourceCache is null.");

		if (viewFamily.Views.empty())
		{
			return;
		}

		// Ensure render targets/render passes/PSOs are ready.
		// This is assumed to be the same policy as your previous answer (recreate-on-demand).
		if (!createShadowTargets())
		{
			return;
		}
		if (!createShadowRenderPasses())
		{
			return;
		}
		if (!createShadowPso())
		{
			return;
		}
		if (!createDeferredTargets())
		{
			return;
		}
		if (!createDeferredRenderPasses())
		{
			return;
		}
		if (!createLightingPso())
		{
			return;
		}
		if (!createPostPso())
		{
			return;
		}

		const View& view = viewFamily.Views[0];
		float3 cameraWs = view.CameraPosition;

		const RenderScene::LightObject* globalLight = nullptr;
		for (const RenderScene::LightObject& light : scene.GetLights())
		{
			globalLight = &light;
			break;
		}

		ASSERT(globalLight, "No global light found (expected at least one).");

		float3 lightDirWs = globalLight->Direction.Normalized();
		float3 lightColor = globalLight->Color;
		float lightIntensity = globalLight->Intensity;

		Matrix4x4 lightViewProj = {};

		// Update frame constants (including light matrices).
		{
			MapHelper<hlsl::FrameConstants> cb(ctx, m_pFrameCB, MAP_WRITE, MAP_FLAG_DISCARD);

			cb->View = view.ViewMatrix;
			cb->Proj = view.ProjMatrix;
			cb->ViewProj = view.ViewMatrix * view.ProjMatrix;
			cb->InvViewProj = cb->ViewProj.Inversed();

			cb->CameraPosition = cameraWs;

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

			const float3 lightForward = lightDirWs.Normalized();
			const float3 centerWs = cameraWs;

			const float shadowDistance = 20.0f;
			const float3 lightPosWs = centerWs - lightForward * shadowDistance;

			float3 up = float3(0, 1, 0);
			if (Abs(Vector3::Dot(up, lightForward)) > 0.99f)
			{
				up = float3(0, 0, 1);
			}

			const Matrix4x4 lightView = Matrix4x4::LookAtLH(lightPosWs, centerWs, up);
			const Matrix4x4 viewProj = cb->ViewProj;
			const Matrix4x4 invViewProj = viewProj.Inversed();

			float3 cornersWs[8] = {};

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
							ws.x *= invW;
							ws.y *= invW;
							ws.z *= invW;

							cornersWs[idx++] = float3(ws.x, ws.y, ws.z);
						}
					}
				}
			}

			float minX = +FLT_MAX;
			float minY = +FLT_MAX;
			float minZ = +FLT_MAX;
			float maxX = -FLT_MAX;
			float maxY = -FLT_MAX;
			float maxZ = -FLT_MAX;

			for (int i = 0; i < 8; ++i)
			{
				const float4 p = lightView.MulVector4(float4(cornersWs[i].x, cornersWs[i].y, cornersWs[i].z, 1.f));

				minX = std::min(minX, p.x);
				maxX = std::max(maxX, p.x);

				minY = std::min(minY, p.y);
				maxY = std::max(maxY, p.y);

				minZ = std::min(minZ, p.z);
				maxZ = std::max(maxZ, p.z);
			}

			const float xyPadding = 1.f;
			const float zPadding = 2.f;

			minX -= xyPadding;
			maxX += xyPadding;
			minY -= xyPadding;
			maxY += xyPadding;
			minZ -= zPadding;
			maxZ += zPadding;

			const Matrix4x4 lightProj = Matrix4x4::OrthoOffCenter(minX, maxX, minY, maxY, minZ, maxZ);
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

		// Pre-transition resources before entering render passes.
		std::vector<StateTransitionDesc> preBarriers;
		preBarriers.reserve(256);

		std::unordered_map<uint64, MaterialRenderData*> frameMatCache;
		frameMatCache.reserve(128);

		for (const RenderScene::RenderObject& obj : scene.GetObjects())
		{
			const StaticMeshRenderData* mesh = m_pRenderResourceCache->TryGetMesh(obj.MeshHandle);
			if (!mesh)
			{
				continue;
			}

			if (mesh->VertexBuffer)
			{
				preBarriers.push_back(
					StateTransitionDesc
					{
						mesh->VertexBuffer,
						RESOURCE_STATE_UNKNOWN,
						RESOURCE_STATE_VERTEX_BUFFER,
						STATE_TRANSITION_FLAG_UPDATE_STATE
					}
				);
			}

			for (const MeshSection& section : mesh->Sections)
			{
				if (section.IndexBuffer)
				{
					preBarriers.push_back(
						StateTransitionDesc
						{
							section.IndexBuffer,
							RESOURCE_STATE_UNKNOWN,
							RESOURCE_STATE_INDEX_BUFFER,
							STATE_TRANSITION_FLAG_UPDATE_STATE
						}
					);
				}

				const Handle<MaterialInstance> matHandle = section.Material;
				const uint64 matKey = static_cast<uint64>(matHandle.GetValue());

				if (frameMatCache.find(matKey) == frameMatCache.end())
				{
					MaterialRenderData* matRd =
						m_pRenderResourceCache->GetOrCreateMaterialRenderData(
							matHandle,
							m_GBufferPSO,
							m_pObjectCB,
							ctx
						);

					ASSERT(matRd, "GetOrCreateMaterialRenderData failed.");
					frameMatCache[matKey] = matRd;

					if (matRd->pMaterialCB)
					{
						preBarriers.push_back(
							StateTransitionDesc
							{
								matRd->pMaterialCB,
								RESOURCE_STATE_UNKNOWN,
								RESOURCE_STATE_CONSTANT_BUFFER,
								STATE_TRANSITION_FLAG_UPDATE_STATE
							}
						);
					}
				}
			}
		}

		if (m_pFrameCB)
		{
			preBarriers.push_back(
				StateTransitionDesc{ m_pFrameCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE }
			);
		}
		if (m_pObjectCB)
		{
			preBarriers.push_back(
				StateTransitionDesc{ m_pObjectCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE }
			);
		}
		if (m_pShadowCB)
		{
			preBarriers.push_back(
				StateTransitionDesc{ m_pShadowCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE }
			);
		}

		if (!preBarriers.empty())
		{
			ctx->TransitionResourceStates(static_cast<uint32>(preBarriers.size()), preBarriers.data());
		}

		// ============================================================
		// PASS 0: Shadow
		// ============================================================

		{
			StateTransitionDesc b =
			{
				m_ShadowMapTex,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_DEPTH_WRITE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			ctx->TransitionResourceStates(1, &b);
		}

		{
			Viewport vp = {};
			vp.TopLeftX = 0.f;
			vp.TopLeftY = 0.f;
			vp.Width = static_cast<float>(SHADOW_MAP_SIZE);
			vp.Height = static_cast<float>(SHADOW_MAP_SIZE);
			vp.MinDepth = 0.f;
			vp.MaxDepth = 1.f;
			ctx->SetViewports(1, &vp, 0, 0);
		}

		{
			OptimizedClearValue clearVals[1] = {};
			clearVals[0].DepthStencil.Depth = 1.f;
			clearVals[0].DepthStencil.Stencil = 0;

			BeginRenderPassAttribs rpBegin = {};
			rpBegin.pRenderPass = m_RenderPassShadow;
			rpBegin.pFramebuffer = m_FrameBufferShadow;
			rpBegin.ClearValueCount = 1;
			rpBegin.pClearValues = clearVals;

			ctx->BeginRenderPass(rpBegin);

			ctx->SetPipelineState(m_ShadowPSO);
			ctx->CommitShaderResources(m_ShadowSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			for (const RenderScene::RenderObject& obj : scene.GetObjects())
			{
				const StaticMeshRenderData* mesh = m_pRenderResourceCache->TryGetMesh(obj.MeshHandle);
				if (!mesh)
				{
					continue;
				}

				{
					MapHelper<hlsl::ObjectConstants> cb(ctx, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
					cb->World = obj.Transform;
					cb->WorldInvTranspose = obj.Transform.Inversed().Transposed();
				}

				IBuffer* vbs[] = { mesh->VertexBuffer };
				uint64 offsets[] = { 0 };

				ctx->SetVertexBuffers(
					0,
					1,
					vbs,
					offsets,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET
				);

				for (const MeshSection& section : mesh->Sections)
				{
					ctx->SetIndexBuffer(section.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					DrawIndexedAttribs dia = {};
					dia.NumIndices = section.NumIndices;
					dia.IndexType = (section.IndexType == VT_UINT16) ? VT_UINT16 : VT_UINT32;
					dia.Flags = DRAW_FLAG_VERIFY_ALL;
					dia.FirstIndexLocation = section.StartIndex;

					ctx->DrawIndexed(dia);
				}
			}

			ctx->EndRenderPass();
		}

		{
			StateTransitionDesc b =
			{
				m_ShadowMapTex,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_SHADER_RESOURCE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			ctx->TransitionResourceStates(1, &b);
		}

		{
			Viewport vp = {};
			vp.TopLeftX = static_cast<float>(view.Viewport.left);
			vp.TopLeftY = static_cast<float>(view.Viewport.top);
			vp.Width = static_cast<float>(view.Viewport.right - view.Viewport.left);
			vp.Height = static_cast<float>(view.Viewport.bottom - view.Viewport.top);
			vp.MinDepth = 0.f;
			vp.MaxDepth = 1.f;
			ctx->SetViewports(1, &vp, 0, 0);
		}

		// ============================================================
		// PASS 1: GBuffer
		// ============================================================

		{
			StateTransitionDesc b[] =
			{
				{ m_GBufferTex[0],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[1],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[2],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[3],   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferDepthTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_DEPTH_WRITE,   STATE_TRANSITION_FLAG_UPDATE_STATE }
			};
			ctx->TransitionResourceStates(_countof(b), b);
		}

		{
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

			BeginRenderPassAttribs rpBegin = {};
			rpBegin.pRenderPass = m_RenderPassGBuffer;
			rpBegin.pFramebuffer = m_FrameBufferGBuffer;
			rpBegin.ClearValueCount = 5;
			rpBegin.pClearValues = clearVals;

			ctx->BeginRenderPass(rpBegin);

			for (const RenderScene::RenderObject& obj : scene.GetObjects())
			{
				const StaticMeshRenderData* mesh = m_pRenderResourceCache->TryGetMesh(obj.MeshHandle);
				if (!mesh)
				{
					continue;
				}

				{
					MapHelper<hlsl::ObjectConstants> cb(ctx, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
					cb->World = obj.Transform;
					cb->WorldInvTranspose = obj.Transform.Inversed().Transposed();
				}

				IBuffer* vbs[] = { mesh->VertexBuffer };
				uint64 offsets[] = { 0 };

				ctx->SetVertexBuffers(
					0,
					1,
					vbs,
					offsets,
					RESOURCE_STATE_TRANSITION_MODE_VERIFY,
					SET_VERTEX_BUFFERS_FLAG_RESET
				);

				Handle<MaterialInstance> lastMat = {};

				for (const MeshSection& section : mesh->Sections)
				{
					ctx->SetIndexBuffer(section.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

					const Handle<MaterialInstance> matHandle = section.Material;
					const uint64 matKey = static_cast<uint64>(matHandle.GetValue());

					MaterialRenderData* matRd = frameMatCache[matKey];
					ASSERT(matRd, "frameMatCache missing MaterialRenderData.");

					if (matHandle != lastMat)
					{
						// Important: matRd->pPSO must NOT call SetRenderTargets internally.
						ctx->SetPipelineState(matRd->pPSO);
						ctx->CommitShaderResources(matRd->pSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
						lastMat = matHandle;
					}

					{
						MapHelper<hlsl::MaterialConstants> cb(ctx, matRd->pMaterialCB, MAP_WRITE, MAP_FLAG_DISCARD);
						cb->BaseColorFactor = matRd->BaseColor;
						cb->EmissiveFactor = matRd->Emissive;
						cb->MetallicFactor = matRd->Metallic;
						cb->RoughnessFactor = matRd->Roughness;
						cb->NormalScale = matRd->NormalScale;
						cb->OcclusionStrength = matRd->OcclusionStrength;
						cb->AlphaCutoff = matRd->AlphaCutoff;
						cb->Flags = matRd->MaterialFlags;
					}

					DrawIndexedAttribs dia = {};
					dia.NumIndices = section.NumIndices;
					dia.IndexType = (section.IndexType == VT_UINT16) ? VT_UINT16 : VT_UINT32;
					dia.Flags = DRAW_FLAG_VERIFY_ALL;
					dia.FirstIndexLocation = section.StartIndex;

					ctx->DrawIndexed(dia);
				}
			}

			ctx->EndRenderPass();
		}

		{
			StateTransitionDesc barriers[] =
			{
				{ m_GBufferTex[0],     RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[1],     RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[2],     RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferTex[3],     RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE },
				{ m_GBufferDepthTex,   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE }
			};
			ctx->TransitionResourceStates(_countof(barriers), barriers);
		}

		// ============================================================
		// PASS 2: Lighting
		// ============================================================

		{
			OptimizedClearValue clearVals[1] = {};
			clearVals[0].Color[3] = 1.f;

			BeginRenderPassAttribs rpBegin = {};
			rpBegin.pRenderPass = m_RenderPassLighting;
			rpBegin.pFramebuffer = m_FrameBufferLighting;
			rpBegin.ClearValueCount = 1;
			rpBegin.pClearValues = clearVals;

			ctx->BeginRenderPass(rpBegin);

			ctx->SetPipelineState(m_LightingPSO);
			ctx->CommitShaderResources(m_LightingSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			drawFullScreenTriangle();

			ctx->EndRenderPass();
		}

		{
			StateTransitionDesc b =
			{
				m_LightingTex,
				RESOURCE_STATE_UNKNOWN,
				RESOURCE_STATE_SHADER_RESOURCE,
				STATE_TRANSITION_FLAG_UPDATE_STATE
			};
			ctx->TransitionResourceStates(1, &b);
		}

		// ============================================================
		// PASS 3: PostCopy (Lighting -> BackBuffer)
		// ============================================================

		{
			ITextureView* bbRtv = swapChain->GetCurrentBackBufferRTV();

			FramebufferDesc fbDesc = {};
			fbDesc.Name = "FB_Post_Frame";
			fbDesc.pRenderPass = m_RenderPassPost;
			fbDesc.AttachmentCount = 1;
			fbDesc.ppAttachments = &bbRtv;

			m_FrameBufferPost.Release();
			device->CreateFramebuffer(fbDesc, &m_FrameBufferPost);

			// Bind input.
			m_PostSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_InputColor")->Set(m_LightingSRV);

			OptimizedClearValue clearVals[1] = {};
			clearVals[0].Color[3] = 1.f;

			BeginRenderPassAttribs rpBegin = {};
			rpBegin.pRenderPass = m_RenderPassPost;
			rpBegin.pFramebuffer = m_FrameBufferPost;
			rpBegin.ClearValueCount = 1;
			rpBegin.pClearValues = clearVals;

			ctx->BeginRenderPass(rpBegin);

			ctx->SetPipelineState(m_PostPSO);
			ctx->CommitShaderResources(m_PostSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			drawFullScreenTriangle();

			ctx->EndRenderPass();
		}
	}

	// ============================================================
	// PSO
	// ============================================================

	bool Renderer::createBasicPso()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "Device is null.");

		const SwapChainDesc& scDesc = m_CreateInfo.pSwapChain->GetDesc();

		GraphicsPipelineStateCreateInfo psoCi = {};
		psoCi.PSODesc.Name = "Debug Basic PSO";
		psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
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

		ShaderCreateInfo sci = {};
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.EntryPoint = "main";
		sci.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR | SHADER_COMPILE_FLAG_SKIP_OPTIMIZATION;

		RefCntAutoPtr<IShader> vs;
		{
			sci.Desc = {};
			sci.Desc.Name = "Basic VS";
			sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
			sci.FilePath = "Basic.vsh";
			sci.Desc.UseCombinedTextureSamplers = false;

			device->CreateShader(sci, &vs);
			if (!vs)
			{
				ASSERT(false, "Failed to create Basic VS.");
				return false;
			}
		}

		RefCntAutoPtr<IShader> ps;
		{
			sci.Desc = {};
			sci.Desc.Name = "Basic PS";
			sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
			sci.FilePath = "Basic.psh";
			sci.Desc.UseCombinedTextureSamplers = false;

			device->CreateShader(sci, &ps);
			if (!ps)
			{
				ASSERT(false, "Failed to create Basic PS.");
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

		device->CreateGraphicsPipelineState(psoCi, &m_PsoBasic);
		if (!m_PsoBasic)
		{
			ASSERT(false, "Failed to create Basic PSO.");
			return false;
		}

		{
			if (auto* var = m_PsoBasic->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FRAME_CONSTANTS"))
			{
				var->Set(m_pFrameCB);
			}
			if (auto* var = m_PsoBasic->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
			{
				var->Set(m_pFrameCB);
			}
		}

		return true;
	}

	bool Renderer::createGBufferPso()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "Device is null.");

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


	bool Renderer::createShadowTargets()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "createShadowTargets(): device null.");

		if (m_ShadowMapTex && m_ShadowMapDsv && m_ShadowMapSrv)
		{
			return true;
		}

		TextureDesc td = {};
		td.Name = "ShadowMap";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = SHADOW_MAP_SIZE;
		td.Height = SHADOW_MAP_SIZE;
		td.MipLevels = 1;
		td.SampleCount = 1;
		td.Usage = USAGE_DEFAULT;

		// Key: typeless + depth-stencil + shader-resource.
		td.Format = TEX_FORMAT_R32_TYPELESS;
		td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

		m_ShadowMapTex.Release();
		m_ShadowMapDsv.Release();
		m_ShadowMapSrv.Release();

		device->CreateTexture(td, nullptr, &m_ShadowMapTex);
		ASSERT(m_ShadowMapTex, "Failed to create ShadowMap texture.");

		// DSV view.
		{
			TextureViewDesc vd = {};
			vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			vd.Format = TEX_FORMAT_D32_FLOAT;

			m_ShadowMapTex->CreateView(vd, &m_ShadowMapDsv);
			ASSERT(m_ShadowMapDsv, "Failed to create ShadowMap DSV.");
		}

		// SRV view (sample depth as R32_FLOAT).
		{
			TextureViewDesc vd = {};
			vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
			vd.Format = TEX_FORMAT_R32_FLOAT;

			m_ShadowMapTex->CreateView(vd, &m_ShadowMapSrv);
			ASSERT(m_ShadowMapSrv, "Failed to create ShadowMap SRV.");
		}

		// Shadow constants CB.
		if (!m_pShadowCB)
		{
			CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ShadowConstants), "Shadow constants CB", &m_pShadowCB);
		}

		return true;
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
		{
			return true;
		}

		m_DeferredWidth = w;
		m_DeferredHeight = h;

		// Single-use helper is expressed as a local lambda (per your rule).
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

		// Must match createGBufferPso() formats.
		createRtTexture2d(w, h, TEX_FORMAT_RGBA8_UNORM, "GBuffer0_AlbedoA", m_GBufferTex[0], m_GBufferRtv[0], m_GBufferSrv[0]);
		createRtTexture2d(w, h, TEX_FORMAT_RGBA16_FLOAT, "GBuffer1_NormalWS", m_GBufferTex[1], m_GBufferRtv[1], m_GBufferSrv[1]);
		createRtTexture2d(w, h, TEX_FORMAT_RGBA8_UNORM, "GBuffer2_MRAO", m_GBufferTex[2], m_GBufferRtv[2], m_GBufferSrv[2]);
		createRtTexture2d(w, h, TEX_FORMAT_RGBA16_FLOAT, "GBuffer3_Emissive", m_GBufferTex[3], m_GBufferRtv[3], m_GBufferSrv[3]);

		// Lighting intermediate: LDR = swapchain format. For HDR, use RGBA16F.
		createRtTexture2d(w, h, sc.ColorBufferFormat, "LightingColor", m_LightingTex, m_LightingRTV, m_LightingSRV);

		// GBuffer depth: typeless + DSV + SRV.
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

		// Render passes/framebuffers must be recreated because attachments changed.
		m_RenderPassGBuffer.Release();
		m_FrameBufferGBuffer.Release();
		m_RenderPassLighting.Release();
		m_FrameBufferLighting.Release();
		m_RenderPassPost.Release();
		m_FrameBufferPost.Release();

		// SRBs depend on views, so rebuild them too.
		m_LightingSRB.Release();
		m_PostSRB.Release();

		return true;
	}

	bool Renderer::createShadowRenderPasses()
	{
		IRenderDevice* device = m_CreateInfo.pDevice.RawPtr();
		ASSERT(device, "createShadowRenderPasses(): device is null.");

		// Already created.
		if (m_RenderPassShadow && m_FrameBufferShadow)
		{
			return true;
		}

		// Depth-only render pass.
		{
			RenderPassAttachmentDesc attachments[1] = {};
			attachments[0].Format = TEX_FORMAT_D32_FLOAT;
			attachments[0].SampleCount = 1;
			attachments[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
			attachments[0].StoreOp = ATTACHMENT_STORE_OP_STORE;

			// NOTE:
			// Keeping final state as DEPTH_WRITE is OK if we do an explicit transition to SRV after the pass.
			attachments[0].InitialState = RESOURCE_STATE_DEPTH_WRITE;
			attachments[0].FinalState = RESOURCE_STATE_DEPTH_WRITE;

			AttachmentReference depthRef = {};
			depthRef.AttachmentIndex = 0;
			depthRef.State = RESOURCE_STATE_DEPTH_WRITE;

			SubpassDesc subpass = {};
			subpass.RenderTargetAttachmentCount = 0;
			subpass.pDepthStencilAttachment = &depthRef;

			RenderPassDesc rpDesc = {};
			rpDesc.Name = "RP_Shadow";
			rpDesc.AttachmentCount = 1;
			rpDesc.pAttachments = attachments;
			rpDesc.SubpassCount = 1;
			rpDesc.pSubpasses = &subpass;

			m_RenderPassShadow.Release();
			device->CreateRenderPass(rpDesc, &m_RenderPassShadow);
			if (!m_RenderPassShadow)
			{
				ASSERT(false, "createShadowRenderPasses(): CreateRenderPass failed.");
				return false;
			}
		}

		// Framebuffer.
		{
			ITextureView* atch[1] = { m_ShadowMapDsv };

			FramebufferDesc fbDesc = {};
			fbDesc.Name = "FB_Shadow";
			fbDesc.pRenderPass = m_RenderPassShadow;
			fbDesc.AttachmentCount = 1;
			fbDesc.ppAttachments = atch;

			m_FrameBufferShadow.Release();
			device->CreateFramebuffer(fbDesc, &m_FrameBufferShadow);
			if (!m_FrameBufferShadow)
			{
				ASSERT(false, "createShadowRenderPasses(): CreateFramebuffer failed.");
				return false;
			}
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

				attachments[i].InitialState = RESOURCE_STATE_RENDER_TARGET;
				attachments[i].FinalState = RESOURCE_STATE_RENDER_TARGET;
			}

			// Depth attachment.
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

} // namespace shz
