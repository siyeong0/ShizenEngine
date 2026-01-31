#include "pch.h"
#include "Engine/Renderer/Public/Renderer.h"

#include <unordered_set>

#include "Engine/Core/Math/Math.h"
#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/GraphicsUtils/Public/GraphicsUtils.hpp"
#include "Engine/Image/Public/TextureUtilities.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/RenderPass/Public/ShadowRenderPass.h"
#include "Engine/RenderPass/Public/GBufferRenderPass.h"
#include "Engine/RenderPass/Public/LightingRenderPass.h"
#include "Engine/RenderPass/Public/PostRenderPass.h"
#include "Engine/RenderPass/Public/GrassRenderPass.h"
#include "Engine/RenderPass/Public/DrawPacket.h"

#include "Engine/Renderer/Public/CommonResourceId.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	static const uint64 kPassGBuffer = STRING_HASH("GBuffer");
	static const uint64 kPassGrass = STRING_HASH("Grass");
	static const uint64 kPassShadow = STRING_HASH("Shadow");

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

	RefCntAutoPtr<IBuffer> Renderer::CreateBuffer(const BufferDesc& desc, const BufferData* pInitData)
	{
		ASSERT(m_pDevice, "Device is null.");
		RefCntAutoPtr<IBuffer> buf;
		m_pDevice->CreateBuffer(desc, pInitData, &buf);
		return buf;
	}

	void Renderer::UpdateBuffer(
		IDeviceContext* pCtx,
		IBuffer* pBuffer,
		uint32 offsetBytes,
		uint32 sizeBytes,
		const void* pData,
		RESOURCE_STATE_TRANSITION_MODE transitionMode) const
	{
		ASSERT(pCtx, "Context is null.");
		ASSERT(pBuffer, "Buffer is null.");
		ASSERT(pData || sizeBytes == 0, "UpdateBuffer: data is null.");

		pCtx->UpdateBuffer(
			pBuffer,
			offsetBytes,
			sizeBytes,
			pData,
			transitionMode
		);
	}

	void Renderer::UpdateTexture2D(
		IDeviceContext* pCtx,
		ITexture* pTexture,
		uint32 mipLevel,
		uint32 arraySlice,
		const TextureSubResData& subRes,
		RESOURCE_STATE_TRANSITION_MODE transitionMode) const
	{
		ASSERT(pCtx, "Context is null.");
		ASSERT(pTexture, "Texture is null.");

		// Update entire subresource (no box)
		IBox box = {}; // empty -> full resource
		pCtx->UpdateTexture(
			pTexture,
			mipLevel,
			arraySlice,
			box,
			subRes,
			transitionMode,
			RESOURCE_STATE_TRANSITION_MODE_NONE
		);
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
		m_pPipelineStateManager->Initialize(m_pDevice);

		// -----------------------------------------------------------------
		// Create error texture -> register to registry
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

			m_pRegistry->RegisterTexture(kRes_ErrorTex, std::move(errorTex));
		}

		// -----------------------------------------------------------------
		// Create shared buffers -> register to registry
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

			m_pRegistry->RegisterBuffer(kRes_FrameCB, std::move(frameCB));
			m_pRegistry->RegisterBuffer(kRes_DrawCB, std::move(drawCB));
			m_pRegistry->RegisterBuffer(kRes_ShadowCB, std::move(shadowCB));

			// ObjectIndexInstanceVB
			{
				BufferDesc desc = {};
				desc.Name = "ObjectIndexInstanceVB";
				desc.Usage = USAGE_DYNAMIC;
				desc.BindFlags = BIND_VERTEX_BUFFER;
				desc.CPUAccessFlags = CPU_ACCESS_WRITE;
				desc.Size = sizeof(uint32);

				RefCntAutoPtr<IBuffer> vb = CreateBuffer(desc, nullptr);
				ASSERT(vb, "Object index VB create failed.");

				m_pRegistry->RegisterBuffer(kRes_ObjectIndexVB, std::move(vb));
			}

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

			m_pRegistry->RegisterBuffer(kRes_ObjectTable_GB, std::move(createObjectTable("ObjectTableSB.GBuffer")));
			m_pRegistry->RegisterBuffer(kRes_ObjectTable_GR, std::move(createObjectTable("ObjectTableSB.Grass")));
			m_pRegistry->RegisterBuffer(kRes_ObjectTable_SH, std::move(createObjectTable("ObjectTableSB.Shadow")));
		}

		// -----------------------------------------------------------------
		// Create env textures -> register to registry
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

			m_pRegistry->RegisterTexture(kRes_EnvTex, std::move(env));
			m_pRegistry->RegisterTexture(kRes_EnvDiffuseTex, std::move(diff));
			m_pRegistry->RegisterTexture(kRes_EnvSpecularTex, std::move(spec));
			m_pRegistry->RegisterTexture(kRes_EnvBrdfTex, std::move(brdf));
		}

		// -----------------------------------------------------------------
		// Setup binders (now fetch buffers from registry)
		// -----------------------------------------------------------------
		{
			IBuffer* pFrameCB = m_pRegistry->GetBuffer(kRes_FrameCB);
			IBuffer* pDrawCB = m_pRegistry->GetBuffer(kRes_DrawCB);

			IBuffer* pSB_GB = m_pRegistry->GetBuffer(kRes_ObjectTable_GB);
			IBuffer* pSB_GR = m_pRegistry->GetBuffer(kRes_ObjectTable_GR);
			IBuffer* pSB_SH = m_pRegistry->GetBuffer(kRes_ObjectTable_SH);
			ASSERT(pFrameCB && pDrawCB, "Common CB invalid.");
			ASSERT(pSB_GB && pSB_GR && pSB_SH, "Object tables invalid.");
		}

		// -----------------------------------------------------------------
		// Fill PassContext from registry (shared resources)
		// -----------------------------------------------------------------
		m_PassCtx = {};
		m_PassCtx.pDevice = m_pDevice.RawPtr();
		m_PassCtx.pImmediateContext = m_pImmediateContext.RawPtr();
		m_PassCtx.pSwapChain = m_pSwapChain.RawPtr();
		m_PassCtx.pShaderSourceFactory = m_pShaderSourceFactory.RawPtr();
		m_PassCtx.pAssetManager = m_pAssetManager;
		m_PassCtx.pPipelineStateManager = m_pPipelineStateManager.get();
		m_PassCtx.pRegistry = m_pRegistry.get();

		m_PassCtx.BackBufferWidth = m_Width;
		m_PassCtx.BackBufferHeight = m_Height;

		// -----------------------------------------------------------------
		// Create common resources for passes
		// -----------------------------------------------------------------
		{
			// Shadow map
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

			}

			// GBuffer textures
			static constexpr uint32 NUM_GBUFFERS = 4;
			{
				auto createGBufferTexture = [&](uint32 w, uint32 h, TEXTURE_FORMAT fmt, const char* name) -> RefCntAutoPtr<ITexture>
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
						return CreateTexture(td);
					};
				m_pRegistry->RegisterTexture(STRING_HASH("GBuffer0_Albedo"), createGBufferTexture(m_Width, m_Height, TEX_FORMAT_RGBA8_UNORM, "GBuffer0_Albedo"));
				m_pRegistry->RegisterTexture(STRING_HASH("GBuffer1_Normal"), createGBufferTexture(m_Width, m_Height, TEX_FORMAT_RGBA16_FLOAT, "GBuffer1_Normal"));
				m_pRegistry->RegisterTexture(STRING_HASH("GBuffer2_MRAO"), createGBufferTexture(m_Width, m_Height, TEX_FORMAT_RGBA8_UNORM, "GBuffer2_MRAO"));
				m_pRegistry->RegisterTexture(STRING_HASH("GBuffer3_Emissive"), createGBufferTexture(m_Width, m_Height, TEX_FORMAT_RGBA16_FLOAT, "GBuffer3_Emissive"));

				TextureDesc td = {};
				td.Name = "GBufferDepth";
				td.Type = RESOURCE_DIM_TEX_2D;
				td.Width = m_Width;
				td.Height = m_Height;
				td.MipLevels = 1;
				td.SampleCount = 1;
				td.Usage = USAGE_DEFAULT;
				td.Format = TEX_FORMAT_R32_TYPELESS;
				td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

				m_pRegistry->RegisterTexture(STRING_HASH("GBufferDepth"), CreateTexture(td));

				TextureViewDesc vd = {};
				vd.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
				vd.Format = TEX_FORMAT_D32_FLOAT;

				m_pRegistry->CreateTextureView(STRING_HASH("GBufferDepth"), vd);

				vd = {};
				vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
				vd.Format = TEX_FORMAT_R32_FLOAT;
				m_pRegistry->CreateTextureView(STRING_HASH("GBufferDepth"), vd);
			}

			// Lighting
			{
				TextureDesc td = {};
				td.Name = "Lighting";
				td.Type = RESOURCE_DIM_TEX_2D;
				td.Width = m_Width;
				td.Height = m_Height;
				td.MipLevels = 1;
				td.Format = m_pSwapChain->GetDesc().ColorBufferFormat;
				td.SampleCount = 1;
				td.Usage = USAGE_DEFAULT;
				td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

				m_pRegistry->RegisterTexture(STRING_HASH("Lighting"), CreateTexture(td));
			}
		}

		// -----------------------------------------------------------------
		// Create render passes
		// -----------------------------------------------------------------
		{
			ASSERT(m_Passes.empty(), "m_Passes are already initilaized.");
			ASSERT(m_PassOrder.empty(), "m_PassOrder are already initilaized.");

			addPass(std::make_unique<ShadowRenderPass>(m_PassCtx));
			addPass(std::make_unique<GBufferRenderPass>(m_PassCtx));
			addPass(std::make_unique<LightingRenderPass>(m_PassCtx));

			addPass(std::make_unique<GrassRenderPass>(m_PassCtx));
			addPass(std::make_unique<PostRenderPass>(m_PassCtx));

			AssetRef<StaticMesh> grassRef = m_pAssetManager->RegisterAsset<StaticMesh>("C:/Dev/ShizenEngine/Assets/Exported/GrassBlade.shzmesh.json");
			AssetPtr<StaticMesh> grassPtr = m_pAssetManager->LoadBlocking<StaticMesh>(grassRef);
			ASSERT(grassPtr&& grassPtr->IsValid(), "Failed to load grass mesh.");

			grassPtr->RecomputeBounds();
			const Box& b = grassPtr->GetBounds();
			float yScale01 = 1.0f / (b.Max.y - b.Min.y);
			grassPtr->ApplyUniformScale(yScale01);
			grassPtr->MoveBottomToOrigin(true);

			const StaticMeshRenderData* grassRenderData = &CreateStaticMeshRenderData(*grassPtr);
			static_cast<GrassRenderPass*>(m_Passes["Grass"].get())->SetGrassModel(m_PassCtx, *grassRenderData);

			AssetRef<Texture> perlinRef = m_pAssetManager->RegisterAsset<Texture>("C:/Dev/ShizenEngine/Assets/Terrain/RollingHills/Worley.jpg");
			AssetPtr<Texture> perlinPtr = m_pAssetManager->LoadBlocking(perlinRef);

			Texture perlin = Texture::ConvertGrayScale(*perlinPtr);
			const TextureRenderData* grassDensityFieldTex = &CreateTextureRenderData(perlin);
			static_cast<GrassRenderPass*>(m_Passes["Grass"].get())->SetGrassDensityField(m_PassCtx, *grassDensityFieldTex);
		}
		return true;
	}

	void Renderer::Cleanup()
	{
		ReleaseSwapChainBuffers();

		m_Passes.clear();
		m_PassOrder.clear();
		m_RHIRenderPasses.clear();

		m_TextureCache.Clear();
		m_StaticMeshCache.Clear();
		m_MaterialCache.Clear();

		if (m_pPipelineStateManager)
		{
			m_pPipelineStateManager->Clear();
			m_pPipelineStateManager.reset();
		}

		m_pShaderSourceFactory.Release();
		m_pAssetManager = nullptr;

		m_pRegistry->Shutdown();

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
		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->BeginFrame(m_PassCtx);
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
		IBuffer* pFrameCB = m_PassCtx.pRegistry->GetBuffer(kRes_FrameCB);
		IBuffer* pDrawCB = m_PassCtx.pRegistry->GetBuffer(kRes_DrawCB);
		IBuffer* pShadowCB = m_PassCtx.pRegistry->GetBuffer(kRes_ShadowCB);

		IBuffer* pObjSB_GB = m_PassCtx.pRegistry->GetBuffer(kRes_ObjectTable_GB);
		IBuffer* pObjSB_Grass = m_PassCtx.pRegistry->GetBuffer(kRes_ObjectTable_GR);
		IBuffer* pObjSB_Shadow = m_PassCtx.pRegistry->GetBuffer(kRes_ObjectTable_SH);
		IBuffer* pObjIndexVB = m_PassCtx.pRegistry->GetBuffer(kRes_ObjectIndexVB);

		ITexture* pEnvTex = m_PassCtx.pRegistry->GetTexture(kRes_EnvTex);
		ITexture* pEnvDiffTex = m_PassCtx.pRegistry->GetTexture(kRes_EnvDiffuseTex);
		ITexture* pEnvSpecTex = m_PassCtx.pRegistry->GetTexture(kRes_EnvSpecularTex);
		ITexture* pEnvBrdfTex = m_PassCtx.pRegistry->GetTexture(kRes_EnvBrdfTex);
		ITexture* pErrorTex = m_PassCtx.pRegistry->GetTexture(kRes_ErrorTex);

		ASSERT(pFrameCB, "FrameCB missing (registry).");
		ASSERT(pDrawCB, "DrawCB missing (registry).");
		ASSERT(pShadowCB, "ShadowCB missing (registry).");
		ASSERT(pObjSB_GB && pObjSB_Grass && pObjSB_Shadow, "ObjectTable SB missing (registry).");
		ASSERT(pObjIndexVB, "ObjectIndexVB missing (registry).");
		ASSERT(pEnvTex && pEnvDiffTex && pEnvSpecTex && pEnvBrdfTex, "Env textures missing (registry).");
		ASSERT(pErrorTex, "Error texture missing (registry).");

		m_PassCtx.pHeightMap = &scene.GetHeightMap();
		scene.ConsumeInteractionStamps(&m_PassCtx.InteractionStamps);

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
		{
			const uint32 count = scene.GetObjectDenseCount();

			m_PassCtx.VisibleObjectIndexMain.clear();
			m_PassCtx.VisibleObjectIndexShadow.clear();

			m_PassCtx.VisibleObjectIndexMain.reserve(count);
			m_PassCtx.VisibleObjectIndexShadow.reserve(count);

			for (uint32 i = 0; i < count; ++i)
			{
				const RenderScene::SceneObject& obj = scene.GetObjectByDenseIndex(i);
				ASSERT(obj.pMesh, "Invalid scene object.");

				const Box& localBounds = obj.pMesh->LocalBounds;

				if (IntersectsFrustum(frustumMain, localBounds, obj.World, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
				{
					m_PassCtx.VisibleObjectIndexMain.push_back(i);
				}

				if (obj.bCastShadow)
				{
					if (IntersectsFrustum(frustumShadow, localBounds, obj.World, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
					{
						m_PassCtx.VisibleObjectIndexShadow.push_back(i);
					}
				}
			}
		}

		// ------------------------------------------------------------
		// Common barriers
		// ------------------------------------------------------------
		m_PassCtx.PushBarrier(pFrameCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		m_PassCtx.PushBarrier(pShadowCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		m_PassCtx.PushBarrier(pDrawCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

		m_PassCtx.PushBarrier(pObjSB_GB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(pObjSB_Grass, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(pObjSB_Shadow, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		m_PassCtx.PushBarrier(pEnvTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(pEnvDiffTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(pEnvSpecTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(pEnvBrdfTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		m_PassCtx.PushBarrier(pErrorTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		// ------------------------------------------------------------
		// Visible objects: VB/IB + Material textures/CB barriers (dedup)
		// ------------------------------------------------------------
		std::unordered_set<const MaterialRenderData*> appliedRD;
		appliedRD.reserve(1024);

		auto applyMaterialIfNeeded = [&](const MaterialRenderData* rd)
			{
				ASSERT(rd, "Material render data is null.");

				if (appliedRD.find(rd) != appliedRD.end())
					return;

				appliedRD.insert(rd);

				if (rd->ConstantBuffer)
				{
					m_PassCtx.PushBarrier(rd->ConstantBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
				}

				for (const auto pTexRD : rd->BoundTextures)
				{
					ASSERT(pTexRD && pTexRD->Texture, "Bound texture render data invalid.");
					m_PassCtx.PushBarrier(pTexRD->Texture, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
				}
			};

		for (uint32 objDense : m_PassCtx.VisibleObjectIndexMain)
		{
			const auto& obj = scene.GetObjectByDenseIndex(objDense);
			ASSERT(obj.pMesh, "Invalid scene object.");

			m_PassCtx.PushBarrier(obj.pMesh->VertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
			m_PassCtx.PushBarrier(obj.pMesh->IndexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

			for (const auto& section : obj.pMesh->Sections)
			{
				applyMaterialIfNeeded(section.pMaterial);
			}
		}

		for (uint32 objDense : m_PassCtx.VisibleObjectIndexShadow)
		{
			const auto& obj = scene.GetObjectByDenseIndex(objDense);
			ASSERT(obj.pMesh, "Invalid scene object.");

			if (!obj.bCastShadow)
				continue;

			m_PassCtx.PushBarrier(obj.pMesh->VertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
			m_PassCtx.PushBarrier(obj.pMesh->IndexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

			for (const auto& section : obj.pMesh->Sections)
			{
				if (section.pMaterial && section.pMaterial->ShadowSRB)
				{
					applyMaterialIfNeeded(section.pMaterial);
				}
			}
		}

		if (!m_PassCtx.PreBarriers.empty())
		{
			ctx->TransitionResourceStates(static_cast<uint32>(m_PassCtx.PreBarriers.size()), m_PassCtx.PreBarriers.data());
		}

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

				// Shadow PSO/SRB sources
				IPipelineState* shadowPSO = nullptr;
				IPipelineState* shadowMaskedPSO = nullptr;
				IShaderResourceBinding* shadowOpaqueSRB = nullptr;

				if (passKey == kPassShadow)
				{
					auto itPass = m_Passes.find("Shadow");
					ASSERT(itPass != m_Passes.end(), "Shadow pass not found.");
					auto* shadowPass = static_cast<ShadowRenderPass*>(itPass->second.get());
					ASSERT(shadowPass, "Shadow pass cast failed.");

					shadowPSO = shadowPass->GetShadowPSO();
					shadowMaskedPSO = shadowPass->GetShadowMaskedPSO();
					shadowOpaqueSRB = shadowPass->GetOpaqueShadowSRB();

					ASSERT(shadowPSO && shadowMaskedPSO && shadowOpaqueSRB, "Shadow PSO/SRB invalid.");
				}

				for (const auto& di : items)
				{
					RenderScene::BatchView bv = {};
					bool ok = scene.TryGetBatchView(di.BatchId, bv);
					ASSERT(ok, "Invalid batch id.");

					const StaticMeshRenderData* mesh = bv.pMesh;
					ASSERT(mesh, "Batch mesh is null.");
					ASSERT(bv.SectionIndex < static_cast<uint32>(mesh->Sections.size()), "SectionIndex OOB.");

					const auto& sec = mesh->Sections[bv.SectionIndex];
					const MaterialRenderData* mat = sec.pMaterial;

					DrawPacket pkt = {};
					pkt.VertexBuffer = mesh->VertexBuffer;
					pkt.IndexBuffer = mesh->IndexBuffer;

					pkt.DrawAttribs = {};
					pkt.DrawAttribs.IndexType = mesh->IndexType;
					pkt.DrawAttribs.NumIndices = sec.IndexCount;
					pkt.DrawAttribs.FirstIndexLocation = sec.FirstIndex;
					pkt.DrawAttribs.BaseVertex = static_cast<int32>(sec.BaseVertex);
					pkt.DrawAttribs.NumInstances = di.InstanceCount;
					pkt.DrawAttribs.FirstInstanceLocation = di.StartInstanceLocation;
					pkt.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

					if (passKey == kPassGBuffer || passKey == kPassGrass)
					{
						ASSERT(mat && mat->PSO && mat->SRB, "Material PSO/SRB invalid.");
						pkt.PSO = mat->PSO;
						pkt.SRB = mat->SRB;
					}
					else if (passKey == kPassShadow)
					{
						if (mat && mat->ShadowSRB)
						{
							pkt.PSO = shadowMaskedPSO;
							pkt.SRB = mat->ShadowSRB;
						}
						else
						{
							pkt.PSO = shadowPSO;
							pkt.SRB = shadowOpaqueSRB;
						}
					}
					else
					{
						ASSERT(false, "Unknown passKey.");
					}

					pkt.ObjectIndex = 0;
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
		scene.BuildDrawList(kPassGBuffer, m_PassCtx.VisibleObjectIndexMain, drawItems, instanceRemap);
		packObjectTableFromRemap(pObjSB_GB, instanceRemap);
		m_PassCtx.GBufferDrawPackets = buildPacketsFromDrawItems(kPassGBuffer, drawItems);

		// Grass
		scene.BuildDrawList(kPassGrass, m_PassCtx.VisibleObjectIndexMain, drawItems, instanceRemap);
		packObjectTableFromRemap(pObjSB_Grass, instanceRemap);
		m_PassCtx.GrassDrawPackets = buildPacketsFromDrawItems(kPassGrass, drawItems);

		// Shadow
		scene.BuildDrawList(kPassShadow, m_PassCtx.VisibleObjectIndexShadow, drawItems, instanceRemap);
		packObjectTableFromRemap(pObjSB_Shadow, instanceRemap);
		m_PassCtx.ShadowDrawPackets = buildPacketsFromDrawItems(kPassShadow, drawItems);

		// Sanity: if this is 0, you will see nothing (this is the #1 failure)
		// (leave as ASSERT while migrating; you can relax later)
		// ASSERT(!m_PassCtx.GBufferDrawPackets.empty(), "No GBuffer draw packets.");

		// ------------------------------------------------------------
		// Execute passes
		// ------------------------------------------------------------
		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->Execute(m_PassCtx);
		}
	}

	void Renderer::EndFrame()
	{
		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->EndFrame(m_PassCtx);
		}
	}

	void Renderer::ReleaseSwapChainBuffers()
	{
		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->ReleaseSwapChainBuffers(m_PassCtx);
		}
	}

	void Renderer::OnResize(uint32 width, uint32 height)
	{
		ASSERT(width != 0 && height != 0, "Invalid size.");

		m_Width = width;
		m_Height = height;

		m_PassCtx.BackBufferWidth = width;
		m_PassCtx.BackBufferHeight = height;

		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->OnResize(m_PassCtx, width, height);
		}
	}

	// ----------------------------
	// RenderData caches below are mostly unchanged from your code
	// ----------------------------

	const TextureRenderData& Renderer::CreateTextureRenderData(const AssetRef<Texture>& assetRef, const std::string& name)
	{
		uint64 key = std::hash<AssetID>{}(assetRef.GetID());
		const TextureRenderData* cached = m_TextureCache.Acquire(key);
		if (cached)
		{
			return *cached;
		}

		AssetPtr<Texture> assetPtr = m_pAssetManager->Acquire(assetRef);
		ASSERT(assetPtr, "Failed to acquire TextureAsset.");

		if (name == "")
		{
			return CreateTextureRenderData(*assetPtr, key, assetPtr.GetSourcePath());
		}
		else
		{
			return CreateTextureRenderData(*assetPtr, key, name);
		}
	}

	const TextureRenderData& Renderer::CreateTextureRenderData(const Texture& texture, uint64 key, const std::string& name)
	{
		if (key == 0)
		{
			key = std::rand();
		}

		TextureRenderData out;

		const auto& mips = texture.GetMips();
		ASSERT(!mips.empty(), "TextureAsset has no mips.");

		const uint32 width = mips[0].Width;
		const uint32 height = mips[0].Height;

		TextureDesc desc = {};
		desc.Name = name.c_str();
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

		out.Texture = CreateTexture(desc, &initData);
		ASSERT(out.Texture, "CreateTexture failed.");
		out.Sampler = nullptr;

		m_TextureCache.Store(key, std::move(out));
		return *m_TextureCache.Acquire(key);
	}

	const MaterialRenderData& Renderer::CreateMaterialRenderData(const AssetRef<Material>& assetRef, const std::string& name)
	{
		uint64 key = std::hash<AssetID>{}(assetRef.GetID());
		const MaterialRenderData* cached = m_MaterialCache.Acquire(key);
		if (cached)
		{
			return *cached;
		}

		AssetPtr<Material> assetPtr = m_pAssetManager->Acquire(assetRef);
		ASSERT(assetPtr, "Failed to acquire MaterialAsset.");

		if (name == "")
		{
			return CreateMaterialRenderData(*assetPtr, key, assetPtr.GetSourcePath());
		}
		else
		{
			return CreateMaterialRenderData(*assetPtr, key, name);
		}
	}

	const MaterialRenderData& Renderer::CreateMaterialRenderData(const Material& material, uint64 key, const std::string& name)
	{
		ASSERT(m_pDevice, "Device is null.");
		if (key == 0)
		{
			key = std::rand(); // TODO: better hash or REMOVE CreateMaterial overload
		}

		MaterialRenderData out = {};
		out.RenderPassId = STRING_HASH(material.GetRenderPassName());

		out.CBIndex = 0;
		for (; out.CBIndex < material.GetTemplate().GetCBufferCount(); ++out.CBIndex)
		{
			const auto& cb = material.GetTemplate().GetCBuffer(out.CBIndex);
			if (cb.Name == MaterialTemplate::MATERIAL_CBUFFER_NAME)
			{
				break;
			}
		}

		// Create PSO
		{
			const MATERIAL_PIPELINE_TYPE pipelineType = material.GetPipelineType();

			if (pipelineType == MATERIAL_PIPELINE_TYPE_GRAPHICS)
			{
				GraphicsPipelineStateCreateInfo psoCI = material.BuildGraphicsPipelineStateCreateInfo(m_RHIRenderPasses);
				ASSERT(psoCI.GraphicsPipeline.pRenderPass != nullptr, "Render pass is null.");

				out.PSO = m_pPipelineStateManager->AcquireGraphics(psoCI);
				ASSERT(out.PSO, "Failed to create PSO.");
			}
			else if (pipelineType == MATERIAL_PIPELINE_TYPE_COMPUTE)
			{
				ComputePipelineStateCreateInfo psoCI = material.BuildComputePipelineStateCreateInfo();

				out.PSO = m_pPipelineStateManager->AcquireCompute(psoCI);
				ASSERT(out.PSO, "Failed to create PSO.");
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

			for (SHADER_TYPE type : supportedShaderTypes)
			{
				if (auto* var = out.PSO->GetStaticVariableByName(type, "FRAME_CONSTANTS"))
				{
					var->Set(m_pRegistry->GetBuffer(kRes_FrameCB));
				}

				if (auto* var = out.PSO->GetStaticVariableByName(type, "DRAW_CONSTANTS"))
				{
					var->Set(m_pRegistry->GetBuffer(kRes_DrawCB));
				}

				if (auto* var = out.PSO->GetStaticVariableByName(type, "g_ObjectTable"))
				{
					var->Set(m_pRegistry->GetBufferSRV(kRes_ObjectTable_GB), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE); // TODO: different tables per pass
				}
			}
		}

		// Create SRB and bind material CB
		{
			out.PSO->CreateShaderResourceBinding(&out.SRB, true);
			ASSERT(out.SRB, "Failed to create SRB.");

			// Create dynamic material constants buffer if template has cbuffers.
			const uint32 cbCount = material.GetTemplate().GetCBufferCount();
			if (cbCount > 0)
			{
				const MaterialCBufferDesc& cb = material.GetTemplate().GetCBuffer(out.CBIndex);

				BufferDesc desc = {};
				desc.Name = "MaterialConstants";
				desc.Usage = USAGE_DEFAULT;
				desc.BindFlags = BIND_UNIFORM_BUFFER;
				desc.CPUAccessFlags = CPU_ACCESS_NONE;
				desc.Size = cb.ByteSize;

				RefCntAutoPtr<IBuffer> pBuf;
				m_pDevice->CreateBuffer(desc, nullptr, &pBuf);

				out.ConstantBuffer = pBuf;

				if (out.ConstantBuffer)
				{
					// Bind by name for first stage that exposes it.
					for (const RefCntAutoPtr<IShader>& shader : material.GetShaders())
					{
						ASSERT(shader, "Shader in source instance is null.");

						const SHADER_TYPE st = shader->GetDesc().ShaderType;

						IShaderResourceVariable* var = out.SRB->GetVariableByName(st, MaterialTemplate::MATERIAL_CBUFFER_NAME);
						if (var)
						{
							var->Set(out.ConstantBuffer);
						}
					}
				}
			}
		}

		if (material.GetBlendMode() == MATERIAL_BLEND_MODE_MASKED)
		{
			IPipelineState* shadowMaskedPSO = static_cast<ShadowRenderPass*>(m_Passes["Shadow"].get())->GetShadowMaskedPSO();
			shadowMaskedPSO->CreateShaderResourceBinding(&out.ShadowSRB, true);
			ASSERT(out.ShadowSRB, "Failed to create shadow SRB for masked material.");

			// Bind material cbuffer by name for common stages used in shadow pass.
			if (out.ConstantBuffer)
			{
				IShaderResourceVariable* v = nullptr;

				if (IShaderResourceVariable* var = out.ShadowSRB->GetVariableByName(SHADER_TYPE_VERTEX, MaterialTemplate::MATERIAL_CBUFFER_NAME))
				{
					var->Set(out.ConstantBuffer);
				}

				if (IShaderResourceVariable* var = out.ShadowSRB->GetVariableByName(SHADER_TYPE_PIXEL, MaterialTemplate::MATERIAL_CBUFFER_NAME))
				{
					var->Set(out.ConstantBuffer);
				}
			}
		}

		// Immediate initial binding
		{
			if (out.ConstantBuffer)
			{
				const uint32 cbCount = material.GetCBufferBlobCount();
				ASSERT(out.CBIndex < cbCount, "CB index out of bounds.");

				const uint8* pBlob = material.GetCBufferBlobData(out.CBIndex);
				const uint32 blobSize = material.GetCBufferBlobSize(out.CBIndex);
				ASSERT(pBlob && blobSize > 0, "Invalid blob data.");
				ASSERT(blobSize <= out.ConstantBuffer->GetDesc().Size, "Blob size exceeds CB size.");

				m_pImmediateContext->UpdateBuffer(
					out.ConstantBuffer,
					0,
					blobSize,
					pBlob,
					RESOURCE_STATE_TRANSITION_MODE_TRANSITION
				);
			}

			// Bind all textures
			{
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

					ITextureView* pView = nullptr;

					if (b.TextureRef.has_value())
					{
						const TextureRenderData& texture = CreateTextureRenderData(*b.TextureRef);
						pView = texture.Texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
						out.BoundTextures.push_back(&texture);
					}
					else
					{
						pView = m_pRegistry->GetTexture(kRes_ErrorTex)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
					}

					if (IShaderResourceVariable* var = out.SRB->GetVariableByName(SHADER_TYPE_VERTEX, resDesc.Name.c_str()))
					{
						var->Set(pView);
					}
					if (IShaderResourceVariable* var = out.SRB->GetVariableByName(SHADER_TYPE_PIXEL, resDesc.Name.c_str()))
					{
						var->Set(pView);
					}

					if (out.ShadowSRB)
					{
						if (IShaderResourceVariable* var = out.ShadowSRB->GetVariableByName(SHADER_TYPE_VERTEX, resDesc.Name.c_str()))
						{
							var->Set(pView);
						}
						if (IShaderResourceVariable* var = out.ShadowSRB->GetVariableByName(SHADER_TYPE_PIXEL, resDesc.Name.c_str()))
						{
							var->Set(pView);
						}
					}
				}

			}
		}

		m_MaterialCache.Store(key, std::move(out));
		return *m_MaterialCache.Acquire(key);
	}

	const StaticMeshRenderData& Renderer::CreateStaticMeshRenderData(const AssetRef<StaticMesh>& assetRef, const std::string& name)
	{
		uint64 key = std::hash<AssetID>{}(assetRef.GetID());
		const StaticMeshRenderData* cached = m_StaticMeshCache.Acquire(key);
		if (cached)
		{
			return *cached;
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

			d.pMaterial = &CreateMaterialRenderData(mesh.GetMaterialSlot(s.MaterialSlot));

			out.Sections.push_back(d);
		}

		m_StaticMeshCache.Store(key, std::move(out));
		return *m_StaticMeshCache.Acquire(key);
	}

	const TextureRenderData& Renderer::CreateTextureRenderDataFromHeightField(const TerrainHeightField& terrain)
	{
		TextureRenderData out = {};

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

		m_pDevice->CreateTexture(desc, &initData, &out.Texture);
		ASSERT(out.Texture, "CreateTexture(HeightField) failed.");

		out.Sampler = nullptr;

		uint64 key = std::rand(); // TODO: better hash or REMOVE CreateStaticMesh overload

		m_TextureCache.Store(key, std::move(out));
		return *m_TextureCache.Acquire(key);
	}

	const std::unordered_map<std::string, uint64> Renderer::GetPassDrawCallCountTable() const
	{
		std::unordered_map<std::string, uint64> drawCallTable;
		for (auto& passPair : m_Passes)
		{
			const std::string& name = passPair.first;
			uint64 drawCallCount = passPair.second->GetDrawCallCount();
			drawCallTable[name] = drawCallCount;
		}
		return drawCallTable;
	}

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

	void Renderer::uploadObjectIndexInstance(IDeviceContext* pCtx, uint32 objectIndex)
	{
		ASSERT(pCtx, "Context is null.");

		IBuffer* pVB = m_pRegistry->GetBuffer(kRes_ObjectIndexVB);
		ASSERT(pVB, "Object index VB is null.");

		MapHelper<uint32> map(pCtx, pVB, MAP_WRITE, MAP_FLAG_DISCARD);
		*map = objectIndex;
	}

	void Renderer::addPass(std::unique_ptr<RenderPassBase> pass)
	{
		ASSERT(pass, "Pass is null.");

		const char* name = pass->GetName();
		ASSERT(name && name[0] != '\0', "Pass name is empty.");

		auto it = m_Passes.find(name);
		ASSERT(it == m_Passes.end(), "Duplicate pass name.");

		m_PassOrder.push_back(name);
		m_Passes.emplace(name, std::move(pass));
		m_RHIRenderPasses.emplace(name, m_Passes[name]->GetRHIRenderPass());
	}
} // namespace shz


