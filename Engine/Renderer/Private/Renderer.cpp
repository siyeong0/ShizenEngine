#include "pch.h"
#include "Engine/Renderer/Public/Renderer.h"

#include "Engine/Core/Math/Math.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetManager.h"
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
		m_pAssetManager = createInfo.pAssetManager;
		m_pShaderSourceFactory = createInfo.pShaderSourceFactory;

		const SwapChainDesc& scDesc = m_CreateInfo.pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : scDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : scDesc.Height;

		m_pCache = std::make_unique<RenderResourceCache>();
		m_pCache->Initialize(m_CreateInfo.pDevice.RawPtr(), m_pAssetManager);
		m_pCache->SetErrorTexture("C:/Dev/ShizenEngine/Assets/Error.jpg");

		m_pMaterialStaticBinder = std::make_unique<RendererMaterialStaticBinder>();

		// Create shared buffers
		{
			ASSERT(m_CreateInfo.pDevice, "Device is null.");
			ASSERT(m_CreateInfo.pImmediateContext, "Context is null.");

			IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();

			CreateUniformBuffer(dev, sizeof(hlsl::FrameConstants), "Frame constants", &m_pFrameCB);
			CreateUniformBuffer(dev, sizeof(hlsl::ShadowConstants), "Shadow constants", &m_pShadowCB);

			ASSERT(m_pFrameCB, "Frame CB create failed.");
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
		}

		// Create env textures
		{
			TextureLoadInfo tli = {};
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
		ASSERT(m_pObjectTableSB, "Object table is null.");
		m_pMaterialStaticBinder->SetObjectTableSRV(m_pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));

		m_PassCtx = {};
		m_PassCtx.pDevice = m_CreateInfo.pDevice.RawPtr();
		m_PassCtx.pImmediateContext = m_CreateInfo.pImmediateContext.RawPtr();
		m_PassCtx.pSwapChain = m_CreateInfo.pSwapChain.RawPtr();
		m_PassCtx.pShaderSourceFactory = m_pShaderSourceFactory.RawPtr();
		m_PassCtx.pAssetManager = m_pAssetManager;
		m_PassCtx.pCache = m_pCache.get();
		m_PassCtx.pMaterialStaticBinder = m_pMaterialStaticBinder.get();

		m_PassCtx.pFrameCB = m_pFrameCB.RawPtr();
		m_PassCtx.pShadowCB = m_pShadowCB.RawPtr();
		m_PassCtx.pObjectTableSB = m_pObjectTableSB.RawPtr();
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
			addPass(std::make_unique<ShadowRenderPass>());
			addPass(std::make_unique<GBufferRenderPass>());
			addPass(std::make_unique<LightingRenderPass>());
			addPass(std::make_unique<PostRenderPass>());

			for (const std::string& name : m_PassOrder)
			{
				RenderPassBase* pass = m_Passes[name].get();
				ASSERT(pass, "Pass is null.");

				const bool ok = pass->Initialize(m_PassCtx);
				ASSERT(ok, "Pass initialize failed.");
			}
		}

		wirePassOutputs();

		return true;
	}

	void Renderer::Cleanup()
	{
		ReleaseSwapChainBuffers();

		for (const std::string& name : m_PassOrder)
		{
			RenderPassBase* pass = m_Passes[name].get();
			ASSERT(pass, "Pass is null.");
			pass->Cleanup();
		}

		m_Passes.clear();
		m_PassOrder.clear();

		m_EnvTex.Release();
		m_EnvDiffuseTex.Release();
		m_EnvSpecularTex.Release();
		m_EnvBrdfTex.Release();

		m_pObjectIndexVB.Release();
		m_pObjectTableSB.Release();

		m_pShadowCB.Release();
		m_pFrameCB.Release();

		if (m_pCache)
		{
			m_pCache->Shutdown();
			m_pCache.reset();
		}

		m_pMaterialStaticBinder.reset();

		m_pShaderSourceFactory.Release();
		m_pAssetManager = nullptr;

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
		ASSERT(m_PassCtx.pCache, "Cache is null.");
		ASSERT(m_PassCtx.pMaterialStaticBinder, "MaterialStaticBinder is null.");

		IDeviceContext* context = m_PassCtx.pImmediateContext;
		m_PassCtx.ResetFrame();

		// Ensure object table capacity
		const uint32 objectCount = static_cast<uint32>(scene.GetObjects().size());
		ASSERT(objectCount < DEFAULT_MAX_OBJECT_COUNT, "Two many objects.");

		// ------------------------------------------------------------
		// Update frame/object/shadow constants
		// ------------------------------------------------------------
		{
			ASSERT(context, "Context is null.");
			ASSERT(m_pObjectTableSB, "Object table is null.");

			const auto& objs = scene.GetObjects();
			const uint32 count = static_cast<uint32>(objs.size());
			if (count == 0)
			{
				return;
			}

			MapHelper<hlsl::ObjectConstants> map(context, m_pObjectTableSB, MAP_WRITE, MAP_FLAG_DISCARD);
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

		IDeviceContext* ctx = m_PassCtx.pImmediateContext;
		const View& view = viewFamily.Views[0];

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

			// Shadow (simple fixed ortho)
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
			cb->LightViewProj = lightViewProj;
		}

		{
			MapHelper<hlsl::ShadowConstants> cb(ctx, m_pShadowCB, MAP_WRITE, MAP_FLAG_DISCARD);
			cb->LightViewProj = lightViewProj;
		}

		// ------------------------------------------------------------
		// Build visibility
		// ------------------------------------------------------------

		ViewFrustumExt viewFrustum = {};
		{
			const Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix;
			ExtractViewFrustumPlanesFromMatrix(viewProj, viewFrustum);
		}

		m_PassCtx.VisibleObjectIndexMain.clear();
		m_PassCtx.VisibleObjectIndexShadow.clear();
		{
			const auto& renderObjects = scene.GetObjects();
			const uint32 count = static_cast<uint32>(renderObjects.size());

			m_PassCtx.VisibleObjectIndexMain.reserve(count);
			m_PassCtx.VisibleObjectIndexShadow.reserve(count);

			for (uint32 i = 0; i < count; ++i)
			{
				const RenderScene::RenderObject& obj = renderObjects[i];

				const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

				const Box& localBounds = mesh->GetLocalBounds(); // TODO: 실제 함수명으로

				// Frustum test (local bounds + world transform)
				if (IntersectsFrustum(viewFrustum, localBounds, obj.Transform, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
				{
					m_PassCtx.VisibleObjectIndexMain.push_back(i);

					// Shadow visibility policy (임시): view에 보이는 것 중, shadow caster만
					if (obj.bCastShadow)
					{
						m_PassCtx.VisibleObjectIndexShadow.push_back(i);
					}
				}
			}
		}

		// ------------------------------------------------------------
		// Build Frame materials and Barriers
		// ------------------------------------------------------------
		IPipelineState* shadowMaskedPSO = nullptr;
		{
			auto it = m_Passes.find("Shadow");
			if (it != m_Passes.end())
			{
				if (auto* shadowPass = static_cast<ShadowRenderPass*>(it->second.get()))
				{
					shadowMaskedPSO = shadowPass->GetShadowMaskedPSO();
				}
			}
		}

		// push common barriers
		m_PassCtx.PushBarrier(m_pFrameCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);
		m_PassCtx.PushBarrier(m_pShadowCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

		// Object indirection resources
		m_PassCtx.PushBarrier(m_pObjectTableSB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(m_pObjectIndexVB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);

		// Env
		m_PassCtx.PushBarrier(m_EnvTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(m_EnvDiffuseTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(m_EnvSpecularTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		m_PassCtx.PushBarrier(m_EnvBrdfTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);

		// Error texture
		if (m_PassCtx.pCache)
		{
			m_PassCtx.PushBarrier(m_PassCtx.pCache->GetErrorTexture().GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
		}

		auto& objectsMutable = scene.GetObjects();

		// Per-visible-object: mesh buffers + material RD creation + material resources barriers
		for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexMain)
		{
			ASSERT(objectIndex < static_cast<uint32>(objectsMutable.size()), "Object index out of bounds.");

			RenderScene::RenderObject& obj = objectsMutable[objectIndex];

			const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
			ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

			m_PassCtx.PushBarrier(mesh->GetVertexBuffer(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
			m_PassCtx.PushBarrier(mesh->GetIndexBuffer(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

			for (const auto& sec : mesh->GetSections())
			{
				ASSERT(sec.IndexCount > 0, "Invalid section. Number of indices is 0.");

				const uint32 slot = sec.MaterialSlot;
				ASSERT(slot < static_cast<uint32>(obj.Materials.size()), "Material slot index out of bounds.");

				MaterialInstance* instance = &obj.Materials[slot];
				ASSERT(instance, "Material instance in object is null.");

				const uint64 key = reinterpret_cast<uint64>(instance);

				auto it = m_PassCtx.FrameMat.find(key);

				if (it == m_PassCtx.FrameMat.end() || instance->IsPsoDirty())
				{
					IPipelineState* pShadowPsoForMat = (obj.bCastShadow && obj.bAlphaMasked) ? shadowMaskedPSO : nullptr;

					const std::string& passName = instance->GetRenderPass();
					auto passIter = m_Passes.find(passName);
					ASSERT(passIter != m_Passes.end(), "Render pass (%s) is not set in renderer.", passName);
					ASSERT(passIter->second, "Render pass (%s) is null", passName);

					instance->GetGraphicsPipelineDesc().pRenderPass = passIter->second.get()->GetRHIRenderPass();

					Handle<MaterialRenderData> matRDHandle =
						m_PassCtx.pCache->GetOrCreateMaterialRenderData(instance, ctx, m_PassCtx.pMaterialStaticBinder, pShadowPsoForMat);

					it = m_PassCtx.FrameMat.emplace(key, matRDHandle).first;
					m_PassCtx.FrameMatKeys.emplace_back(key);

					// Render data barriers
					MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(it->second);
					ASSERT(rd, "Material render data is null.");

					auto* matCB = rd->GetMaterialConstantsBuffer();
					ASSERT(matCB, "Material constant buffer is null.");
					m_PassCtx.PushBarrier(matCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

					for (auto texHandle : rd->GetBoundTextures())
					{
						auto texRD = m_PassCtx.pCache->TryGetTextureRenderData(texHandle);
						ASSERT(texRD && texRD->IsValid(), "Texture is invalid.");
						m_PassCtx.PushBarrier(texRD->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
					}
				}
			}
		}

		// Apply render data
		for (uint64 key : m_PassCtx.FrameMatKeys)
		{
			auto it = m_PassCtx.FrameMat.find(key);
			ASSERT(it != m_PassCtx.FrameMat.end(), "Material not exists in frame material buffer.");
			MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(it->second);
			ASSERT(rd, "MaterialRenderData is null.");
			MaterialInstance* inst = reinterpret_cast<MaterialInstance*>(key);
			ASSERT(inst, "Material instance is null.");

			rd->Apply(m_PassCtx.pCache, *inst, ctx);
		}

		// Apply transitions once
		if (!m_PassCtx.PreBarriers.empty())
		{
			ctx->TransitionResourceStates(static_cast<uint32>(m_PassCtx.PreBarriers.size()), m_PassCtx.PreBarriers.data());
		}

		// ------------------------------------------------------------
		// Build DrawPackets + sort
		// ------------------------------------------------------------

		{
			auto& packets = m_PassCtx.GetPassPackets("GBuffer");
			packets.clear();
			packets.reserve(m_PassCtx.VisibleObjectIndexMain.size() * 2);

			for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexMain)
			{
				ASSERT(objectIndex < static_cast<uint32>(objectsMutable.size()), "Object index out of bounds.");

				RenderScene::RenderObject& obj = objectsMutable[objectIndex];

				const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

				IBuffer* vertexBuffer = mesh->GetVertexBuffer();
				IBuffer* indexBuffer = mesh->GetIndexBuffer();
				ASSERT(vertexBuffer, "Vertex buffer is null.");
				ASSERT(indexBuffer, "Index buffer is null.");

				const VALUE_TYPE indexType = mesh->GetIndexType();

				for (const auto& sec : mesh->GetSections())
				{
					ASSERT(sec.IndexCount > 0, "Invalid section. Number of indices is 0.");

					const uint32 slot = sec.MaterialSlot;
					ASSERT(slot < static_cast<uint32>(obj.Materials.size()), "Material slot index out of bounds.");

					MaterialInstance* instance = &obj.Materials[slot];
					ASSERT(instance, "Material instance in object is null.");

					const uint64 key = reinterpret_cast<uint64>(instance);
					auto it = m_PassCtx.FrameMat.find(key);
					ASSERT(it != m_PassCtx.FrameMat.end(), "Frame material not set. All materials that used in frame must be already set.");

					MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(it->second);
					ASSERT(rd && rd->GetPSO() && rd->GetSRB(), "Material render data is invalid.");

					DrawPacket pkt = {};
					pkt.VertexBuffer = vertexBuffer;
					pkt.IndexBuffer = indexBuffer;
					pkt.PSO = rd->GetPSO();
					pkt.SRB = rd->GetSRB();
					pkt.ObjectIndex = objectIndex;

					pkt.DrawAttribs = {};
					pkt.DrawAttribs.NumIndices = sec.IndexCount;
					pkt.DrawAttribs.FirstIndexLocation = sec.FirstIndex;
					pkt.DrawAttribs.BaseVertex = static_cast<int32>(sec.BaseVertex);
					pkt.DrawAttribs.IndexType = indexType;
					pkt.DrawAttribs.NumInstances = 1;
					pkt.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

					// Basic sort keys (stable enough)
					{
						const uint64 psoKey = reinterpret_cast<uint64>(pkt.PSO);
						const uint64 srbKey = reinterpret_cast<uint64>(pkt.SRB);
						const uint64 vbKey = reinterpret_cast<uint64>(pkt.VertexBuffer);
						const uint64 ibKey = reinterpret_cast<uint64>(pkt.IndexBuffer);

						pkt.SortKey0 = (psoKey >> 4) ^ (srbKey << 1);
						pkt.SortKey1 = (vbKey >> 4) ^ (ibKey << 1) ^ (static_cast<uint64>(sec.FirstIndex) << 16);
					}

					packets.push_back(pkt);
				}
			}

			std::sort(packets.begin(), packets.end(),
				[](const DrawPacket& a, const DrawPacket& b)
				{
					if (a.SortKey0 != b.SortKey0) return a.SortKey0 < b.SortKey0;
					return a.SortKey1 < b.SortKey1;
				});
		}

		// ------------------------------------------------------------
		// Build Shadow draw packets
		// ------------------------------------------------------------
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

			std::vector<DrawPacket>& packets = m_PassCtx.GetPassPackets("Shadow");
			packets.clear();
			packets.reserve(m_PassCtx.VisibleObjectIndexShadow.size() * 2); // 섹션 고려

			auto& objs = scene.GetObjects();

			for (uint32 objectIndex : m_PassCtx.VisibleObjectIndexShadow)
			{
				ASSERT(objectIndex < static_cast<uint32>(objectsMutable.size()), "Object index out of bounds.");

				RenderScene::RenderObject& obj = objs[objectIndex];
				if (!obj.bCastShadow)
				{
					continue;
				}

				const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				ASSERT(mesh && mesh->IsValid(), "Invalid mesh data.");

				IBuffer* vertexBuffer = mesh->GetVertexBuffer();
				IBuffer* indexBuffer = mesh->GetIndexBuffer();
				ASSERT(vertexBuffer, "Vertex buffer is null.");
				ASSERT(indexBuffer, "Index buffer is null.");


				const VALUE_TYPE indexType = mesh->GetIndexType();

				for (const auto& sec : mesh->GetSections())
				{
					ASSERT(sec.IndexCount > 0, "Invalid section. Number of indices is 0.");

					DrawPacket pkt = {};
					pkt.VertexBuffer = vertexBuffer;
					pkt.IndexBuffer = indexBuffer;
					pkt.ObjectIndex = objectIndex;

					pkt.DrawAttribs = {};
					pkt.DrawAttribs.NumIndices = sec.IndexCount;
					pkt.DrawAttribs.FirstIndexLocation = sec.FirstIndex;
					pkt.DrawAttribs.BaseVertex = static_cast<int32>(sec.BaseVertex);
					pkt.DrawAttribs.IndexType = indexType;
					pkt.DrawAttribs.NumInstances = 1;
					pkt.DrawAttribs.Flags = DRAW_FLAG_VERIFY_ALL;

					// --- choose PSO/SRB ---
					if (!obj.bAlphaMasked)
					{
						// Opaque shadow caster
						pkt.PSO = shadowPSO;
						pkt.SRB = shadowSRB;
					}
					else
					{
						// Masked shadow caster (needs material-driven alpha texture)
						const uint32 slot = sec.MaterialSlot;
						ASSERT(slot < static_cast<uint32>(obj.Materials.size()), "Material slot index out of bounds.");

						MaterialInstance* instance = &obj.Materials[slot];
						ASSERT(instance, "Material instance in object is null.");

						const uint64 key = reinterpret_cast<uint64>(instance);
						auto it = m_PassCtx.FrameMat.find(key);
						ASSERT(it != m_PassCtx.FrameMat.end(), "Frame material not set. All materials that used in frame must be already set.");

						MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(it->second);
						ASSERT(rd, "Material render data is invalid.");

						IShaderResourceBinding* maskedShadowSRB = rd->GetShadowSRB();
						ASSERT(maskedShadowSRB, "Masked shadow SRB is null.");

						pkt.PSO = shadowMaskedPSO;
						pkt.SRB = maskedShadowSRB;
					}

					ASSERT(pkt.PSO && pkt.SRB, "PSO/SRB in draw packet is null.");

					// --- Sort keys (PSO -> SRB -> VB/IB -> draw range) ---
					{
						const uint64 psoKey = reinterpret_cast<uint64>(pkt.PSO);
						const uint64 srbKey = reinterpret_cast<uint64>(pkt.SRB);
						const uint64 vbKey = reinterpret_cast<uint64>(pkt.VertexBuffer);
						const uint64 ibKey = reinterpret_cast<uint64>(pkt.IndexBuffer);

						pkt.SortKey0 = (psoKey >> 4) ^ (srbKey << 1);
						pkt.SortKey1 = (vbKey >> 4) ^ (ibKey << 1) ^ (static_cast<uint64>(sec.FirstIndex) << 16);
					}

					packets.push_back(pkt);
				}
			}

			std::sort(packets.begin(), packets.end(),
				[](const DrawPacket& a, const DrawPacket& b)
				{
					if (a.SortKey0 != b.SortKey0) return a.SortKey0 < b.SortKey0;
					return a.SortKey1 < b.SortKey1;
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

	Handle<StaticMeshRenderData> Renderer::CreateStaticMesh(const StaticMeshAsset& asset)
	{
		ASSERT(m_pCache, "Cache is null.");
		return m_pCache->GetOrCreateStaticMeshRenderData(asset, m_CreateInfo.pImmediateContext.RawPtr());
	}

	bool Renderer::DestroyStaticMesh(Handle<StaticMeshRenderData> hMesh)
	{
		ASSERT(m_pCache, "Cache is null.");
		return m_pCache->DestroyStaticMeshRenderData(hMesh);
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
	}
} // namespace shz
