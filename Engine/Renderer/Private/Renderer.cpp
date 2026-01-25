#include "pch.h"
#include "Engine/Renderer/Public/Renderer.h"

#include <unordered_set>

#include "Engine/Core/Math/Math.h"
#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

#include "Tools/Image/Public/TextureUtilities.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"

#include "Engine/RenderPass/Public/ShadowRenderPass.h"
#include "Engine/RenderPass/Public/GBufferRenderPass.h"
#include "Engine/RenderPass/Public/LightingRenderPass.h"
#include "Engine/RenderPass/Public/PostRenderPass.h"

#include "Engine/RenderPass/Public/DrawPacket.h"

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

			MaterialTemplate gbufferMaskedTemplate;
			const bool ok1 = makeTemplate(gbufferMaskedTemplate, "DefaultLitMasked", "GBufferMasked.vsh", "GBufferMasked.psh");
			ASSERT(ok0 && ok1, "Build initial material templates failed.");

			m_TemplateLibrary[gbufferTemplate.GetName()] = gbufferTemplate;
			m_TemplateLibrary[gbufferMaskedTemplate.GetName()] = gbufferMaskedTemplate;

			Material::RegisterTemplateLibrary(&m_TemplateLibrary);
		}

		const SwapChainDesc& scDesc = m_pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : scDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : scDesc.Height;

		m_pPipelineStateManager = std::make_unique<PipelineStateManager>();
		m_pPipelineStateManager->Initialize(m_pDevice);

		AssetRef<Texture> ref = m_pAssetManager->RegisterAsset<Texture>("C:/Dev/ShizenEngine/Assets/Error.jpg");
		m_ErrorTexture = CreateTexture(ref);

		m_pMaterialStaticBinder = std::make_unique<RendererMaterialStaticBinder>();

		// Create shared buffers
		{
			ASSERT(m_CreateInfo.pDevice, "Device is null.");
			ASSERT(m_CreateInfo.pImmediateContext, "Context is null.");

			IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();

			CreateUniformBuffer(dev, sizeof(hlsl::FrameConstants), "Frame constants", &m_pFrameCB);
			CreateUniformBuffer(dev, sizeof(hlsl::DrawConstants), "Draw constants", &m_pDrawCB);
			CreateUniformBuffer(dev, sizeof(hlsl::ShadowConstants), "Shadow constants", &m_pShadowCB);

			ASSERT(m_pFrameCB, "Frame CB create failed.");
			ASSERT(m_pDrawCB, "Frame CB create failed.");
			ASSERT(m_pShadowCB, "Shadow CB create failed.");

			// Create object index instance buffer
			{
				IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();
				ASSERT(dev, "Device is null.");

				ASSERT(m_pObjectIndexVB == nullptr, "m_pObjectIndexVB is already initilaized");

				BufferDesc desc = {};
				desc.Name = "ObjectIndexInstanceVB";
				desc.Usage = USAGE_DYNAMIC;
				desc.BindFlags = BIND_VERTEX_BUFFER;
				desc.CPUAccessFlags = CPU_ACCESS_WRITE;
				desc.Size = sizeof(uint32);

				dev->CreateBuffer(desc, nullptr, &m_pObjectIndexVB);
				ASSERT(m_pObjectIndexVB, "Object index VB create failed.");
			}

			// Allocate object table
			{
				IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();
				ASSERT(dev, "Device is null.");

				BufferDesc desc = {};
				desc.Name = "ObjectTableSB";
				desc.Usage = USAGE_DYNAMIC;
				desc.BindFlags = BIND_SHADER_RESOURCE;
				desc.CPUAccessFlags = CPU_ACCESS_WRITE;
				desc.Mode = BUFFER_MODE_STRUCTURED;
				desc.ElementByteStride = sizeof(hlsl::ObjectConstants);
				desc.Size = uint64(desc.ElementByteStride) * uint64(DEFAULT_MAX_OBJECT_COUNT);

				RefCntAutoPtr<IBuffer> newBuf;
				dev->CreateBuffer(desc, nullptr, &newBuf);
				ASSERT(newBuf, "Object table create failed.");

				m_pObjectTableSB = newBuf;
				m_PassCtx.pObjectTableSB = m_pObjectTableSB.RawPtr();

				if (m_pMaterialStaticBinder)
				{
					m_pMaterialStaticBinder->SetObjectTableSRV(m_pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
				}
			}

			{
				IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();
				ASSERT(dev, "Device is null.");

				BufferDesc desc = {};
				desc.Name = "ObjectTableSBShadow";
				desc.Usage = USAGE_DYNAMIC;
				desc.BindFlags = BIND_SHADER_RESOURCE;
				desc.CPUAccessFlags = CPU_ACCESS_WRITE;
				desc.Mode = BUFFER_MODE_STRUCTURED;
				desc.ElementByteStride = sizeof(hlsl::ObjectConstants);
				desc.Size = uint64(desc.ElementByteStride) * uint64(DEFAULT_MAX_OBJECT_COUNT);

				RefCntAutoPtr<IBuffer> newBuf;
				dev->CreateBuffer(desc, nullptr, &newBuf);
				ASSERT(newBuf, "Object table create failed.");

				m_pObjectTableSBShadow = newBuf;
				m_PassCtx.pObjectTableSBShadow = m_pObjectTableSBShadow.RawPtr();
			}
		}

		// Create env textures
		{
			TextureLoadInfo tli = {};
			/*CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/clear_pureskyEnvHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/clear_pureskyDiffuseHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvDiffuseTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/clear_pureskySpecularHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvSpecularTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/clear_pureskyBrdf.dds", tli, m_CreateInfo.pDevice, &m_EnvBrdfTex);*/

			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleEnvHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleDiffuseHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvDiffuseTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleSpecularHDR.dds", tli, m_CreateInfo.pDevice, &m_EnvSpecularTex);
			CreateTextureFromFile("C:/Dev/ShizenEngine/Assets/Cubemap/SampleBrdf.dds", tli, m_CreateInfo.pDevice, &m_EnvBrdfTex);


			ASSERT(m_EnvTex, "Env tex load failed.");
			ASSERT(m_EnvDiffuseTex, "Env diffuse load failed.");
			ASSERT(m_EnvSpecularTex, "Env specular load failed.");
			ASSERT(m_EnvBrdfTex, "Env brdf load failed.");
		}

		m_pMaterialStaticBinder->SetFrameConstants(m_pFrameCB);
		m_pMaterialStaticBinder->SetDrawConstants(m_pDrawCB);
		m_pMaterialStaticBinder->SetObjectTableSRV(m_pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));

		m_PassCtx = {};
		m_PassCtx.pDevice = m_CreateInfo.pDevice.RawPtr();
		m_PassCtx.pImmediateContext = m_CreateInfo.pImmediateContext.RawPtr();
		m_PassCtx.pSwapChain = m_CreateInfo.pSwapChain.RawPtr();
		m_PassCtx.pShaderSourceFactory = m_pShaderSourceFactory.RawPtr();
		m_PassCtx.pAssetManager = m_pAssetManager;
		m_PassCtx.pPipelineStateManager = m_pPipelineStateManager.get();
		m_PassCtx.pMaterialStaticBinder = m_pMaterialStaticBinder.get();

		m_PassCtx.pFrameCB = m_pFrameCB.RawPtr();
		m_PassCtx.pDrawCB = m_pDrawCB.RawPtr();
		m_PassCtx.pShadowCB = m_pShadowCB.RawPtr();
		m_PassCtx.pObjectTableSB = m_pObjectTableSB.RawPtr();
		m_PassCtx.pObjectTableSBShadow = m_pObjectTableSBShadow.RawPtr();
		m_PassCtx.pObjectIndexVB = m_pObjectIndexVB.RawPtr();

		m_PassCtx.pEnvTex = m_EnvTex.RawPtr();
		m_PassCtx.pEnvDiffuseTex = m_EnvDiffuseTex.RawPtr();
		m_PassCtx.pEnvSpecularTex = m_EnvSpecularTex.RawPtr();
		m_PassCtx.pEnvBrdfTex = m_EnvBrdfTex.RawPtr();

		m_PassCtx.BackBufferWidth = m_Width;
		m_PassCtx.BackBufferHeight = m_Height;

		// Create render passes
		{
			ASSERT(m_Passes.empty(), "m_Passes are already initilaized.");
			ASSERT(m_PassOrder.empty(), "m_PassOrder are already initilaized.");
			addPass(std::make_unique<ShadowRenderPass>(m_PassCtx));
			addPass(std::make_unique<GBufferRenderPass>(m_PassCtx));
			addPass(std::make_unique<LightingRenderPass>(m_PassCtx));
			addPass(std::make_unique<PostRenderPass>(m_PassCtx));
		}

		wirePassOutputs();

		return true;
	}

	void Renderer::Cleanup()
	{
		ReleaseSwapChainBuffers();

		m_Passes.clear();
		m_PassOrder.clear();
		m_RHIRenderPasses.clear();

		m_EnvTex.Release();
		m_EnvDiffuseTex.Release();
		m_EnvSpecularTex.Release();
		m_EnvBrdfTex.Release();

		m_pObjectIndexVB.Release();
		m_pObjectTableSB.Release();
		m_pObjectTableSBShadow.Release();

		m_pShadowCB.Release();
		m_pFrameCB.Release();

		m_TextureCache.Clear();
		m_StaticMeshCache.Clear();
		m_MaterialCache.Clear();

		m_pMaterialStaticBinder.reset();

		m_pShaderSourceFactory.Release();
		m_pAssetManager = nullptr;
		m_pPipelineStateManager->Clear();
		m_pPipelineStateManager.reset();

		m_CreateInfo = {};
		m_PassCtx = {};
		m_Width = 0;
		m_Height = 0;
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
		ASSERT(m_PassCtx.pMaterialStaticBinder, "MaterialStaticBinder is null.");

		IDeviceContext* ctx = m_PassCtx.pImmediateContext;
		m_PassCtx.ResetFrame();

		// Ensure SBs exist
		ASSERT(m_pObjectTableSB, "ObjectTableSB (GBuffer) is null.");
		ASSERT(m_pObjectTableSBShadow, "ObjectTableSBShadow is null.");
		ASSERT(m_PassCtx.pDrawCB, "DrawCB is null.");

		// Wire SB pointers
		m_PassCtx.pObjectTableSB = m_pObjectTableSB.RawPtr();
		m_PassCtx.pObjectTableSBShadow = m_pObjectTableSBShadow.RawPtr();

		const View& view = viewFamily.Views[0];

		// ------------------------------------------------------------
		// Update Frame/Shadow constants + compute lightViewProj
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

			// Global light (first one)
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

			const float shadowDistance = 50.0f;
			const float3 lightPosWs = centerWs - lightForward * shadowDistance;

			float3 up = float3(0, 1, 0);
			if (Abs(Vector3::Dot(up, lightForward)) > 0.99f)
			{
				up = float3(0, 0, 1);
			}

			const Matrix4x4 lightView = Matrix4x4::LookAtLH(lightPosWs, centerWs, up);

			const float radiusXY = 25.0f;
			const float nearZ = 0.1f;
			const float farZ = 200.0f;
			const Matrix4x4 lightProj = Matrix4x4::OrthoOffCenter(
				-radiusXY, +radiusXY,
				-radiusXY, +radiusXY,
				nearZ, farZ);

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

		// ------------------------------------------------------------
		// Build frustums: Main / Shadow 
		// ------------------------------------------------------------
		ViewFrustumExt frMain = {};
		ViewFrustumExt frShadow = {};
		{
			const Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix;
			ExtractViewFrustumPlanesFromMatrix(viewProj, frMain);
			ExtractViewFrustumPlanesFromMatrix(lightViewProj, frShadow);
		}

		// ------------------------------------------------------------
		// Visibility
		// ------------------------------------------------------------
		{
			const auto& renderObjects = scene.GetObjects();
			const uint32 count = static_cast<uint32>(renderObjects.size());

			m_PassCtx.VisibleObjectIndexMain.reserve(count);
			m_PassCtx.VisibleObjectIndexShadow.reserve(count);

			for (uint32 i = 0; i < count; ++i)
			{
				const RenderScene::RenderObject& obj = renderObjects[i];

				const Box& localBounds = obj.Mesh.LocalBounds;

				if (IntersectsFrustum(frMain, localBounds, obj.Transform, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
				{
					m_PassCtx.VisibleObjectIndexMain.push_back(i);
				}

				if (obj.bCastShadow)
				{
					if (IntersectsFrustum(frShadow, localBounds, obj.Transform, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
					{
						m_PassCtx.VisibleObjectIndexShadow.push_back(i);
					}
				}
			}
		}

		// ------------------------------------------------------------
		// Common barriers (Frame/Shadow/Env/etc.)
		// ------------------------------------------------------------
		m_PassCtx.PushBarrier(m_pFrameCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		m_PassCtx.PushBarrier(m_pShadowCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

		m_PassCtx.PushBarrier(m_pObjectTableSB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(m_pObjectTableSBShadow, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		m_PassCtx.PushBarrier(m_PassCtx.pDrawCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

		m_PassCtx.PushBarrier(m_EnvTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(m_EnvDiffuseTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(m_EnvSpecularTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(m_EnvBrdfTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		m_PassCtx.PushBarrier(m_ErrorTexture.Texture, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		auto& objectsMutable = scene.GetObjects();

		// ------------------------------------------------------------
		// Material RD apply
		// ------------------------------------------------------------
		std::unordered_set<MaterialRenderData*> appliedRD;
		appliedRD.reserve(1024);

		auto applyMaterialIfNeeded = [&](MaterialRenderData& rd)
			{
				if (appliedRD.find(&rd) != appliedRD.end())
				{
					return;
				}

				appliedRD.insert(&rd);

				// Dynamic MaterialConstants 포함한 SRB 업데이트를 여기서 보장
				// rd->Apply(m_PassCtx.pCache, ctx); //TODO: Apply?

				// Barriers: mat CB / bound textures
				if (rd.ConstantBuffer)
				{
					m_PassCtx.PushBarrier(rd.ConstantBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
				}

				for (auto tex : rd.BoundTextures)
				{
					m_PassCtx.PushBarrier(tex.Texture, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
				}
			};

		// Mesh VB/IB barriers + material apply (Main)
		for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexMain)
		{
			RenderScene::RenderObject& obj = objectsMutable[objectIndex];

			m_PassCtx.PushBarrier(obj.Mesh.VertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
			m_PassCtx.PushBarrier(obj.Mesh.IndexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

			for (auto& section : obj.Mesh.Sections)
			{
				MaterialRenderData& rd = section.Material;
				applyMaterialIfNeeded(rd);
			}
		}

		// Shadow: alpha-masked only (opaque shadow SRB는 보통 material constants 필요 없음)
		for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexShadow)
		{
			RenderScene::RenderObject& obj = objectsMutable[objectIndex];
			if (!obj.bCastShadow)
			{
				continue;
			}

			m_PassCtx.PushBarrier(obj.Mesh.VertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
			m_PassCtx.PushBarrier(obj.Mesh.IndexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

			for (auto& section : obj.Mesh.Sections)
			{
				if (section.Material.ShadowSRB)
				{
					MaterialRenderData& rd = section.Material;
					applyMaterialIfNeeded(rd);
				}
			}
		}

		// Apply transitions once
		if (!m_PassCtx.PreBarriers.empty())
		{
			ctx->TransitionResourceStates(static_cast<uint32>(m_PassCtx.PreBarriers.size()), m_PassCtx.PreBarriers.data());
		}

		// ============================================================
		//  Build GBuffer packets + pack ObjectTableSB (2-pass batching)
		// ============================================================
		{
			std::unordered_map<DrawPacketKey, BatchInfo, DrawPacketKeyHasher> batches;
			batches.reserve(m_PassCtx.VisibleObjectIndexMain.size() * 4);

			// -------- Pass 1: count instances per batch
			uint32 totalInstances = 0;

			for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexMain)
			{
				RenderScene::RenderObject& obj = objectsMutable[objectIndex];

				IBuffer* vb = obj.Mesh.VertexBuffer;
				IBuffer* ib = obj.Mesh.IndexBuffer;
				ASSERT(vb && ib, "Mesh buffers are invalid.");

				const VALUE_TYPE indexType = obj.Mesh.IndexType;
				for (auto& section : obj.Mesh.Sections)
				{
					ASSERT(section.IndexCount > 0, "Invalid section. IndexCount == 0.");
					const MaterialRenderData& rd = section.Material;
					ASSERT(rd.PSO && rd.SRB, "Material render data is invalid.");

					DrawPacketKey key = {};
					key.PSO = rd.PSO;
					key.SRB = rd.SRB;
					key.VB = vb;
					key.IB = ib;
					key.IndexType = indexType;
					key.NumIndices = section.IndexCount;
					key.FirstIndexLocation = section.FirstIndex;
					key.BaseVertex = section.BaseVertex;

					auto it = batches.find(key);
					if (it == batches.end())
					{
						BatchInfo bi = {};
						bi.Packet = {};
						bi.Packet.VertexBuffer = vb;
						bi.Packet.IndexBuffer = ib;
						bi.Packet.PSO = key.PSO;
						bi.Packet.SRB = key.SRB;
						bi.Packet.ObjectIndex = 0; // instance table path

						bi.Packet.DrawAttribs = {};
						bi.Packet.DrawAttribs.NumIndices = key.NumIndices;
						bi.Packet.DrawAttribs.FirstIndexLocation = key.FirstIndexLocation;
						bi.Packet.DrawAttribs.BaseVertex = key.BaseVertex;
						bi.Packet.DrawAttribs.IndexType = key.IndexType;
						bi.Packet.DrawAttribs.NumInstances = 0;              // finalize later
						bi.Packet.DrawAttribs.FirstInstanceLocation = 0;     // finalize later
						bi.Packet.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

						auto [newIt, ok] = batches.emplace(key, bi);
						ASSERT(ok, "Failed to emplace GBuffer batch.");
						it = newIt;
					}

					it->second.Count += 1;
					totalInstances += 1;
				}
			}

			ASSERT(totalInstances < DEFAULT_MAX_OBJECT_COUNT, "Increase object table capacity (GBuffer).");

			// -------- Prefix sum: assign FirstInstance per batch
			{
				uint32 cursor = 0;
				for (auto& kv : batches)
				{
					BatchInfo& bi = kv.second;
					bi.FirstInstance = cursor;
					bi.Cursor = 0;
					cursor += bi.Count;
				}
			}

			// -------- Pass 2: fill SB at the correct contiguous ranges
			{
				MapHelper<hlsl::ObjectConstants> map(ctx, m_pObjectTableSB, MAP_WRITE, MAP_FLAG_DISCARD);
				hlsl::ObjectConstants* dst = map;

				for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexMain)
				{
					const RenderScene::RenderObject& obj = objectsMutable[objectIndex];

					IBuffer* vb = obj.Mesh.VertexBuffer;
					IBuffer* ib = obj.Mesh.IndexBuffer;
					const VALUE_TYPE indexType = obj.Mesh.IndexType;

					for (auto& section : obj.Mesh.Sections)
					{
						const MaterialRenderData& rd = section.Material;

						DrawPacketKey key = {};
						key.PSO = rd.PSO;
						key.SRB = rd.SRB;
						key.VB = vb;
						key.IB = ib;
						key.IndexType = indexType;
						key.NumIndices = section.IndexCount;
						key.FirstIndexLocation = section.FirstIndex;
						key.BaseVertex = section.BaseVertex;

						auto it = batches.find(key);
						ASSERT(it != batches.end(), "Batch not found (GBuffer).");

						BatchInfo& bi = it->second;

						const uint32 writeIndex = bi.FirstInstance + bi.Cursor;
						bi.Cursor += 1;

						hlsl::ObjectConstants oc = {};
						oc.World = obj.Transform;
						oc.WorldInvTranspose = obj.Transform.Inversed().Transposed();

						dst[writeIndex] = oc;
					}
				}
			}

			// -------- Finalize packets
			m_PassCtx.GBufferDrawPackets.clear();
			m_PassCtx.GBufferDrawPackets.reserve(batches.size());

			for (auto& kv : batches)
			{
				BatchInfo& bi = kv.second;
				bi.Packet.DrawAttribs.NumInstances = bi.Count;
				bi.Packet.DrawAttribs.FirstInstanceLocation = bi.FirstInstance;
				m_PassCtx.GBufferDrawPackets.push_back(bi.Packet);
			}

			// Sort packets for state change minimization (SB layout에는 영향 없음)
			std::sort(m_PassCtx.GBufferDrawPackets.begin(), m_PassCtx.GBufferDrawPackets.end(),
				[](const DrawPacket& a, const DrawPacket& b)
				{
					if (a.PSO != b.PSO) return a.PSO < b.PSO;
					if (a.SRB != b.SRB) return a.SRB < b.SRB;
					if (a.VertexBuffer != b.VertexBuffer) return a.VertexBuffer < b.VertexBuffer;
					if (a.IndexBuffer != b.IndexBuffer)  return a.IndexBuffer < b.IndexBuffer;

					if (a.DrawAttribs.IndexType != b.DrawAttribs.IndexType) return a.DrawAttribs.IndexType < b.DrawAttribs.IndexType;
					if (a.DrawAttribs.FirstIndexLocation != b.DrawAttribs.FirstIndexLocation) return a.DrawAttribs.FirstIndexLocation < b.DrawAttribs.FirstIndexLocation;
					if (a.DrawAttribs.NumIndices != b.DrawAttribs.NumIndices) return a.DrawAttribs.NumIndices < b.DrawAttribs.NumIndices;
					if (a.DrawAttribs.BaseVertex != b.DrawAttribs.BaseVertex) return a.DrawAttribs.BaseVertex < b.DrawAttribs.BaseVertex;

					return a.DrawAttribs.FirstInstanceLocation < b.DrawAttribs.FirstInstanceLocation;
				});
		}

		// ============================================================
		//  Build Shadow packets + pack ObjectTableSBShadow (2-pass)
		// ============================================================
		{
			auto itPass = m_Passes.find("Shadow");
			ASSERT(itPass != m_Passes.end(), "Shadow pass not found.");

			auto* shadowPass = static_cast<ShadowRenderPass*>(itPass->second.get());
			ASSERT(shadowPass, "Shadow pass cast failed.");

			IPipelineState* shadowPSO = shadowPass->GetShadowPSO();
			IPipelineState* shadowMaskedPSO = shadowPass->GetShadowMaskedPSO();
			IShaderResourceBinding* shadowSRB = shadowPass->GetOpaqueShadowSRB();

			ASSERT(shadowPSO, "ShadowPSO is null.");
			ASSERT(shadowMaskedPSO, "ShadowMaskedPSO is null.");
			ASSERT(shadowSRB, "ShadowSRB is null.");

			std::unordered_map<DrawPacketKey, BatchInfo, DrawPacketKeyHasher> batches;
			batches.reserve(m_PassCtx.VisibleObjectIndexShadow.size() * 4);

			// Pass1 count
			uint32 totalInstances = 0;

			for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexShadow)
			{
				RenderScene::RenderObject& obj = objectsMutable[objectIndex];
				if (!obj.bCastShadow)
				{
					continue;
				}

				IBuffer* vb = obj.Mesh.VertexBuffer;
				IBuffer* ib = obj.Mesh.IndexBuffer;
				ASSERT(vb && ib, "Mesh buffers are invalid.");

				const VALUE_TYPE indexType = obj.Mesh.IndexType;
				for (const auto& section : obj.Mesh.Sections)
				{
					ASSERT(section.IndexCount > 0, "Invalid section. IndexCount == 0.");

					IPipelineState* pso = nullptr;
					IShaderResourceBinding* srb = nullptr;

					const MaterialRenderData& rd = section.Material;
					if (!rd.ShadowSRB)
					{
						pso = shadowPSO;
						srb = shadowSRB;
					}
					else
					{
						IShaderResourceBinding* maskedShadowSRB = rd.ShadowSRB;
						pso = shadowMaskedPSO;
						srb = maskedShadowSRB;
					}

					DrawPacketKey key = {};
					key.PSO = pso;
					key.SRB = srb;
					key.VB = vb;
					key.IB = ib;
					key.IndexType = indexType;
					key.NumIndices = section.IndexCount;
					key.FirstIndexLocation = section.FirstIndex;
					key.BaseVertex = section.BaseVertex;

					auto it = batches.find(key);
					if (it == batches.end())
					{
						BatchInfo bi = {};
						bi.Packet = {};
						bi.Packet.VertexBuffer = vb;
						bi.Packet.IndexBuffer = ib;
						bi.Packet.PSO = key.PSO;
						bi.Packet.SRB = key.SRB;
						bi.Packet.ObjectIndex = 0;

						bi.Packet.DrawAttribs = {};
						bi.Packet.DrawAttribs.NumIndices = key.NumIndices;
						bi.Packet.DrawAttribs.FirstIndexLocation = key.FirstIndexLocation;
						bi.Packet.DrawAttribs.BaseVertex = key.BaseVertex;
						bi.Packet.DrawAttribs.IndexType = key.IndexType;
						bi.Packet.DrawAttribs.NumInstances = 0;
						bi.Packet.DrawAttribs.FirstInstanceLocation = 0;
						bi.Packet.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

						auto [newIt, ok] = batches.emplace(key, bi);
						ASSERT(ok, "Failed to emplace Shadow batch.");
						it = newIt;
					}

					it->second.Count += 1;
					totalInstances += 1;
				}
			}

			ASSERT(totalInstances < DEFAULT_MAX_OBJECT_COUNT, "Increase object table capacity (Shadow).");

			// Prefix sum
			{
				uint32 cursor = 0;
				for (auto& kv : batches)
				{
					BatchInfo& bi = kv.second;
					bi.FirstInstance = cursor;
					bi.Cursor = 0;
					cursor += bi.Count;
				}
			}

			// Pass2 fill SBShadow
			{
				MapHelper<hlsl::ObjectConstants> map(ctx, m_pObjectTableSBShadow, MAP_WRITE, MAP_FLAG_DISCARD);
				hlsl::ObjectConstants* dst = map;

				for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexShadow)
				{
					const RenderScene::RenderObject& obj = objectsMutable[objectIndex];
					if (!obj.bCastShadow)
					{
						continue;
					}

					IBuffer* vb = obj.Mesh.VertexBuffer;
					IBuffer* ib = obj.Mesh.IndexBuffer;
					const VALUE_TYPE indexType = obj.Mesh.IndexType;

					for (const auto& section : obj.Mesh.Sections)
					{
						IPipelineState* pso = nullptr;
						IShaderResourceBinding* srb = nullptr;

						const MaterialRenderData& rd = section.Material;
						if (!rd.ShadowSRB)
						{
							pso = shadowPSO;
							srb = shadowSRB;
						}
						else
						{
							IShaderResourceBinding* maskedShadowSRB = rd.ShadowSRB;
							pso = shadowMaskedPSO;
							srb = maskedShadowSRB;
						}

						DrawPacketKey key = {};
						key.PSO = pso;
						key.SRB = srb;
						key.VB = vb;
						key.IB = ib;
						key.IndexType = indexType;
						key.NumIndices = section.IndexCount;
						key.FirstIndexLocation = section.FirstIndex;
						key.BaseVertex = section.BaseVertex;

						auto it = batches.find(key);
						ASSERT(it != batches.end(), "Batch not found (Shadow).");

						BatchInfo& bi = it->second;

						const uint32 writeIndex = bi.FirstInstance + bi.Cursor;
						bi.Cursor += 1;

						hlsl::ObjectConstants oc = {};
						oc.World = obj.Transform;
						oc.WorldInvTranspose = obj.Transform.Inversed().Transposed();

						dst[writeIndex] = oc;
					}
				}
			}

			// Finalize packets
			m_PassCtx.ShadowDrawPackets.clear();
			m_PassCtx.ShadowDrawPackets.reserve(batches.size());

			for (auto& kv : batches)
			{
				BatchInfo& bi = kv.second;
				bi.Packet.DrawAttribs.NumInstances = bi.Count;
				bi.Packet.DrawAttribs.FirstInstanceLocation = bi.FirstInstance;
				m_PassCtx.ShadowDrawPackets.push_back(bi.Packet);
			}

			std::sort(m_PassCtx.ShadowDrawPackets.begin(), m_PassCtx.ShadowDrawPackets.end(),
				[](const DrawPacket& a, const DrawPacket& b)
				{
					if (a.PSO != b.PSO) return a.PSO < b.PSO;
					if (a.SRB != b.SRB) return a.SRB < b.SRB;
					if (a.VertexBuffer != b.VertexBuffer) return a.VertexBuffer < b.VertexBuffer;
					if (a.IndexBuffer != b.IndexBuffer)  return a.IndexBuffer < b.IndexBuffer;

					if (a.DrawAttribs.IndexType != b.DrawAttribs.IndexType) return a.DrawAttribs.IndexType < b.DrawAttribs.IndexType;
					if (a.DrawAttribs.FirstIndexLocation != b.DrawAttribs.FirstIndexLocation) return a.DrawAttribs.FirstIndexLocation < b.DrawAttribs.FirstIndexLocation;
					if (a.DrawAttribs.NumIndices != b.DrawAttribs.NumIndices) return a.DrawAttribs.NumIndices < b.DrawAttribs.NumIndices;
					if (a.DrawAttribs.BaseVertex != b.DrawAttribs.BaseVertex) return a.DrawAttribs.BaseVertex < b.DrawAttribs.BaseVertex;

					return a.DrawAttribs.FirstInstanceLocation < b.DrawAttribs.FirstInstanceLocation;
				});
		}

		// ------------------------------------------------------------
		// Execute passes
		// ------------------------------------------------------------
		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->Execute(m_PassCtx);
		}

		wirePassOutputs();
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

		wirePassOutputs();
	}

	const TextureRenderData& Renderer::CreateTexture(const AssetRef<Texture>& assetRef, const std::string& name)
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
			return CreateTexture(*assetPtr, key, assetPtr.GetSourcePath());
		}
		else
		{
			return CreateTexture(*assetPtr, key, name);
		}
	}

	const TextureRenderData& Renderer::CreateTexture(const Texture& asset, uint64 key, const std::string& name)
	{
		if (key == 0)
		{
			key = std::rand(); // TODO: better hash or REMOVE CreateTexture overload
		}

		TextureRenderData out;

		const auto& mips = asset.GetMips();
		ASSERT(!mips.empty(), "TextureAsset has no mips.");

		const uint32 width = mips[0].Width;
		const uint32 height = mips[0].Height;

		// Validate mip chain
		for (size_t i = 0; i < mips.size(); ++i)
		{
			const TextureMip& mip = mips[i];
			ASSERT(mip.Width != 0 && mip.Height != 0, "Invalid mip dimension.");

			const uint64 expectedBytes = uint64(mip.Width) * uint64(mip.Height) * 4ull;
			ASSERT(mip.RGBA.data(), "Mip RGBA data is null.");
		}

		TextureDesc desc = {};
		desc.Name = name.c_str();
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = static_cast<uint32>(mips.size());
		desc.ArraySize = 1;

		// Since system-memory format is RGBA8,.
		desc.Format = TEX_FORMAT_RGBA8_UNORM;

		desc.Usage = USAGE_DEFAULT;
		desc.BindFlags = BIND_SHADER_RESOURCE;

		std::vector<TextureSubResData> subres;
		subres.resize(mips.size());

		for (size_t i = 0; i < mips.size(); ++i)
		{
			const TextureMip& mip = mips[i];

			TextureSubResData sr = {};
			sr.pData = mip.RGBA.data();
			sr.Stride = mip.Width * 4; // tightly packed RGBA8 row pitch
			sr.DepthStride = 0;

			subres[i] = sr;
		}

		TextureData initData = {};
		initData.pSubResources = subres.data();
		initData.NumSubresources = static_cast<uint32>(subres.size());

		m_pDevice->CreateTexture(desc, &initData, &out.Texture);
		ASSERT(out.Texture, "CreateTexture failed.");
		out.Sampler = nullptr;

		m_TextureCache.Store(key, out);
		return *m_TextureCache.Acquire(key);
	}


	const MaterialRenderData& Renderer::CreateMaterial(const AssetRef<Material>& assetRef, const std::string& name)
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
			return CreateMaterial(*assetPtr, key, assetPtr.GetSourcePath());
		}
		else
		{
			return CreateMaterial(*assetPtr, key, name);
		}
	}

	const MaterialRenderData& Renderer::CreateMaterial(const Material& material, uint64 key, const std::string& name)
	{
		if (key == 0)
		{
			key = std::rand(); // TODO: better hash or REMOVE CreateMaterial overload
		}

		MaterialRenderData out = {};

		ASSERT(m_pDevice, "Device is null.");

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

			if (m_pMaterialStaticBinder)
			{
				bool ok = m_pMaterialStaticBinder->BindStatics(out.PSO);
				ASSERT(ok, "Failed to bind statics.");
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
						const TextureRenderData& texture = CreateTexture(*b.TextureRef);
						pView = texture.Texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
						out.BoundTextures.push_back(texture);
					}
					else
					{
						pView = m_ErrorTexture.Texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
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

		m_MaterialCache.Store(key, out);
		return *m_MaterialCache.Acquire(key);
	}

	const StaticMeshRenderData& Renderer::CreateStaticMesh(const AssetRef<StaticMesh>& assetRef, const std::string& name)
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
			return CreateStaticMesh(*assetPtr, key, assetPtr.GetSourcePath());
		}
		else
		{
			return CreateStaticMesh(*assetPtr, key);
		}
	}

	const StaticMeshRenderData& Renderer::CreateStaticMesh(const StaticMesh& asset, uint64 key, const std::string& name)
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
			const uint32 vtxCount = asset.GetVertexCount();
			packed.resize(vtxCount);

			const std::vector<float3>& positions = asset.GetPositions();
			const std::vector<float3>& normals = asset.GetNormals();
			const std::vector<float3>& tangents = asset.GetTangents();
			const std::vector<float2>& texCoords = asset.GetTexCoords();

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

		const void* pIndexData = asset.GetIndexData();
		const uint32 ibBytes = asset.GetIndexDataSizeBytes();
		ASSERT(pIndexData && ibBytes > 0, "Invalid index data in StaticMeshAsset.");

		RefCntAutoPtr<IBuffer> pIB = createImmutableBuffer(m_pDevice, "StaticMesh_IB", BIND_INDEX_BUFFER, pIndexData, ibBytes);
		ASSERT(pIB, "Failed to create index buffer for StaticMesh.");

		StaticMeshRenderData out = {};
		out.VertexBuffer = pVB;
		out.IndexBuffer = pIB;
		out.VertexStride = static_cast<uint32>(sizeof(PackedStaticVertex));
		out.VertexCount = asset.GetVertexCount();
		out.IndexCount = asset.GetIndexCount();
		out.IndexType = asset.GetIndexType();
		out.LocalBounds = asset.GetBounds();

		out.Sections.reserve(asset.GetSections().size());
		for (const auto& s : asset.GetSections())
		{
			StaticMeshRenderData::Section d{};
			d.FirstIndex = s.FirstIndex;
			d.IndexCount = s.IndexCount;
			d.BaseVertex = s.BaseVertex;
			d.LocalBounds = s.LocalBounds;

			d.Material = CreateMaterial(asset.GetMaterialSlot(s.MaterialSlot));

			out.Sections.push_back(d);
		}

		m_StaticMeshCache.Store(key, out);
		return *m_StaticMeshCache.Acquire(key);

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
		ASSERT(m_pObjectIndexVB, "Object index VB is null.");

		MapHelper<uint32> map(pCtx, m_pObjectIndexVB, MAP_WRITE, MAP_FLAG_DISCARD);
		*map = objectIndex;
	}

	void Renderer::wirePassOutputs()
	{
		if (auto* shadow = static_cast<ShadowRenderPass*>(m_Passes["Shadow"].get()))
		{
			m_PassCtx.pShadowMapSrv = shadow->GetShadowMapSRV();
		}

		if (auto* gb = static_cast<GBufferRenderPass*>(m_Passes["GBuffer"].get()))
		{
			for (uint32 i = 0; i < RenderPassContext::NUM_GBUFFERS; ++i)
				m_PassCtx.pGBufferSrv[i] = gb->GetGBufferSRV(i);

			m_PassCtx.pDepthSrv = gb->GetDepthSRV();
		}

		if (auto* light = static_cast<LightingRenderPass*>(m_Passes["Lighting"].get()))
		{
			m_PassCtx.pLightingSrv = light->GetLightingSRV();
		}
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
