#include "pch.h"
#include "Engine/Renderer/Public/Renderer.h"

#include "Engine/Core/Math/Math.h"
#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/GraphicsUtils/Public/GraphicsUtils.hpp"
#include "Engine/Image/Public/TextureUtilities.h"

#include "Engine/Renderer/Public/DrawPacket.h"
#include "Engine/RenderPass/Public/ShadowRenderPass.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

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

		// Build fixed templates + prepare cache map
		{
			auto makeTemplate = [&](MaterialTemplate& outTmpl, const char* name, const char* vs, const char* ps) -> bool
			{
				MaterialTemplateCreateInfo tci = {};
				tci.PipelineType = MATERIAL_PIPELINE_TYPE_GRAPHICS;
				tci.TemplateName = name;

				tci.ShaderStages.clear();
				tci.ShaderStages.reserve(2);

				MaterialShaderStageDesc sVS = {};
				sVS.ShaderType = SHADER_TYPE_VERTEX;
				sVS.DebugName = "VS";
				sVS.FilePath = vs;
				sVS.EntryPoint = "main";
				sVS.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				sVS.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
				sVS.UseCombinedTextureSamplers = false;

				MaterialShaderStageDesc sPS = {};
				sPS.ShaderType = SHADER_TYPE_PIXEL;
				sPS.DebugName = "PS";
				sPS.FilePath = ps;
				sPS.EntryPoint = "main";
				sPS.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				sPS.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
				sPS.UseCombinedTextureSamplers = false;

				tci.ShaderStages.push_back(sVS);
				tci.ShaderStages.push_back(sPS);

				return outTmpl.Initialize(m_pDevice, m_pShaderSourceFactory, tci);
			};

			MaterialTemplate gbufferTemplate;
			const bool ok0 = makeTemplate(gbufferTemplate, "DefaultLit", "GBuffer.vsh", "GBuffer.psh");
			ASSERT(ok0, "Build initial material templates failed.");

			m_TemplateLibrary[gbufferTemplate.GetName()] = gbufferTemplate;
			Material::RegisterTemplateLibrary(&m_TemplateLibrary);
		}

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

			RefCntAutoPtr<ITexture> errorTex = createTexture(desc, &initData);
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

				RefCntAutoPtr<IBuffer> sb = createBuffer(desc, nullptr);
				ASSERT(sb, "Object table create failed.");
				return sb;
			};

			AddBuffer(STRING_HASH("ObjectTable.GBuffer"), std::move(createObjectTable("ObjectTableSB.GBuffer")));
			AddBuffer(STRING_HASH("ObjectTable.Shadow"), std::move(createObjectTable("ObjectTableSB.Shadow")));

			m_pPipelineStateManager->RegisterStaticBufferSRV("g_ObjectTable", STRING_HASH("ObjectTable.GBuffer"));
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

			m_pRegistry->RegisterTexture(STRING_HASH("ShadowMap"), createTexture(td));

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

		m_NewBuffersThisFrame.clear();
		m_NewTexturesThisFrame.clear();

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
		IBuffer* pObjSB_Shadow = m_PassCtx.pRegistry->GetBuffer(STRING_HASH("ObjectTable.Shadow"));;

		ITexture* pEnvTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvTex"));
		ITexture* pEnvDiffTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvDiffuseTex"));
		ITexture* pEnvSpecTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvSpecularTex"));
		ITexture* pEnvBrdfTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("EnvBrdfTex"));
		ITexture* pErrorTex = m_PassCtx.pRegistry->GetTexture(STRING_HASH("ErrorTex"));

		ASSERT(pFrameCB, "FrameCB missing (registry).");
		ASSERT(pDrawCB, "DrawCB missing (registry).");
		ASSERT(pShadowCB, "ShadowCB missing (registry).");
		ASSERT(pObjSB_GB && pObjSB_Shadow, "ObjectTable SB missing (registry).");
		ASSERT(pEnvTex && pEnvDiffTex && pEnvSpecTex && pEnvBrdfTex, "Env textures missing (registry).");
		ASSERT(pErrorTex, "Error texture missing (registry).");

		m_PassCtx.pScene = &scene;
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
			const float ShadowVisibleDistance = 100.0f;

			// TODO: replace with actual shadow map size if you store it
			const float shadowMapWidth = 4096.0f;
			const float shadowMapHeight = 4096.0f;

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

			const float unitsPerTexelSqX = extent / shadowMapWidth;
			const float unitsPerTexelSqY = extent / shadowMapHeight;
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
		std::vector<StateTransitionDesc> preBarriers = {};
		auto pushBarrier = [&preBarriers](IDeviceObject* pObj, RESOURCE_STATE from, RESOURCE_STATE to)
		{
			ASSERT(pObj, "Device object is null.");

			StateTransitionDesc b = {};
			b.pResource = pObj;
			b.OldState = from;
			b.NewState = to;
			b.Flags = STATE_TRANSITION_FLAG_UPDATE_STATE;
			preBarriers.push_back(b);
		};

		pushBarrier(pFrameCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		pushBarrier(pShadowCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		pushBarrier(pDrawCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

		pushBarrier(pObjSB_GB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pObjSB_Shadow, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pEnvTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pEnvDiffTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pEnvSpecTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		pushBarrier(pEnvBrdfTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		pushBarrier(pErrorTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		// ------------------------------------------------------------
		// Visible objects: VB/IB + Material textures/CB barriers (dedup)
		// ------------------------------------------------------------
		MaterialManager* pMaterialManager = MaterialManager::GetInstance();

		auto hashCombine64 = [](uint64 h, uint64 v) {return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); };

		for (uint32 objDense : visibleObjectIndexMain)
		{
			const RenderScene::SceneObject& obj = scene.GetObjectByDenseIndex(objDense);
			ASSERT(obj.pMesh, "Invalid scene object.");

			ASSERT(obj.pMesh->VertexBuffer && obj.pMesh->IndexBuffer, "Buffer is null.");
			pushBarrier(obj.pMesh->VertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
			pushBarrier(obj.pMesh->IndexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

			for (const auto& section : obj.pMesh->Sections)
			{
				uint64 hash = hashCombine64(section.MaterialId, STRING_HASH("GBuffer"));
				auto it = m_PipelineBindingCache.find(hash);
				if (it == m_PipelineBindingCache.end())
				{
					PipelineBinding pb;
					pb.pPSO = acquirePipelineStateFromMaterial(section.MaterialId, STRING_HASH("GBuffer"));
					pb.pSRB = acquireShaderResourceBindingFromMaterial(section.MaterialId, pb.pPSO);
					m_PipelineBindingCache[hash] = pb;
				}
			}
		}

		for (uint32 objDense : visibleObjectIndexShadow)
		{
			const auto& obj = scene.GetObjectByDenseIndex(objDense);
			ASSERT(obj.pMesh, "Invalid scene object.");

			if (!obj.bCastShadow)
			{
				continue;
			}

			ASSERT(obj.pMesh->VertexBuffer && obj.pMesh->IndexBuffer, "Buffer is null.");
			pushBarrier(obj.pMesh->VertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
			pushBarrier(obj.pMesh->IndexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

			for (const auto& section : obj.pMesh->Sections)
			{
				uint64 hash = hashCombine64(section.MaterialId, STRING_HASH("Shadow"));
				auto it = m_PipelineBindingCache.find(hash);
				if (it == m_PipelineBindingCache.end())
				{
					PipelineBinding pb;
					pb.pPSO = acquirePipelineStateFromMaterial(section.MaterialId, STRING_HASH("Shadow"));
					pb.pSRB = acquireShaderResourceBindingFromMaterial(section.MaterialId, pb.pPSO);
					m_PipelineBindingCache[hash] = pb;
				}
			}
		}

		for (RefCntAutoPtr<IBuffer> newBuffer : m_NewBuffersThisFrame)
		{
			ASSERT(newBuffer, "Buffer is null.");
			pushBarrier(newBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		}
		for (RefCntAutoPtr<ITexture> newTexure : m_NewTexturesThisFrame)
		{
			ASSERT(newTexure, "Texture is null.");
			pushBarrier(newTexure, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		}


		if (!preBarriers.empty())
		{
			ctx->TransitionResourceStates(static_cast<uint32>(preBarriers.size()), preBarriers.data());
		}

		m_NewBuffersThisFrame.clear();
		m_NewTexturesThisFrame.clear();

		// Apply pending buffer updates
		for (const BufferUpdateDesc& bud : m_PendingBufferUpdates)
		{
			IBuffer* pBuf = m_pRegistry->GetBuffer(bud.ResourceId);
			ASSERT(pBuf, "Buffer not found for ResourceId=%llu", (unsigned long long)bud.ResourceId);

			const auto& desc = pBuf->GetDesc();
			ASSERT(desc.Size >= bud.Data.size(), "Update size exceeds buffer size.");

			// Diligent UpdateBuffer expects Uint32 size
			ASSERT(bud.Data.size() <= std::numeric_limits<uint32>::max(), "Update too large.");
			const uint32 updateSize = static_cast<uint32>(bud.Data.size());

			const bool bCpuWritableDynamic = (desc.Usage == USAGE_DYNAMIC) && ((desc.CPUAccessFlags & CPU_ACCESS_WRITE) != 0);

			if (bCpuWritableDynamic)
			{
				// Dynamic buffers must be updated via Map/Unmap, not UpdateBuffer()
				void* pData = nullptr;
				ctx->MapBuffer(pBuf, MAP_WRITE, MAP_FLAG_DISCARD, pData);
				ASSERT(pData, "MapBuffer returned null.");
				std::memcpy(pData, bud.Data.data(), bud.Data.size());
				ctx->UnmapBuffer(pBuf, MAP_WRITE);
			}
			else
			{
				// Default/Sparse buffers can be updated via UpdateBuffer()
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
		// Helper: build packets from draw items
		// ------------------------------------------------------------
		auto buildPacketsFromDrawItems = [&](uint64 passKey, const std::vector<RenderScene::DrawItem>& items) -> std::vector<DrawPacket>
		{
			std::vector<DrawPacket> out;
			out.reserve(items.size());

			for (const RenderScene::DrawItem& di : items)
			{
				RenderScene::BatchView bv = {};
				bool ok = scene.TryGetBatchView(di.BatchId, bv);
				ASSERT(ok, "Invalid batch id.");

				const StaticMeshRenderData* mesh = bv.pMesh;
				ASSERT(mesh, "Batch mesh is null.");
				ASSERT(bv.SectionIndex < static_cast<uint32>(mesh->Sections.size()), "SectionIndex OOB.");

				const auto& sec = mesh->Sections[bv.SectionIndex];

				DrawPacket pkt = {};
				pkt.VertexBuffer = mesh->VertexBuffer;
				pkt.IndexBuffer = mesh->IndexBuffer;

				pkt.DrawCallType = EDrawCallType::Direct;

				pkt.DrawAttribs = {};
				pkt.DrawAttribs.IndexType = mesh->IndexType;
				pkt.DrawAttribs.NumIndices = sec.IndexCount;
				pkt.DrawAttribs.FirstIndexLocation = sec.FirstIndex;
				pkt.DrawAttribs.BaseVertex = static_cast<int32>(sec.BaseVertex);
				pkt.DrawAttribs.NumInstances = di.InstanceCount;
				pkt.DrawAttribs.FirstInstanceLocation = di.StartInstanceLocation;
				pkt.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

				uint64 pbHash = hashCombine64(sec.MaterialId, passKey);
				ASSERT(m_PipelineBindingCache.contains(pbHash), "Cache not found.");
				const PipelineBinding& pb = m_PipelineBindingCache[pbHash];
				pkt.PSO = pb.pPSO;
				pkt.SRB = pb.pSRB;

				out.push_back(pkt);
			}

			return out;
		};

		// ------------------------------------------------------------
		// Build draw lists + pack object tables + build packets
		// ------------------------------------------------------------
		std::vector<RenderScene::DrawItem> drawItems;
		std::vector<uint32> instanceRemap;

		// GBuffer
		scene.BuildDrawList(STRING_HASH("GBuffer"), visibleObjectIndexMain, drawItems, instanceRemap);
		packObjectTableFromRemap(pObjSB_GB, instanceRemap);
		m_PassCtx.MainDrawPackets = buildPacketsFromDrawItems(STRING_HASH("GBuffer"), drawItems);

		// Shadow
		scene.BuildDrawList(STRING_HASH("Shadow"), visibleObjectIndexShadow, drawItems, instanceRemap);
		packObjectTableFromRemap(pObjSB_Shadow, instanceRemap);
		m_PassCtx.ShadowDrawPackets = buildPacketsFromDrawItems(STRING_HASH("Shadow"), drawItems);

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

			if (pass.pRHIRenderpass)
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

				RenderPassAttachmentDesc at = {};
				at.Format = vd.Format;          // ✅ View format
				at.SampleCount = td.SampleCount;
				at.LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
				at.StoreOp = ATTACHMENT_STORE_OP_STORE;
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
				bWrite &&
				a.Usage == RENDER_USAGE_DSV_WRITE &&
				a.TextureViewType == TEXTURE_VIEW_DEPTH_STENCIL)
			{
				ASSERT(!bHasDepth, "Multiple depth attachments are not supported yet.");

				ITexture* pTex = m_pRegistry->GetTexture(a.ResourceId);
				ASSERT(pTex, "DSV texture not found.");

				ITextureView* pDSV = m_pRegistry->GetTextureDSV(a.ResourceId);
				ASSERT(pDSV, "DSV view not found.");

				const TextureDesc& td = pTex->GetDesc();
				const TextureViewDesc& vd = pDSV->GetDesc();

				RenderPassAttachmentDesc at = {};
				at.Format = vd.Format;         // ✅ View format
				at.SampleCount = td.SampleCount;
				at.LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
				at.StoreOp = ATTACHMENT_STORE_OP_STORE;
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
			// - RenderPass는 만들 수 있음 (format은 swapchain desc)
			// - Framebuffer는 "현재 backbuffer RTV"가 필요하므로 런타임에 생성/갱신
			// -------------------------------------------------------------
			else if (a.Kind == RENDER_RESOURCE_KIND_EXTERNAL &&
				bWrite &&
				a.ResourceId == STRING_HASH("SwapChain.BackBuffer") &&
				a.Usage == RENDER_USAGE_RTV)
			{
				ASSERT(m_pSwapChain, "SwapChain is null.");

				const SwapChainDesc& scDesc = m_pSwapChain->GetDesc();

				RenderPassAttachmentDesc at = {};
				at.Format = scDesc.ColorBufferFormat;
				at.SampleCount = 1;
				at.LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
				at.StoreOp = ATTACHMENT_STORE_OP_STORE;
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
				m_pPresentRenderPass = rpItem.pRHIRenderpass;
			}
		}
		else
		{
			// compute-only / no RP
			rpItem.ClearValues.clear();
			rpItem.StaticFBAttachments.clear();
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
	// Resource registry wrappers
	// ---------------------------------------------------------------------

	uint64 Renderer::AddTexture(const std::string& name, const TextureDesc& desc, const TextureData* pInitData)
	{
		ASSERT(!name.empty(), "Name is empty.");
		return AddTexture(STRING_HASH(name), desc, pInitData);
	}

	uint64 Renderer::AddTexture(uint64 id, const TextureDesc& desc, const TextureData* pInitData)
	{
		ASSERT(m_pRegistry, "Registry is null.");
		RefCntAutoPtr<ITexture> tex = createTexture(desc, pInitData);
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
		RefCntAutoPtr<IBuffer> buf = createBuffer(desc, pInitData);
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

		m_StaticMeshCache.Store(key, std::move(out));
		return m_StaticMeshCache.Acquire(key);
	}

	RefCntAutoPtr<ITexture> Renderer::CreateTextureRenderDataFromHeightField(const TerrainHeightField& terrain)
	{
		RefCntAutoPtr<ITexture> out;

		const uint32 width = terrain.GetWidth();
		const uint32 height = terrain.GetHeight();

		const std::vector<uint16>& dataU16 = terrain.GetDataU16();
		ASSERT(!dataU16.empty(), "TerrainHeightField data is empty.");
		ASSERT(uint64(dataU16.size()) == uint64(width) * uint64(height), "TerrainHeightField data size mismatch.");

		// ---------------------------------------------------------------------
		// Create R16_UNORM texture with initial data
		// ---------------------------------------------------------------------
		TextureDesc desc = {};
		desc.Name = "HeightField R16_UNORM";
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;

		// Height map: 16-bit normalized [0..1] -> shader reads float
		desc.Format = TEX_FORMAT_R16_UNORM;

		desc.Usage = USAGE_DEFAULT;
		desc.BindFlags = BIND_SHADER_RESOURCE;

		TextureSubResData sr = {};
		sr.pData = dataU16.data();
		sr.Stride = width * sizeof(uint16); // row pitch (tightly packed)
		sr.DepthStride = 0;

		TextureData initData = {};
		initData.pSubResources = &sr;
		initData.NumSubresources = 1;

		m_pDevice->CreateTexture(desc, &initData, &out);
		ASSERT(out, "CreateTexture(HeightField) failed.");

		return out;
	}

	void Renderer::CreateShader(ShaderCreateInfo& sci, IShader** ppOutShader)
	{
		// TODO: 중복 생성 제거
		sci.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		sci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

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

	// ---------------------------------------------------------------------
	// Material templates
	// ---------------------------------------------------------------------

	const MaterialTemplate& Renderer::GetMaterialTemplate(const std::string& name) const
	{
		auto it = m_TemplateLibrary.find(name);
		ASSERT(it != m_TemplateLibrary.end(), "Material template not found: %s", name.c_str());
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
	// Resource wrappers
	// ---------------------------------------------------------------------

	RefCntAutoPtr<ITexture> Renderer::createTexture(const TextureDesc& desc, const TextureData* pInitData)
	{
		ASSERT(m_pDevice, "Device is null.");
		RefCntAutoPtr<ITexture> tex;
		m_pDevice->CreateTexture(desc, pInitData, &tex);
		return tex;
	}

	RefCntAutoPtr<ITexture> Renderer::createTexture(const AssetRef<Texture>& assetRef)
	{
		uint64 key = std::hash<AssetID>{}(assetRef.GetID());
		if (m_pRegistry->HasTexture(key))
		{
			return m_pRegistry->GetTexture(key);
		}

		AssetPtr<Texture> assetPtr = m_pAssetManager->Acquire(assetRef);
		ASSERT(assetPtr, "Failed to acquire TextureAsset.");

		return createTexture(key, *assetPtr);
	}

	RefCntAutoPtr<ITexture> Renderer::createTexture(const std::string& name, const Texture& texture)
	{
		return createTexture(STRING_HASH(name), texture);
	}

	RefCntAutoPtr<ITexture> Renderer::createTexture(uint64 id, const Texture& texture)
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

		m_pRegistry->RegisterTexture(id, createTexture(desc, &initData));
		return m_pRegistry->GetTexture(id);
	}

	RefCntAutoPtr<IBuffer> Renderer::createBuffer(const BufferDesc& desc, const BufferData* pInitData)
	{
		ASSERT(m_pDevice, "Device is null.");
		RefCntAutoPtr<IBuffer> buf;
		m_pDevice->CreateBuffer(desc, pInitData, &buf);
		return buf;
	}

	void Renderer::updateTexture2D(
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

	RefCntAutoPtr<IPipelineState> Renderer::acquirePipelineStateFromMaterial(MaterialId id, uint64 renderPassKey) const
	{
		MaterialManager* pMaterialManager = MaterialManager::GetInstance();
		ASSERT(pMaterialManager->HasMaterial(id), "Material is not found.");

		const Material& material = pMaterialManager->GetMaterial(id);

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

		const MATERIAL_PIPELINE_TYPE pipelineType = material.GetPipelineType();
		if (pipelineType == MATERIAL_PIPELINE_TYPE_GRAPHICS)
		{
			ASSERT(renderPassKey != 0, "Render pass must be set in graphics pipeline.");
			ASSERT(m_PassTable.contains(renderPassKey), "Render pass not found.");
			GraphicsPipelineStateCreateInfo psoCI = material.BuildGraphicsPipelineStateCreateInfo(m_PassTable.at(renderPassKey).pRHIRenderpass);
			pOutPipelineState = m_pPipelineStateManager->AcquireGraphics(psoCI);
		}
		else if (pipelineType == MATERIAL_PIPELINE_TYPE_COMPUTE)
		{
			ASSERT(renderPassKey == 0, "Render pass must be null in compute pipeline.");
			ComputePipelineStateCreateInfo psoCI = material.BuildComputePipelineStateCreateInfo();
			pOutPipelineState = m_pPipelineStateManager->AcquireCompute(psoCI);
		}
		else
		{
			ASSERT(false, "Unsupported pipeline type.");
		}

		SHADER_TYPE supportedShaderTypes[] =
		{
			SHADER_TYPE_VERTEX,
			SHADER_TYPE_PIXEL,
			SHADER_TYPE_COMPUTE,
		};

		return pOutPipelineState;
	}

	RefCntAutoPtr<IShaderResourceBinding> Renderer::acquireShaderResourceBindingFromMaterial(MaterialId id, IPipelineState* pso)
	{
		MaterialManager* pMaterialManager = MaterialManager::GetInstance();
		ASSERT(pMaterialManager->HasMaterial(id), "Material is not found.");

		const Material& material = pMaterialManager->GetMaterial(id);

		RefCntAutoPtr<IShaderResourceBinding> pOutSRB;

		// Create SRB
		pso->CreateShaderResourceBinding(&pOutSRB, true);
		ASSERT(pOutSRB, "Failed to create SRB.");

		// Initialize and bind resources
		uint32 cbIndex = 0;
		uint32 cbCount = material.GetTemplate().GetCBufferCount();
		for (; cbIndex < cbCount; ++cbIndex)
		{
			const auto& cb = material.GetTemplate().GetCBuffer(cbIndex);
			if (cb.Name == MaterialTemplate::MATERIAL_CBUFFER_NAME)
			{
				break;
			}
		}

		if (cbIndex < cbCount)
		{
			const MaterialCBufferDesc& cb = material.GetTemplate().GetCBuffer(cbIndex);

			BufferDesc desc = {};
			desc.Name = "MaterialConstants";
			desc.Usage = USAGE_DEFAULT;
			desc.BindFlags = BIND_UNIFORM_BUFFER;
			desc.CPUAccessFlags = CPU_ACCESS_NONE;
			desc.Size = cb.ByteSize;

			RefCntAutoPtr<IBuffer> pConstantBuffer;
			m_pDevice->CreateBuffer(desc, nullptr, &pConstantBuffer);

			// Bind by name for first stage that exposes it.
			for (const RefCntAutoPtr<IShader>& shader : material.GetShaders())
			{
				ASSERT(shader, "Shader in source instance is null.");

				const SHADER_TYPE st = shader->GetDesc().ShaderType;
				IShaderResourceVariable* var = pOutSRB->GetVariableByName(st, MaterialTemplate::MATERIAL_CBUFFER_NAME);
				if (var)
				{
					var->Set(pConstantBuffer);
				}
			}

			// Immediate initial binding
			const uint8* pBlob = material.GetCBufferBlobData(cbIndex);
			const uint32 blobSize = material.GetCBufferBlobSize(cbIndex);
			ASSERT(pBlob && blobSize > 0, "Invalid blob data.");
			ASSERT(blobSize <= pConstantBuffer->GetDesc().Size, "Blob size exceeds CB size.");

			m_pImmediateContext->UpdateBuffer(
				pConstantBuffer,
				0,
				blobSize,
				pBlob,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION
			);

			m_NewBuffersThisFrame.emplace(pConstantBuffer);
		}

		// Textures
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

			const MaterialTextureBinding& b = material.GetTextureBinding(i);

			RefCntAutoPtr<ITexture> pTexture;
			ITextureView* pView = nullptr;

			if (b.TextureRef.has_value())
			{
				pTexture = createTexture(b.TextureRef.value());
				pView = pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
			}
			else
			{
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
				m_NewTexturesThisFrame.emplace(pTexture);
			}
		}

		return pOutSRB;
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
			return m_pRegistry->GetTexture(a.ResourceId);
		}
		else if (a.Kind == RENDER_RESOURCE_KIND_BUFFER)
		{
			return m_pRegistry->GetBuffer(a.ResourceId);
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

		// ------------------------------------------------------------
		// Baseline deterministic ordering (even when graph has no edges)
		// ------------------------------------------------------------
		auto getStageHint = [](const char* name) -> int32
		{
			// Lower comes earlier.
			// Tune these buckets as your renderer grows.
			if (!name) return 1000;

			// Shadow / depth-pre
			if (std::strstr(name, "Shadow"))   return 100;
			if (std::strstr(name, "Depth"))    return 150;

			// GBuffer / prepass
			if (std::strstr(name, "GBuffer"))  return 200;

			// Deferred lighting
			if (std::strstr(name, "Deferred")) return 300;
			if (std::strstr(name, "Light"))    return 320; // "Lighting", etc.

			// Forward/transparent
			if (std::strstr(name, "Forward"))  return 400;
			if (std::strstr(name, "Trans"))    return 420;

			// Post
			if (std::strstr(name, "Post"))     return 500;
			if (std::strstr(name, "Tonemap"))  return 520;

			return 1000;
		};

		std::sort(passes.begin(), passes.end(),
			[&](uint64 a, uint64 b)
			{
				const RenderPassItem& passA = m_PassTable.at(a);
				const RenderPassItem& passB = m_PassTable.at(b);
				const int32 sa = getStageHint(a ? passA.Name.c_str() : "");
				const int32 sb = getStageHint(b ? passB.Name.c_str() : "");

				if (sa != sb) return sa < sb;

				// Stable within stage: lexicographic name
				const char* na = a ? passA.Name.c_str() : "";
				const char* nb = b ? passB.Name.c_str() : "";
				return std::strcmp(na, nb) < 0;
			});

		const uint32 n = static_cast<uint32>(passes.size());

		// Access classification
		auto isWrite = [](const RenderPassResourceAccess& a) -> bool
		{
			if (a.Access == RENDER_ACCESS_WRITE || a.Access == RENDER_ACCESS_READWRITE) return true;
			if (a.Usage == RENDER_USAGE_RTV || a.Usage == RENDER_USAGE_DSV_WRITE || a.Usage == RENDER_USAGE_UAV) return true;
			return false;
		};

		auto isRead = [](const RenderPassResourceAccess& a) -> bool
		{
			if (a.Access == RENDER_ACCESS_READ || a.Access == RENDER_ACCESS_READWRITE) return true;
			if (a.Usage == RENDER_USAGE_SRV || a.Usage == RENDER_USAGE_CBV || a.Usage == RENDER_USAGE_DSV_READ) return true;
			if (a.Usage == RENDER_USAGE_INDIRECT_ARGUMENT) return true;
			return false;
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
		ASSERT(order.size() == n, "Compile failed.");

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
			ASSERT(pObj, "Resolve device object failed for ResourceId=%llu", (unsigned long long)a.ResourceId);

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


