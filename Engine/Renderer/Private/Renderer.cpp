#include "pch.h"
#include "Engine/Renderer/Public/Renderer.h"

#include "Engine/Core/Math/Math.h"

#include "Engine/Image/Public/TextureUtilities.h"
#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"
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
			RefCntAutoPtr<IBuffer> drawCB;
			RefCntAutoPtr<IBuffer> shadowCB;

			CreateUniformBuffer(dev, sizeof(hlsl::FrameConstants), "Frame constants", &frameCB);
			CreateUniformBuffer(dev, sizeof(hlsl::DrawConstants), "Draw constants", &drawCB);
			CreateUniformBuffer(dev, sizeof(hlsl::ShadowConstants), "Shadow constants", &shadowCB);

			ASSERT(frameCB, "Frame CB create failed.");
			ASSERT(drawCB, "Draw CB create failed.");
			ASSERT(shadowCB, "Shadow CB create failed.");

			m_pRegistry->RegisterBuffer(STRING_HASH("FRAME_CONSTANTS"), std::move(frameCB));
			m_pRegistry->RegisterBuffer(STRING_HASH("DRAW_CONSTANTS"), std::move(drawCB));
			m_pRegistry->RegisterBuffer(STRING_HASH("SHADOW_CONSTANTS"), std::move(shadowCB));

			m_pPipelineStateManager->RegisterStaticBufferCBV("FRAME_CONSTANTS", STRING_HASH("FRAME_CONSTANTS"));
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

			AddBuffer(STRING_HASH("ObjectTable.GBuffer"), std::move(createObjectTable("ObjectTableSB.GBuffer")));
			AddBuffer(STRING_HASH("ObjectTable.Forward"), std::move(createObjectTable("ObjectTableSB.Forward")));
			AddBuffer(STRING_HASH("ObjectTable.Shadow"), std::move(createObjectTable("ObjectTableSB.Shadow")));

			m_pPipelineStateManager->RegisterStaticBufferSRV("g_ObjectTable", STRING_HASH("ObjectTable.GBuffer"));
			m_pPipelineStateManager->RegisterStaticBufferSRV("g_ForwardObjectTable", STRING_HASH("ObjectTable.Forward"));
			m_pPipelineStateManager->RegisterStaticBufferSRV("g_ShadowObjectTable", STRING_HASH("ObjectTable.Shadow"));
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

		// -----------------------------------------------------------------
		// Create shadow map
		// -----------------------------------------------------------------
		static constexpr uint32 SHADOW_MAP_SIZE = 4096;
		m_PassCtx.ShadowMapResolution = SHADOW_MAP_SIZE;
		{
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

			m_pRegistry->RegisterTexture(STRING_HASH("ShadowMap"), CreateTexture(td));

			TextureViewDesc dsvDesc = {};
			dsvDesc.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			dsvDesc.Format = TEX_FORMAT_D32_FLOAT;
			m_pRegistry->CreateTextureView(STRING_HASH("ShadowMap"), dsvDesc);

			TextureViewDesc srvDesc = {};
			srvDesc.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
			srvDesc.Format = TEX_FORMAT_R32_FLOAT;
			m_pRegistry->CreateTextureView(STRING_HASH("ShadowMap"), srvDesc);

			m_pPipelineStateManager->RegisterStaticTextureResource("g_ShadowMap", STRING_HASH("ShadowMap"));
		}

		Material::RegisterTemplateLibrary(&m_TemplateLibrary);
		CreateMaterialTemplate("DefaultLit", "GBuffer.vsh", "GBuffer.psh");

		return true;
	}

	void Renderer::Cleanup()
	{
		ReleaseSwapChainBuffers();

		m_PassTable.clear();
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
		IBuffer* pDrawCB = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("DRAW_CONSTANTS"));
		IBuffer* pShadowCB = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("SHADOW_CONSTANTS"));

		IBuffer* pObjSB_GB = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("ObjectTable.GBuffer"));
		IBuffer* pObjSB_Forward = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("ObjectTable.Forward"));
		IBuffer* pObjSB_Shadow = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("ObjectTable.Shadow"));

		ITexture* pEnvTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvTex"));
		ITexture* pEnvDiffTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvDiffuseTex"));
		ITexture* pEnvSpecTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvSpecularTex"));
		ITexture* pEnvBrdfTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvBrdfTex"));
		ITexture* pErrorTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("ErrorTex"));

		ASSERT(pFrameCB, "FrameCB missing (registry).");
		ASSERT(pDrawCB, "DrawCB missing (registry).");
		ASSERT(pShadowCB, "ShadowCB missing (registry).");
		ASSERT(pObjSB_GB && pObjSB_Forward && pObjSB_Shadow, "ObjectTable SB missing (registry).");
		ASSERT(pEnvTex && pEnvDiffTex && pEnvSpecTex && pEnvBrdfTex, "Env textures missing (registry).");
		ASSERT(pErrorTex, "Error texture missing (registry).");

		m_PassCtx.pScene = &scene;
		m_PassCtx.pViewFamily = &viewFamily;
		m_PassCtx.DeltaTime = viewFamily.DeltaTime;

		const View& view = viewFamily.Views[0];

		// ------------------------------------------------------------
		// Build frustums: Main / Shadow
		// ------------------------------------------------------------
		ViewFrustumExt frustumMain = {};
		{
			const Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix;
			ExtractViewFrustumPlanesFromMatrix(viewProj, frustumMain);
		}

		// ------------------------------------------------------------
		// Update Frame/Shadow constants + compute lightViewProj
		// ------------------------------------------------------------
		Matrix4x4 lightViewProj = {};
		{
			MapHelper<hlsl::FrameConstants> cb(ctx, pFrameCB, MAP_WRITE, MAP_FLAG_DISCARD);

			cb->View = view.ViewMatrix;
			cb->Proj = view.ProjMatrix;
			cb->ViewProj = view.ViewMatrix * view.ProjMatrix;
			cb->InvViewProj = cb->ViewProj.Inversed();

			cb->CameraPosition = view.CameraPosition;

			cb->FrustumPlanesWS[0] = frustumMain.NearPlane;
			cb->FrustumPlanesWS[1] = frustumMain.FarPlane;
			cb->FrustumPlanesWS[2] = frustumMain.TopPlane;
			cb->FrustumPlanesWS[3] = frustumMain.BottomPlane;
			cb->FrustumPlanesWS[4] = frustumMain.LeftPlane;
			cb->FrustumPlanesWS[5] = frustumMain.RightPlane;

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

			// Global light (first one)
			const RenderScene::LightObject* globalLight = nullptr;
			for (const auto& l : scene.GetLights()) { globalLight = &l; break; }

			float3 lightDirWs = globalLight ? globalLight->Direction.Normalized() : float3(0, -1, 0);
			float3 lightColor = globalLight ? globalLight->Color : float3(1, 1, 1);
			float  lightIntensity = globalLight ? globalLight->Intensity : 1.0f;

			cb->LightDirWS = lightDirWs;
			cb->LightColor = lightColor;
			cb->LightIntensity = lightIntensity;

			// ---- Shadow lightViewProj (your existing block, unchanged) ----
			const float ShadowVisibleDistance = 200.0f;

			const float3 lightForward = lightDirWs;

			float3 up = float3(0, 1, 0);
			if (Abs(Vector3::Dot(up, lightForward)) > 0.99f) { up = float3(0, 0, 1); }

			auto CornerIndex = [](int xBit, int yBit, int zBit) -> int
			{
				return (xBit ? 1 : 0) | (yBit ? 2 : 0) | (zBit ? 4 : 0);
			};

			float3 shadowCornersWS[8] = {};
			{
				const float3 C = view.CameraPosition;

				for (int yBit = 0; yBit <= 1; ++yBit)
				{
					for (int xBit = 0; xBit <= 1; ++xBit)
					{
						const int idxNear = CornerIndex(xBit, yBit, 0);
						const int idxFar = CornerIndex(xBit, yBit, 1);

						const float3 N = frustumMain.FrustumCorners[idxNear];
						const float3 F = frustumMain.FrustumCorners[idxFar];

						shadowCornersWS[idxNear] = N;

						const float nearDist = (N - C).Length();
						const float farDist = (F - C).Length();

						float t = 1.0f;
						if (farDist > nearDist + 1e-4f)
						{
							t = (ShadowVisibleDistance - nearDist) / (farDist - nearDist);
						}
						t = Clamp(t, 0.0f, 1.0f);

						shadowCornersWS[idxFar] = Vector3::Lerp(N, F, t);
					}
				}
			}

			float3 centerWs = float3(0, 0, 0);
			for (int i = 0; i < 8; ++i) { centerWs += shadowCornersWS[i]; }
			centerWs *= (1.0f / 8.0f);

			const float3 lightPosWs = centerWs - lightForward * ShadowVisibleDistance;
			Matrix4x4 lightView = Matrix4x4::LookAtLH(lightPosWs, centerWs, up);

			float minX = +FLT_MAX, minY = +FLT_MAX, minZ = +FLT_MAX;
			float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

			for (int i = 0; i < 8; ++i)
			{
				const float4 pLs4 = float4(shadowCornersWS[i], 1.0f) * lightView;
				minX = Min(minX, pLs4.x);  minY = Min(minY, pLs4.y);  minZ = Min(minZ, pLs4.z);
				maxX = Max(maxX, pLs4.x);  maxY = Max(maxY, pLs4.y);  maxZ = Max(maxZ, pLs4.z);
			}

			const float pcfPadXY = 1.0f;
			const float padZ = 10.0f;

			minX -= pcfPadXY; minY -= pcfPadXY;
			maxX += pcfPadXY; maxY += pcfPadXY;

			float nearZ = minZ - padZ;
			float farZ = maxZ + padZ;

			const float centerX = 0.5f * (minX + maxX);
			const float centerY = 0.5f * (minY + maxY);

			float extentX = (maxX - minX);
			float extentY = (maxY - minY);
			float extent = Max(extentX, extentY);

			const float unitsPerTexelSqX = extent / m_PassCtx.ShadowMapResolution;
			const float unitsPerTexelSqY = extent / m_PassCtx.ShadowMapResolution;
			const float unitsPerTexelSq = Max(unitsPerTexelSqX, unitsPerTexelSqY);

			extent = ceil(extent / unitsPerTexelSq) * unitsPerTexelSq;

			minX = centerX - extent * 0.5f;
			maxX = centerX + extent * 0.5f;
			minY = centerY - extent * 0.5f;
			maxY = centerY + extent * 0.5f;

			minX = floor(minX / unitsPerTexelSq) * unitsPerTexelSq;
			minY = floor(minY / unitsPerTexelSq) * unitsPerTexelSq;
			maxX = ceil(maxX / unitsPerTexelSq) * unitsPerTexelSq;
			maxY = ceil(maxY / unitsPerTexelSq) * unitsPerTexelSq;

			const Matrix4x4 lightProj = Matrix4x4::OrthoOffCenter(
				minX, maxX,
				minY, maxY,
				nearZ, farZ);

			lightViewProj = lightView * lightProj;
			cb->LightViewProj = lightViewProj;
		}

		{
			MapHelper<hlsl::ShadowConstants> cb(ctx, pShadowCB, MAP_WRITE, MAP_FLAG_DISCARD);
			cb->LightViewProj = lightViewProj;
		}

		ViewFrustumExt frustumShadow = {};
		ExtractViewFrustumPlanesFromMatrix(lightViewProj, frustumShadow);

		// ------------------------------------------------------------
		// Visibility (dense object indices)
		// ------------------------------------------------------------
		std::vector<uint32> visibleObjectIndexMain = {};
		std::vector<uint32> visibleObjectIndexShadow = {};
		{
			const uint32 count = scene.GetObjectDenseCount();

			visibleObjectIndexMain.clear();
			visibleObjectIndexShadow.clear();

			visibleObjectIndexMain.reserve(count);
			visibleObjectIndexShadow.reserve(count);

			for (uint32 i = 0; i < count; ++i)
			{
				const RenderScene::SceneObject& obj = scene.GetObjectByDenseIndex(i);
				ASSERT(obj.pMesh, "Invalid scene object.");

				const Box& localBounds = obj.pMesh->LocalBounds;

				if (IntersectsFrustum(frustumMain, localBounds, obj.World, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
				{
					visibleObjectIndexMain.push_back(i);
				}

				if (obj.bCastShadow)
				{
					if (IntersectsFrustum(frustumShadow, localBounds, obj.World, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
					{
						visibleObjectIndexShadow.push_back(i);
					}
				}
			}
		}

		// ------------------------------------------------------------
		// Common barriers
		// ------------------------------------------------------------
		pushBarrier(pFrameCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		pushBarrier(pShadowCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		pushBarrier(pDrawCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

		pushBarrier(pObjSB_GB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pObjSB_Forward, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pObjSB_Shadow, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
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
		// ------------------------------------------------------------
		std::vector<uint32> instanceRemap;

		auto pipelineResolver = [this](MaterialId matId, uint64 rpKey) -> const MaterialPipelineBinding&
		{
			return this->AcquireMaterialPipelineBinding(matId, rpKey);
		};

		// GBuffer
		scene.BuildDrawPackets(
			STRING_HASH("GBuffer"),
			visibleObjectIndexMain,
			pipelineResolver,
			m_PassCtx.MainDrawPackets,
			instanceRemap);

		packObjectTableFromRemap(pObjSB_GB, instanceRemap);

		// Forward
		scene.BuildDrawPackets(
			STRING_HASH("Forward"),
			visibleObjectIndexMain,
			pipelineResolver,
			m_PassCtx.ForwardDrawPackets,
			instanceRemap);

		packObjectTableFromRemap(pObjSB_Forward, instanceRemap);

		// Shadow
		scene.BuildDrawPackets(
			STRING_HASH("Shadow"),
			visibleObjectIndexShadow,
			pipelineResolver,
			m_PassCtx.ShadowDrawPackets,
			instanceRemap);

		packObjectTableFromRemap(pObjSB_Shadow, instanceRemap);

		scene.BuildIndirectDrawPackets(STRING_HASH("GBuffer"), pipelineResolver, m_PassCtx.MainIndirectPackets);
		scene.BuildIndirectDrawPackets(STRING_HASH("Forward"), pipelineResolver, m_PassCtx.ForwardIndirectPackets);
		scene.BuildIndirectDrawPackets(STRING_HASH("Shadow"), pipelineResolver, m_PassCtx.ShadowIndirectPackets);

		IBuffer* pIndirectArgs = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("IndirectArgsBuffer"));
		ASSERT(pIndirectArgs, "IndirectArgs buffer missing.");

		auto patchIndirectPackets = [&](std::vector<DrawIndirectPacket>& packets)
		{
			for (DrawIndirectPacket& p : packets)
			{
				p.DrawAttribs.pAttribsBuffer = pIndirectArgs;
			}
		};
		patchIndirectPackets(m_PassCtx.MainIndirectPackets);
		patchIndirectPackets(m_PassCtx.ForwardIndirectPackets);
		patchIndirectPackets(m_PassCtx.ShadowIndirectPackets);

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

			if (pass.eDomain == EPassExecutionDomain::RenderPass && pass.pRHIRenderpass)
			{
				m_PassCtx.pRHIRenderPass = pass.pRHIRenderpass;

				BeginRenderPassAttribs rp = {};
				rp.pRenderPass = pass.pRHIRenderpass;
				rp.pFramebuffer = !pass.bUseSwapChainBackBuffer ? pass.pRHIFramebuffer : m_pSwapChainFramebuffer;
				rp.ClearValueCount = static_cast<uint32>(pass.ClearValues.size());
				rp.pClearValues = pass.ClearValues.empty() ? nullptr : pass.ClearValues.data();

				ctx->BeginRenderPass(rp);
				pass.ExecuteLambda(m_PassCtx);
				ctx->EndRenderPass();
			}
			else
			{
				pass.ExecuteLambda(m_PassCtx);
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
		std::function<void(RenderPassBuilder&)> buildLambda,
		std::function<void(RenderPassContext&)> executeLambda,
		std::function<void()> onCreated,
		EPassExecutionDomain domain)
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
				// bWrite && TODO: Read만 해도 DSV 생성하도록 해놈. 더 근본적인 해결?
				(a.Usage == RENDER_USAGE_DSV_WRITE || a.Usage == RENDER_USAGE_DSV_READ) &&
				a.TextureViewType == TEXTURE_VIEW_DEPTH_STENCIL)
			{
				ASSERT(!bHasDepth, "Multiple depth attachments are not supported yet.");

				ITexture* pTex = m_pRegistry->GetTexture(a.ResourceId);
				ASSERT(pTex, "DSV texture not found.");

				ITextureView* pDSV = m_pRegistry->GetTextureDSV(a.ResourceId);
				ASSERT(pDSV, "DSV view not found.");

				const TextureDesc& td = pTex->GetDesc();
				const TextureViewDesc& vd = pDSV->GetDesc();

				const bool bClear = hasClearValue(a.ResourceId);

				RenderPassAttachmentDesc at = {};
				at.Format = vd.Format; // View format
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
		ASSERT(!textureName.empty(), "Name is empty.");
		AddTextureView(STRING_HASH(textureName), viewDesc);
	}

	void Renderer::AddTextureView(uint64 textureId, const TextureViewDesc& viewDesc)
	{
		ASSERT(m_pRegistry, "Registry is null.");
		m_pRegistry->CreateTextureView(textureId, viewDesc);
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

		struct PackedStaticVertex final
		{
			float3 Pos;
			float2 UV;
			float3 Normal;
			float3 Tangent;
		};

		std::vector<PackedStaticVertex> packed;
		// Build packed vertex buffer data
		{
			const uint32 vtxCount = mesh.GetVertexCount();
			packed.resize(vtxCount);

			const std::vector<float3>& positions = mesh.GetPositions();
			const std::vector<float3>& normals = mesh.GetNormals();
			const std::vector<float3>& tangents = mesh.GetTangents();
			const std::vector<float2>& texCoords = mesh.GetTexCoords();

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

		const void* pIndexData = mesh.GetIndexData();
		const uint32 ibBytes = mesh.GetIndexDataSizeBytes();
		ASSERT(pIndexData && ibBytes > 0, "Invalid index data in StaticMeshAsset.");

		RefCntAutoPtr<IBuffer> pIB = createImmutableBuffer(m_pDevice, "StaticMesh_IB", BIND_INDEX_BUFFER, pIndexData, ibBytes);
		ASSERT(pIB, "Failed to create index buffer for StaticMesh.");

		StaticMeshRenderData out = {};
		out.VertexBuffer = pVB;
		out.IndexBuffer = pIB;
		out.VertexStride = static_cast<uint32>(sizeof(PackedStaticVertex));
		out.VertexCount = mesh.GetVertexCount();
		out.IndexCount = mesh.GetIndexCount();
		out.IndexType = mesh.GetIndexType();
		out.LocalBounds = mesh.GetBounds();

		out.Sections.reserve(mesh.GetSections().size());
		for (const auto& s : mesh.GetSections())
		{
			StaticMeshRenderData::Section d{};
			d.FirstIndex = s.FirstIndex;
			d.IndexCount = s.IndexCount;
			d.BaseVertex = s.BaseVertex;
			d.LocalBounds = s.LocalBounds;
			d.MaterialId = mesh.GetMaterialSlot(s.MaterialSlot);
			out.Sections.push_back(d);
		}

		pushBarrier(out.VertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
		pushBarrier(out.IndexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

		m_StaticMeshCache.Store(key, std::move(out));
		return m_StaticMeshCache.Acquire(key);
	}

	struct BillboardVertex final
	{
		float3 Pos = {};
		float2 UV = {};
	};

	static bool DumpBillboardMeshToOBJ(
		const char* filePath,
		const std::vector<BillboardVertex>& vertices,
		const std::vector<uint16>& indices)
	{
		if (vertices.empty() || indices.empty())
			return false;

		std::ofstream ofs(filePath, std::ios::out | std::ios::trunc);
		if (!ofs.is_open())
			return false;

		ofs << "# Billboard Cutout Mesh (debug dump)\n";
		ofs << "# verts: " << vertices.size()
			<< ", tris: " << (indices.size() / 3) << "\n\n";

		// ------------------------------------------------------------
		// Positions
		// ------------------------------------------------------------
		for (const BillboardVertex& v : vertices)
		{
			// OBJ: v x y z
			ofs << "v "
				<< v.Pos.x << " "
				<< v.Pos.y << " "
				<< v.Pos.z << "\n";
		}

		ofs << "\n";

		// ------------------------------------------------------------
		// UVs
		// OBJ는 v=0이 bottom이므로 그대로 써도 OK
		// ------------------------------------------------------------
		for (const BillboardVertex& v : vertices)
		{
			ofs << "vt "
				<< v.UV.x << " "
				<< (1.0f - v.UV.y) << "\n"; // Blender 기준 맞추려면 flip 권장
		}

		ofs << "\n";

		// ------------------------------------------------------------
		// Faces (OBJ는 1-based index)
		// f v/vt v/vt v/vt
		// ------------------------------------------------------------
		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const uint32 i0 = indices[i + 0] + 1;
			const uint32 i1 = indices[i + 1] + 1;
			const uint32 i2 = indices[i + 2] + 1;

			ofs << "f "
				<< i0 << "/" << i0 << " "
				<< i1 << "/" << i1 << " "
				<< i2 << "/" << i2 << "\n";
		}

		ofs.close();
		return true;
	}

	static inline uint32 ClampU32(int32 v, uint32 lo, uint32 hi)
	{
		return static_cast<uint32>(std::min<int32>(static_cast<int32>(hi), std::max<int32>(static_cast<int32>(lo), v)));
	}

	struct RGBA8 final
	{
		uint8 R = 0;
		uint8 G = 0;
		uint8 B = 0;
		uint8 A = 0;
	};

	static inline RGBA8 LoadRGBA8(const uint8* p)
	{
		RGBA8 c;
		c.R = p[0];
		c.G = p[1];
		c.B = p[2];
		c.A = p[3];
		return c;
	}

	static inline void StoreRGBA8(uint8* p, const RGBA8& c)
	{
		p[0] = c.R;
		p[1] = c.G;
		p[2] = c.B;
		p[3] = c.A;
	}

	//----------------------------------------------------------------------------
	// Alpha bleed / dilate RGB into transparent area.
	//
	// 목적:
	//  - 투명 영역(alpha=0)의 RGB가 흰/검 등 배경색으로 남아있으면,
	//    linear filtering/mip에서 가장자리에 색 번짐(fringe)이 생긴다.
	//  - 투명 영역 RGB를 주변의 "유효 색(opaque)"으로 채워 넣어서 fringe를 제거한다.
	//
	// 동작:
	//  - seedAlpha 이상인 픽셀을 "유효"로 간주
	//  - fillAlpha 이하인 픽셀에 대해서만 RGB를 채움(알파는 유지)
	//  - radiusPixels 만큼 확장
	//
	// 주의:
	//  - alpha는 건드리지 않는다. (마스크/블렌딩 의미가 바뀜)
	//  - 텍스처가 straight alpha든 premultiplied든, "투명 영역 RGB"를 채워주는 건 유효하다.
	//----------------------------------------------------------------------------
	static void AlphaBleedRGBA8(
		TextureMip& mip,
		uint8 seedAlpha = 1,     // 유효 픽셀 기준(보통 1~16 권장)
		uint8 fillAlpha = 0,     // 채울 대상 기준(보통 0)
		uint32 radiusPixels = 16 // 패딩 폭(예: 8~32)
	)
	{
		ASSERT(mip.Width > 0 && mip.Height > 0, "AlphaBleed: invalid mip size.");
		ASSERT(!mip.Data.empty(), "AlphaBleed: mip data is empty.");
		ASSERT((mip.Data.size() == size_t(mip.Width) * size_t(mip.Height) * 4u),
			"AlphaBleed: expected tightly packed RGBA8.");

		const uint32 W = mip.Width;
		const uint32 H = mip.Height;
		const uint32 Stride = W * 4u;

		// Work buffers (double buffer)
		std::vector<uint8> curr = mip.Data;
		std::vector<uint8> next = mip.Data;

		auto Index = [&](uint32 x, uint32 y) -> uint32
		{
			return y * Stride + x * 4u;
		};

		// "유효 픽셀" 마스크를 단계별로 갱신 (radius 확장)
		// valid[x,y] == 1 이면 이 픽셀의 RGB는 신뢰할 수 있는(시드 또는 이미 채워진) 색이다.
		std::vector<uint8> valid(size_t(W) * size_t(H), 0);

		// 초기 valid 설정: seedAlpha 이상이면 유효
		for (uint32 y = 0; y < H; ++y)
		{
			for (uint32 x = 0; x < W; ++x)
			{
				const uint32 i = Index(x, y);
				const uint8 a = curr[i + 3];
				valid[size_t(y) * size_t(W) + size_t(x)] = (a >= seedAlpha) ? 1 : 0;
			}
		}

		// 8-neighborhood
		static constexpr int32 kDx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
		static constexpr int32 kDy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

		// radiusPixels 번 확장
		for (uint32 step = 0; step < radiusPixels; ++step)
		{
			bool anyWrite = false;

			// next는 curr에서 시작 (alpha는 그대로 유지)
			next = curr;

			// 이 step에서 새로 유효가 된 픽셀을 추적할 새 valid
			std::vector<uint8> newValid = valid;

			for (uint32 y = 0; y < H; ++y)
			{
				for (uint32 x = 0; x < W; ++x)
				{
					const size_t vidx = size_t(y) * size_t(W) + size_t(x);

					// 이미 유효면 패스 (RGB가 이미 믿을만함)
					if (valid[vidx] != 0)
						continue;

					const uint32 i = Index(x, y);
					const uint8 a = curr[i + 3];

					// "진짜 투명"만 채우기 (원하면 fillAlpha를 8~32 같은 값으로 올릴 수도 있음)
					if (a > fillAlpha)
						continue;

					// 주변 유효 픽셀들의 RGB 평균(또는 가중)으로 채움
					uint32 sumR = 0;
					uint32 sumG = 0;
					uint32 sumB = 0;
					uint32 count = 0;

					for (uint32 k = 0; k < 8; ++k)
					{
						const int32 nx = int32(x) + kDx[k];
						const int32 ny = int32(y) + kDy[k];
						if (nx < 0 || ny < 0 || nx >= int32(W) || ny >= int32(H))
							continue;

						const size_t nvidx = size_t(ny) * size_t(W) + size_t(nx);
						if (valid[nvidx] == 0)
							continue;

						const uint32 ni = Index(uint32(nx), uint32(ny));
						sumR += curr[ni + 0];
						sumG += curr[ni + 1];
						sumB += curr[ni + 2];
						++count;
					}

					if (count > 0)
					{
						next[i + 0] = uint8(sumR / count);
						next[i + 1] = uint8(sumG / count);
						next[i + 2] = uint8(sumB / count);
						// next[i + 3] (alpha) 는 유지

						newValid[vidx] = 1; // 이제 이 픽셀도 "유효 색"으로 취급 가능
						anyWrite = true;
					}
				}
			}

			curr.swap(next);
			valid.swap(newValid);

			// 더 이상 확장될 게 없으면 조기 종료
			if (!anyWrite)
				break;
		}

		mip.Data.swap(curr);
	}

	//----------------------------------------------------------------------------
	// Texture 전체에 alpha bleed 적용 (모든 mip에 적용 가능)
	//----------------------------------------------------------------------------
	static void AlphaBleedTextureRGBA8(
		Texture& tex,
		uint8 seedAlpha = 1,
		uint8 fillAlpha = 0,
		uint32 radiusPixelsAtTopMip = 16,
		bool bApplyToAllMips = true
	)
	{
		ASSERT(tex.GetFormat() == TEX_FORMAT_RGBA8_UNORM, "AlphaBleedTextureRGBA8: only RGBA8 supported here.");

		auto& mips = tex.GetMips();
		ASSERT(!mips.empty(), "AlphaBleedTextureRGBA8: no mips.");

		for (size_t mipIndex = 0; mipIndex < mips.size(); ++mipIndex)
		{
			if (!bApplyToAllMips && mipIndex != 0)
				break;

			// mip이 작아질수록 radius를 줄이는게 보통 유리 (너무 과도한 확장 방지)
			// ex) 1024 mip 16px, 512 mip 8px, 256 mip 4px ...
			uint32 radius = radiusPixelsAtTopMip;
			{
				const uint32 div = 1u << uint32(mipIndex);
				radius = std::max<uint32>(1u, radiusPixelsAtTopMip / div);
			}

			AlphaBleedRGBA8(mips[mipIndex], seedAlpha, fillAlpha, radius);
		}
	}

	const BillboardRenderData& Renderer::CreateBillboardRenderData(
		const AssetRef<Texture>& colorTexRef,
		MATERIAL_BLEND_MODE blendMode,
		float2 scale,
		float2 pivot01)
	{
		ASSERT(scale.x > 0.0f && scale.y > 0.0f, "Scale value must be positive.");
		ASSERT(pivot01.x >= 0.0f && pivot01.y >= 0.0f && pivot01.x <= 1.0f && pivot01.y <= 1.0f,
			"pivot value range must be 0~1.");

		BillboardRenderData billboard = {};
		billboard.BlendMode = blendMode;

		AssetPtr<Texture> colorTexPtr = m_pAssetManager->LoadBlocking<Texture>(colorTexRef);
		ASSERT(colorTexPtr, "CreateBillboardRenderData: LoadBlocking<Texture> failed.");
		Texture& colorTex = *colorTexPtr;

		shz::AlphaBleedTextureRGBA8(
			colorTex,
			/*seedAlpha*/ 8,          // 1~16 사이 추천 (경계가 얇으면 1~4)
			/*fillAlpha*/ 0,
			/*radius*/    16,
			/*allMips*/   true
		);

		billboard.BaseColorTex = CreateTexture(colorTexRef.GetSourcePath() + "_Bleed", colorTex);
		ASSERT(billboard.BaseColorTex, "Create billboard base color texture failed.");

		// ---------------------------------------------------------------------
		// Cutout mesh build from alpha (CPU)
		// ---------------------------------------------------------------------
		auto IsRGBA8 = [](TEXTURE_FORMAT fmt) -> bool
		{
			return fmt == TEX_FORMAT_RGBA8_UNORM || fmt == TEX_FORMAT_RGBA8_UNORM_SRGB;
		};

		auto GetAlphaAt = [&](int x, int y) -> uint8
		{
			// Assumes tightly packed RGBA8.
			const uint32 w = colorTex.GetWidth();
			const uint32 h = colorTex.GetHeight();
			x = std::max(0, std::min((int)w - 1, x));
			y = std::max(0, std::min((int)h - 1, y));

			const uint8* data = colorTex.GetData();
			const uint32 idx = (uint32)(y * (int)w + x) * 4u;
			return data[idx + 3u];
		};

		struct IPoint final
		{
			int x = 0;
			int y = 0;
		};

		struct Segment final
		{
			IPoint A = {};
			IPoint B = {};
		};

		struct Loop2D final
		{
			// Quantized points in grid*2 space (so we can represent half steps exactly).
			std::vector<IPoint> P = {};
		};

		auto SignedArea = [](const std::vector<float2>& poly) -> float
		{
			double a = 0.0;
			const int n = (int)poly.size();
			for (int i = 0; i < n; ++i)
			{
				const float2 p0 = poly[i];
				const float2 p1 = poly[(i + 1) % n];
				a += (double)p0.x * (double)p1.y - (double)p1.x * (double)p0.y;
			}
			return (float)(0.5 * a);
		};

		auto PointInTri = [](const float2& p, const float2& a, const float2& b, const float2& c) -> bool
		{
			// Barycentric (works for either winding if consistent).
			const float2 v0 = c - a;
			const float2 v1 = b - a;
			const float2 v2 = p - a;

			const float dot00 = v0.x * v0.x + v0.y * v0.y;
			const float dot01 = v0.x * v1.x + v0.y * v1.y;
			const float dot02 = v0.x * v2.x + v0.y * v2.y;
			const float dot11 = v1.x * v1.x + v1.y * v1.y;
			const float dot12 = v1.x * v2.x + v1.y * v2.y;

			const float denom = dot00 * dot11 - dot01 * dot01;
			if (fabsf(denom) < 1e-12f)
				return false;

			const float inv = 1.0f / denom;
			const float u = (dot11 * dot02 - dot01 * dot12) * inv;
			const float v = (dot00 * dot12 - dot01 * dot02) * inv;

			return (u >= 0.0f) && (v >= 0.0f) && (u + v <= 1.0f);
		};

		auto IsConvex = [](const float2& prev, const float2& cur, const float2& next) -> bool
		{
			// For CCW polygon in (u,v) space.
			const float2 a = cur - prev;
			const float2 b = next - cur;
			const float cross = a.x * b.y - a.y * b.x;
			return cross > 0.0f;
		};

		auto RdpSimplify = [](const std::vector<float2>& inPts, float epsilon) -> std::vector<float2>
		{
			const int n = (int)inPts.size();
			if (n <= 3)
				return inPts;

			auto DistPointToSeg = [](const float2& p, const float2& a, const float2& b) -> float
			{
				const float2 ab = b - a;
				const float2 ap = p - a;
				const float ab2 = ab.x * ab.x + ab.y * ab.y;
				if (ab2 < 1e-20f)
				{
					const float2 d = p - a;
					return sqrtf(d.x * d.x + d.y * d.y);
				}
				float t = (ap.x * ab.x + ap.y * ab.y) / ab2;
				t = std::max(0.0f, std::min(1.0f, t));
				const float2 q = a + ab * t;
				const float2 d = p - q;
				return sqrtf(d.x * d.x + d.y * d.y);
			};

			std::vector<uint8> keep(n, 0);

			std::function<void(int, int)> Recurse = [&](int i0, int i1)
			{
				float dmax = 0.0f;
				int imax = -1;

				const float2 a = inPts[i0];
				const float2 b = inPts[i1];

				for (int i = i0 + 1; i < i1; ++i)
				{
					const float d = DistPointToSeg(inPts[i], a, b);
					if (d > dmax)
					{
						dmax = d;
						imax = i;
					}
				}

				if (imax >= 0 && dmax > epsilon)
				{
					keep[imax] = 1;
					Recurse(i0, imax);
					Recurse(imax, i1);
				}
			};

			keep[0] = 1;
			keep[n - 1] = 1;
			Recurse(0, n - 1);

			std::vector<float2> out;
			out.reserve(n);
			for (int i = 0; i < n; ++i)
			{
				if (keep[i])
					out.push_back(inPts[i]);
			}

			// If it over-simplified to < 3, fallback
			if ((int)out.size() < 3)
				return inPts;

			return out;
		};

		auto EarClipTriangulate = [&](const std::vector<float2>& polyCCW, std::vector<uint16>& outIndices) -> bool
		{
			const int n = (int)polyCCW.size();
			if (n < 3 || n > 65535)
				return false;

			std::vector<int> V(n);
			for (int i = 0; i < n; ++i) V[i] = i;

			auto IsEar = [&](int iPrev, int iCur, int iNext) -> bool
			{
				const float2 a = polyCCW[iPrev];
				const float2 b = polyCCW[iCur];
				const float2 c = polyCCW[iNext];

				if (!IsConvex(a, b, c))
					return false;

				// No other point inside triangle
				for (int k = 0; k < (int)V.size(); ++k)
				{
					const int idx = V[k];
					if (idx == iPrev || idx == iCur || idx == iNext)
						continue;

					if (PointInTri(polyCCW[idx], a, b, c))
						return false;
				}
				return true;
			};

			outIndices.clear();
			outIndices.reserve((n - 2) * 3);

			int guard = 0;
			while ((int)V.size() > 3 && guard < 100000)
			{
				++guard;
				bool clipped = false;

				const int m = (int)V.size();
				for (int i = 0; i < m; ++i)
				{
					const int iPrev = V[(i + m - 1) % m];
					const int iCur = V[i];
					const int iNext = V[(i + 1) % m];

					if (IsEar(iPrev, iCur, iNext))
					{
						outIndices.push_back((uint16)iPrev);
						outIndices.push_back((uint16)iCur);
						outIndices.push_back((uint16)iNext);

						V.erase(V.begin() + i);
						clipped = true;
						break;
					}
				}

				if (!clipped)
				{
					// Likely self-intersection or degeneracy; fail.
					return false;
				}
			}

			if ((int)V.size() == 3)
			{
				outIndices.push_back((uint16)V[0]);
				outIndices.push_back((uint16)V[1]);
				outIndices.push_back((uint16)V[2]);
				return true;
			}
			return false;
		};

		auto BuildCutoutMeshFromAlpha = [&](std::vector<BillboardVertex>& outVerts, std::vector<uint16>& outIdx) -> bool
		{
			if (!IsRGBA8(colorTex.GetFormat()))
				return false;

			const uint32 srcW = colorTex.GetWidth();
			const uint32 srcH = colorTex.GetHeight();
			if (srcW == 0 || srcH == 0)
				return false;

			// Downsample to limit marching squares cost.
			// Keep aspect, cap max dimension.
			const uint32 kMaxDim = 128;
			uint32 w = srcW;
			uint32 h = srcH;
			if (w > kMaxDim || h > kMaxDim)
			{
				const float sx = (float)kMaxDim / (float)w;
				const float sy = (float)kMaxDim / (float)h;
				const float s = std::min(sx, sy);
				w = std::max(8u, (uint32)floorf((float)w * s));
				h = std::max(8u, (uint32)floorf((float)h * s));
			}

			const int gw = (int)w;
			const int gh = (int)h;

			auto SampleAlpha01 = [&](int x, int y) -> uint8
			{
				// Map downsample grid -> source texel (nearest)
				const int sx = (int)((double)x * (double)(srcW - 1) / (double)std::max(1, gw - 1));
				const int sy = (int)((double)y * (double)(srcH - 1) / (double)std::max(1, gh - 1));
				return GetAlphaAt(sx, sy);
			};

			// Threshold: you can tune this per asset; 0.4~0.5 is typical for foliage cutout.
			const uint8 alphaThreshold = 128;

			std::vector<uint8> mask((size_t)gw * (size_t)gh, 0);
			for (int y = 0; y < gh; ++y)
			{
				for (int x = 0; x < gw; ++x)
				{
					const uint8 a = SampleAlpha01(x, y);
					mask[(size_t)y * (size_t)gw + (size_t)x] = (a >= alphaThreshold) ? 1 : 0;
				}
			}

			// Optional dilation (helps avoid "shaved" silhouettes due to AA / threshold).
			// 1 iteration is usually enough.
			{
				std::vector<uint8> tmp = mask;
				for (int y = 1; y < gh - 1; ++y)
				{
					for (int x = 1; x < gw - 1; ++x)
					{
						if (mask[(size_t)y * (size_t)gw + (size_t)x])
							continue;

						uint8 any = 0;
						for (int oy = -1; oy <= 1; ++oy)
							for (int ox = -1; ox <= 1; ++ox)
							{
								any |= mask[(size_t)(y + oy) * (size_t)gw + (size_t)(x + ox)];
							}
						tmp[(size_t)y * (size_t)gw + (size_t)x] = any ? 1 : 0;
					}
				}
				mask.swap(tmp);
			}

			// Marching squares segments in (grid*2) integer space.
			// Points are at half steps => multiply coordinates by 2 to keep integers.
			std::vector<Segment> segments;
			segments.reserve((size_t)(gw * gh));

			auto M = [&](int x, int y) -> uint8
			{
				return mask[(size_t)y * (size_t)gw + (size_t)x];
			};

			auto AddSeg = [&](int ax2, int ay2, int bx2, int by2)
			{
				Segment s;
				s.A = { ax2, ay2 };
				s.B = { bx2, by2 };
				segments.push_back(s);
			};

			auto EdgePoint = [&](int cellX, int cellY, int edge) -> IPoint
			{
				// cell is [x..x+1]x[y..y+1] in grid coords.
				// edge:
				// 0 = top (mid)
				// 1 = right
				// 2 = bottom
				// 3 = left
				switch (edge)
				{
				default:
				case 0: return { (cellX * 2) + 1, (cellY * 2) + 0 };
				case 1: return { (cellX * 2) + 2, (cellY * 2) + 1 };
				case 2: return { (cellX * 2) + 1, (cellY * 2) + 2 };
				case 3: return { (cellX * 2) + 0, (cellY * 2) + 1 };
				}
			};

			// Standard marching squares (filled=1) segment table: each case yields 0/1/2 segments (pairs of edges).
			// Ambiguous cases (5,10) resolved by center fill heuristic.
			for (int y = 0; y < gh - 1; ++y)
			{
				for (int x = 0; x < gw - 1; ++x)
				{
					const uint8 a = M(x, y);     // top-left
					const uint8 b = M(x + 1, y);     // top-right
					const uint8 c = M(x + 1, y + 1); // bottom-right
					const uint8 d = M(x, y + 1); // bottom-left

					const int code = (int)a | ((int)b << 1) | ((int)c << 2) | ((int)d << 3);
					if (code == 0 || code == 15)
						continue;

					const bool centerFilled = ((int)a + (int)b + (int)c + (int)d) >= 2;

					auto Emit = [&](int e0, int e1)
					{
						const IPoint p0 = EdgePoint(x, y, e0);
						const IPoint p1 = EdgePoint(x, y, e1);
						AddSeg(p0.x, p0.y, p1.x, p1.y);
					};

					switch (code)
					{
					case 1:  Emit(3, 0); break;
					case 2:  Emit(0, 1); break;
					case 3:  Emit(3, 1); break;
					case 4:  Emit(1, 2); break;
					case 5:
						if (centerFilled) { Emit(3, 2); Emit(0, 1); }
						else { Emit(3, 0); Emit(1, 2); }
						break;
					case 6:  Emit(0, 2); break;
					case 7:  Emit(3, 2); break;
					case 8:  Emit(2, 3); break;
					case 9:  Emit(0, 2); break;
					case 10:
						if (centerFilled) { Emit(0, 3); Emit(1, 2); }
						else { Emit(0, 1); Emit(2, 3); }
						break;
					case 11: Emit(1, 2); break;
					case 12: Emit(1, 3); break;
					case 13: Emit(0, 1); break;
					case 14: Emit(0, 3); break;
					default: break;
					}
				}
			}

			if (segments.empty())
				return false;

			// Build adjacency (each vertex degree should be 2 for clean loops)
			struct KeyHash
			{
				size_t operator()(const uint64 v) const noexcept { return std::hash<uint64>{}(v); }
			};
			auto Pack = [](const IPoint& p) -> uint64
			{
				return (uint64)(uint32)p.x | ((uint64)(uint32)p.y << 32);
			};

			std::unordered_map<uint64, std::vector<IPoint>, KeyHash> adj;
			adj.reserve(segments.size() * 2);

			for (const Segment& s : segments)
			{
				adj[Pack(s.A)].push_back(s.B);
				adj[Pack(s.B)].push_back(s.A);
			}

			// Trace loops
			std::unordered_set<uint64, KeyHash> usedEdge;
			usedEdge.reserve(segments.size() * 2);

			auto EdgeKey = [&](const IPoint& a, const IPoint& b) -> uint64
			{
				const uint64 ka = Pack(a);
				const uint64 kb = Pack(b);
				// order-independent
				return (ka < kb) ? (ka ^ (kb * 0x9E3779B185EBCA87ull)) : (kb ^ (ka * 0x9E3779B185EBCA87ull));
			};

			std::vector<Loop2D> loops;

			for (const Segment& s0 : segments)
			{
				const uint64 ek0 = EdgeKey(s0.A, s0.B);
				if (usedEdge.find(ek0) != usedEdge.end())
					continue;

				Loop2D loop;
				loop.P.reserve(256);

				IPoint start = s0.A;
				IPoint prev = s0.A;
				IPoint cur = s0.B;

				loop.P.push_back(start);
				loop.P.push_back(cur);

				usedEdge.insert(ek0);

				int guard = 0;
				while (guard++ < 100000)
				{
					auto it = adj.find(Pack(cur));
					if (it == adj.end() || it->second.empty())
						break;

					const std::vector<IPoint>& nbrs = it->second;

					// choose next != prev
					IPoint next = nbrs[0];
					if ((int)nbrs.size() > 1 && (next.x == prev.x && next.y == prev.y))
						next = nbrs[1];

					const uint64 ek = EdgeKey(cur, next);
					if (usedEdge.find(ek) != usedEdge.end())
					{
						// If we returned to start, close.
						if (next.x == start.x && next.y == start.y)
							break;

						// Otherwise try alternate neighbor if exists.
						if ((int)nbrs.size() > 1)
						{
							IPoint alt = nbrs[1];
							if (!(alt.x == prev.x && alt.y == prev.y))
							{
								const uint64 ekAlt = EdgeKey(cur, alt);
								if (usedEdge.find(ekAlt) == usedEdge.end())
								{
									next = alt;
								}
								else
								{
									break;
								}
							}
							else
							{
								break;
							}
						}
						else
						{
							break;
						}
					}

					usedEdge.insert(EdgeKey(cur, next));

					prev = cur;
					cur = next;

					if (cur.x == start.x && cur.y == start.y)
						break;

					loop.P.push_back(cur);
				}

				// Remove duplicate last==start if present
				if (!loop.P.empty() && loop.P.back().x == start.x && loop.P.back().y == start.y)
				{
					loop.P.pop_back();
				}

				if ((int)loop.P.size() >= 3)
					loops.push_back(std::move(loop));
			}

			if (loops.empty())
				return false;

			// Choose the largest loop (ignore holes / tiny islands).
			int bestIdx = 0;
			float bestAreaAbs = -1.0f;

			std::vector<std::vector<float2>> loopUVs;
			loopUVs.reserve(loops.size());

			for (int li = 0; li < (int)loops.size(); ++li)
			{
				const Loop2D& L = loops[li];

				std::vector<float2> uv;
				uv.reserve(L.P.size());

				for (const IPoint& q : L.P)
				{
					// q is in grid*2 units.
					const float u = (float)q.x / (float)(gw * 2);
					const float v = (float)q.y / (float)(gh * 2);
					uv.push_back(float2{ u, v });
				}

				// Close poly for simplify later as open chain
				// (RDP expects endpoints; we will make it cyclic by duplicating first at end)
				loopUVs.push_back(std::move(uv));

				const float a = SignedArea(loopUVs.back());
				const float absA = fabsf(a);
				if (absA > bestAreaAbs)
				{
					bestAreaAbs = absA;
					bestIdx = li;
				}
			}

			std::vector<float2> poly = loopUVs[bestIdx];
			if ((int)poly.size() < 3)
				return false;

			// Ensure CCW for triangulation.
			// (Area sign depends on v axis direction, but we just need consistency.)
			if (SignedArea(poly) < 0.0f)
				std::reverse(poly.begin(), poly.end());

			// Simplify (RDP) on an "open" version of the loop
			{
				std::vector<float2> open = poly;
				open.push_back(poly[0]); // close
				const float eps = std::max(1.0f / (float)gw, 1.0f / (float)gh) * 4.0f;
				std::vector<float2> simp = RdpSimplify(open, eps);
				if (!simp.empty() && (simp.back().x == simp.front().x && simp.back().y == simp.front().y))
					simp.pop_back();

				// Keep reasonable vertex count (foliage cutout sweet spot: 16~64)
				const int kMaxVerts = 12;
				if ((int)simp.size() > kMaxVerts)
				{
					// crude additional thinning
					std::vector<float2> th;
					th.reserve(kMaxVerts);
					for (int i = 0; i < (int)simp.size(); i += (int)std::ceil((float)simp.size() / (float)kMaxVerts))
						th.push_back(simp[i]);
					if ((int)th.size() >= 3)
						simp = std::move(th);
				}

				if ((int)simp.size() >= 3)
					poly = std::move(simp);

				if (SignedArea(poly) < 0.0f)
					std::reverse(poly.begin(), poly.end());
			}

			// Triangulate
			std::vector<uint16> triIdx;
			if (!EarClipTriangulate(poly, triIdx))
				return false;

			// Build vertices (pos from UV, same pivot/scale behavior as your quad)
			outVerts.clear();
			outVerts.reserve(poly.size());

			auto UVToPos = [&](const float2& uv) -> float3
			{
				const float x = (uv.x - pivot01.x) * scale.x;
				const float y = ((1.0f - uv.y) - pivot01.y) * scale.y; // v=0 top => y=top
				return float3{ x, y, 0.0f };
			};

			for (const float2& uv : poly)
			{
				BillboardVertex vtx;
				vtx.UV = uv;
				vtx.Pos = UVToPos(uv);
				outVerts.push_back(vtx);
			}

			outIdx = std::move(triIdx);
			if (outIdx.size() < 3 || outVerts.size() < 3)
				return false;

			return true;
		};

		// ---------------------------------------------------------------------
		// Create VB/IB (Cutout if possible, otherwise fallback to quad)
		// ---------------------------------------------------------------------
		std::vector<BillboardVertex> vertices;
		std::vector<uint16> indices;

		bool bBuiltCutout = BuildCutoutMeshFromAlpha(vertices, indices);

		if (!bBuiltCutout)
		{
			// Fallback to quad (original behavior)
			vertices.resize(4);

			const float x0 = -pivot01.x * scale.x;
			const float x1 = (1.0f - pivot01.x) * scale.x;

			const float y0 = -pivot01.y * scale.y;
			const float y1 = (1.0f - pivot01.y) * scale.y;

			//  3 ---- 2
			//  |      |
			//  0 ---- 1
			//
			// UVs: (0,0)=top-left, (1,1)=bottom-right (matches your original)
			vertices[0].Pos = float3{ x0, y0, 0.0f }; vertices[0].UV = float2{ 0.0f, 1.0f };
			vertices[1].Pos = float3{ x1, y0, 0.0f }; vertices[1].UV = float2{ 1.0f, 1.0f };
			vertices[2].Pos = float3{ x1, y1, 0.0f }; vertices[2].UV = float2{ 1.0f, 0.0f };
			vertices[3].Pos = float3{ x0, y1, 0.0f }; vertices[3].UV = float2{ 0.0f, 0.0f };

			indices =
			{
				0, 1, 2,
				0, 2, 3
			};
		}

		// Vertex buffer
		{
			BufferDesc vb = {};
			vb.Name = bBuiltCutout ? "Billboard.Cutout.VB" : "Billboard.Quad.VB";
			vb.Usage = USAGE_IMMUTABLE;
			vb.BindFlags = BIND_VERTEX_BUFFER;
			vb.Size = uint32(vertices.size() * sizeof(BillboardVertex));

			BufferData init = {};
			init.pData = vertices.data();
			init.DataSize = vb.Size;

			billboard.VertexBuffer = CreateVertexBuffer(vb, &init);
			ASSERT(billboard.VertexBuffer, "Create billboard VB failed.");
		}

		// Index buffer
		{
			BufferDesc ib = {};
			ib.Name = bBuiltCutout ? "Billboard.Cutout.IB" : "Billboard.Quad.IB";
			ib.Usage = USAGE_IMMUTABLE;
			ib.BindFlags = BIND_INDEX_BUFFER;
			ib.Size = uint32(indices.size() * sizeof(uint16));

			BufferData init = {};
			init.pData = indices.data();
			init.DataSize = ib.Size;

			billboard.IndexBuffer = CreateIndexBuffer(ib, &init);
			ASSERT(billboard.IndexBuffer, "CreateBillboardRenderData: Create billboard IB failed.");
		}

		billboard.VertexStride = sizeof(BillboardVertex);
		billboard.VertexCount = static_cast<uint32>(vertices.size());
		billboard.IndexCount = static_cast<uint32>(indices.size());
		billboard.IndexType = VT_UINT16;

		if (bBuiltCutout)
		{
			DumpBillboardMeshToOBJ(
				"C:/Dev/ShizenEngine/BillboardCutout.obj",
				vertices,
				indices);
		}

		static uint64 sBillboardId = 1;

		m_BillboardCache.Store(sBillboardId, std::move(billboard));
		return m_BillboardCache.Acquire(sBillboardId++);
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

		const Material& material = pMaterialManager->GetMaterial(materialId);

		// ---------------------------------------------------------------------
		// PSO acquire
		// ---------------------------------------------------------------------
		// TODO: Replace
		if (renderPassKey == STRING_HASH("Shadow"))
		{
			if (material.GetBlendMode() == MATERIAL_BLEND_MODE_OPAQUE)
			{
				out.pPSO = m_pShadowOpaquePSO;
			}
			else if (material.GetBlendMode() == MATERIAL_BLEND_MODE_MASKED)
			{
				out.pPSO = m_pShadowMaskedPSO;
			}
			else
			{
				ASSERT(false, "Not supported");
			}
		}

		ASSERT(renderPassKey != 0, "Render pass must be set in graphics pipeline.");
		ASSERT(m_PassTable.contains(renderPassKey), "Render pass not found.");

		GraphicsPipelineStateCreateInfo psoCI =
			material.BuildGraphicsPipelineStateCreateInfo(m_PassTable.at(renderPassKey).pRHIRenderpass);

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
				for (const RefCntAutoPtr<IShader>& shader : material.GetShaders())
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

	RefCntAutoPtr<IPipelineState> Renderer::AcquirePipelineStateFromMaterial(MaterialId materialId, uint64 renderPassKey) const
	{
		MaterialManager* pMaterialManager = MaterialManager::GetInstance();
		ASSERT(pMaterialManager->HasMaterial(materialId), "Material is not found.");

		const Material& material = pMaterialManager->GetMaterial(materialId);

		// TODO: Replace
		if (renderPassKey == STRING_HASH("Shadow"))
		{
			if (material.GetBlendMode() == MATERIAL_BLEND_MODE_OPAQUE)
			{
				return m_pShadowOpaquePSO;
			}
			else if (material.GetBlendMode() == MATERIAL_BLEND_MODE_MASKED)
			{
				return m_pShadowMaskedPSO;
			}
			else
			{
				ASSERT(false, "Not supported");
			}
		}

		RefCntAutoPtr<IPipelineState> pOutPipelineState;

		ASSERT(renderPassKey != 0, "Render pass must be set in graphics pipeline.");
		ASSERT(m_PassTable.contains(renderPassKey), "Render pass not found.");
		GraphicsPipelineStateCreateInfo psoCI = material.BuildGraphicsPipelineStateCreateInfo(m_PassTable.at(renderPassKey).pRHIRenderpass);
		pOutPipelineState = m_pPipelineStateManager->AcquireGraphics(psoCI);

		return pOutPipelineState;
	}

	RefCntAutoPtr<IShaderResourceBinding> Renderer::AcquireShaderResourceBindingFromMaterial(MaterialId materialId, IPipelineState* pso)
	{
		MaterialManager* pMaterialManager = MaterialManager::GetInstance();
		ASSERT(pMaterialManager->HasMaterial(materialId), "Material is not found.");

		const Material& material = pMaterialManager->GetMaterial(materialId);

		RefCntAutoPtr<IShaderResourceBinding> pOutSRB;

		// Create SRB
		pso->CreateShaderResourceBinding(&pOutSRB, true);
		ASSERT(pOutSRB, "Failed to create SRB.");

		// ---------------------------------------------------------------------
		// Constant Buffers (bind ALL reflected CBs)
		// ---------------------------------------------------------------------
		const uint32 cbCount = material.GetTemplate().GetCBufferCount();
		for (uint32 cbIndex = 0; cbIndex < cbCount; ++cbIndex)
		{
			const MaterialCBufferDesc& cb = material.GetTemplate().GetCBuffer(cbIndex);

			BufferDesc desc = {};
			desc.Name = cb.Name.c_str();
			desc.Usage = USAGE_DEFAULT;
			desc.BindFlags = BIND_UNIFORM_BUFFER;
			desc.CPUAccessFlags = CPU_ACCESS_NONE;
			desc.Size = cb.ByteSize;

			RefCntAutoPtr<IBuffer> pConstantBuffer;
			m_pDevice->CreateBuffer(desc, nullptr, &pConstantBuffer);
			ASSERT(pConstantBuffer, "Failed to create constant buffer: %s", cb.Name.c_str());

			// Bind by name for stages that expose it.
			for (const RefCntAutoPtr<IShader>& shader : material.GetShaders())
			{
				ASSERT(shader, "Shader in source instance is null.");

				const SHADER_TYPE st = shader->GetDesc().ShaderType;
				IShaderResourceVariable* var = pOutSRB->GetVariableByName(st, cb.Name.c_str());
				if (var)
				{
					var->Set(pConstantBuffer);
				}
			}

			// Upload initial blob
			const uint8* pBlob = material.GetCBufferBlobData(cbIndex);
			const uint32 blobSize = material.GetCBufferBlobSize(cbIndex);

			ASSERT(pBlob, "Invalid blob pointer. cb=%s", cb.Name.c_str());
			ASSERT(blobSize == cb.ByteSize, "Blob size mismatch. cb=%s blob=%u expected=%u", cb.Name.c_str(), blobSize, cb.ByteSize);
			ASSERT(blobSize <= pConstantBuffer->GetDesc().Size, "Blob size exceeds CB size. cb=%s", cb.Name.c_str());

			m_pImmediateContext->UpdateBuffer(
				pConstantBuffer,
				0,
				blobSize,
				pBlob,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION
			);

			pushBarrier(pConstantBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		}

		// ---------------------------------------------------------------------
		// Textures (bind by reflected resource list)
		// ---------------------------------------------------------------------
		const uint32 resCount = material.GetTemplate().GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const MaterialResourceDesc& resDesc = material.GetTemplate().GetResource(i);

			if (resDesc.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2D &&
				resDesc.Type != MATERIAL_RESOURCE_TYPE_TEXTURE2DARRAY &&
				resDesc.Type != MATERIAL_RESOURCE_TYPE_TEXTURECUBE)
			{
				continue;
			}

			RefCntAutoPtr<ITexture> pTexture;
			ITextureView* pView = nullptr;

			// Prefer simplified map lookup (MaterialTexture)
			if (const MaterialTexture* mt = material.GetTextureOrNull(resDesc.Name))
			{
				ASSERT(mt->Texture.IsValid(), "Material texture ref invalid: %s", resDesc.Name.c_str());
				pTexture = CreateTexture(mt->Texture);
				pView = pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
			}
			else
			{
				// fallback error
				pTexture = m_pRegistry->GetTexture(STRING_HASH("ErrorTex"));
				pView = pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
			}

			bool bSet = false;
			if (IShaderResourceVariable* var = pOutSRB->GetVariableByName(SHADER_TYPE_VERTEX, resDesc.Name.c_str()))
			{
				var->Set(pView);
				bSet = true;
			}
			if (IShaderResourceVariable* var = pOutSRB->GetVariableByName(SHADER_TYPE_PIXEL, resDesc.Name.c_str()))
			{
				var->Set(pView);
				bSet = true;
			}

			if (bSet)
			{
				ASSERT(pTexture, "");
				pushBarrier(pTexture, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
			}
		}

		return pOutSRB;
	}

	// ---------------------------------------------------------------------
	// Material templates
	// ---------------------------------------------------------------------

	MaterialTemplate& Renderer::CreateMaterialTemplate(const MaterialTemplateCreateInfo& createInfo)
	{
		ASSERT(createInfo.ShaderStages.size() >= 1, "At least one shader stage must be specified.");
		ASSERT(createInfo.TemplateName != "", "Material template name is empty.");

		const std::string& name = createInfo.TemplateName;
		auto it = m_TemplateLibrary.find(name);
		ASSERT(it == m_TemplateLibrary.end(), "Material template already exists: %s", name.c_str());

		MaterialTemplate matTemplate;
		bool bResult = matTemplate.Initialize(*this, createInfo);
		ASSERT(bResult, "Failed to create material template: %s", name.c_str());

		m_TemplateLibrary[name] = std::move(matTemplate);
		return m_TemplateLibrary[name];
	}

	MaterialTemplate& Renderer::CreateMaterialTemplate(const std::string& name, const std::string& vsPath, const std::string& psPath)
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

		return CreateMaterialTemplate(createInfo);
	}

	MaterialTemplate& Renderer::CreateMaterialTemplate(const std::string& name, const std::string& vsPath, const std::string& vsEntry, const std::string& psPath, const std::string& psEntry)
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

		return CreateMaterialTemplate(createInfo);
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

	void Renderer::SetShadowPipeline(RefCntAutoPtr<IPipelineState> pOpaquePSO, RefCntAutoPtr<IPipelineState> pMaskedPSO)
	{
		m_pShadowOpaquePSO = pOpaquePSO;
		m_pShadowMaskedPSO = pMaskedPSO;
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
		std::vector<uint64> passes;
		passes.reserve(m_PassTable.size());

		for (const auto& pair : m_PassTable)
		{
			uint64 passId = pair.first;
			ASSERT(passId != 0, "Invalid pass ID.");
			passes.push_back(passId);
		}

		ASSERT(!passes.empty(), "Requires at least one render pass.");

		const uint32 n = static_cast<uint32>(passes.size());

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
			const auto& accesses = m_PassTable.at(passes[i]).ResourceAccess;

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
			return m_PassTable.at(passes[passIndex]).Name.c_str();
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
					const auto& p = m_PassTable.at(passes[i]);
					std::cout << " - " << p.Name << " (indeg=" << indeg[i] << ")\n";
				}
			}

			std::cout << "\n[RenderGraph] edges among remaining:\n";
			for (uint32 u = 0; u < n; ++u)
			{
				if (indeg[u] == 0) continue;
				const auto& pu = m_PassTable.at(passes[u]);

				for (uint32 v : adj[u])
				{
					if (indeg[v] == 0) continue;
					const auto& pv = m_PassTable.at(passes[v]);
					std::cout << "   " << pu.Name << " -> " << pv.Name << "\n";
				}
			}

			ASSERT(false, "Compile failed (cycle).");
		}


		for (uint32 idx : order)
		{
			ASSERT(idx < n, "Topo order index out of bounds.");
			m_CompiledPassOrder.push_back(passes[idx]);
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


