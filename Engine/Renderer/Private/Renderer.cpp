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

			// Ensure object table capacity
			m_ObjectTableCapacity = 0;
			bool ok = ensureObjectTableCapacity(DEFAULT_MAX_OBJECT_COUNT);
			ASSERT(ok, "Failed to make a object table buffer.");
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
		m_ObjectTableCapacity = 0;

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

		// ------------------------------------------------------------
		// 0) Reset per-frame (NEW)
		// ------------------------------------------------------------
		m_PassCtx.ResetFrame();

		// ------------------------------------------------------------
		// 0.5) Ensure object table capacity
		// ------------------------------------------------------------
		const uint32 objectCount = static_cast<uint32>(scene.GetObjects().size());
		if (!ensureObjectTableCapacity(objectCount))
		{
			return;
		}

		// ------------------------------------------------------------
		// 0.75) Upload object table (world matrices) - 그대로
		// ------------------------------------------------------------
		{
			ASSERT(context, "Context is null.");
			ASSERT(m_pObjectTableSB, "Object table is null.");

			const auto& objs = scene.GetObjects();
			const uint32 count = static_cast<uint32>(objs.size());
			if (count == 0)
				return;

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

		// ------------------------------------------------------------
		// 1) Update frame/shadow constants (old logic)
		// ------------------------------------------------------------
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

			const float shadowDistance = 50.0f;
			const float3 lightPosWs = centerWs - lightForward * shadowDistance;

			float3 up = float3(0, 1, 0);
			if (Abs(Vector3::Dot(up, lightForward)) > 0.99f)
				up = float3(0, 0, 1);

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
		// 1.5) Build visibility (NEW) -> store in ctx
		// ------------------------------------------------------------

		// Build view frustum from current view-projection.
		// D3D/Vulkan clip space: near = 0 -> bIsOpenGL = false
		ViewFrustumExt viewFrustum = {};
		{
			const Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix;
			ExtractViewFrustumPlanesFromMatrix(viewProj, viewFrustum /*, bIsOpenGL=false*/);
		}

		// TODO(선택): shadow frustum / ortho volume 기반 visibility도 별도 계산 가능
		// 일단은 main visibility만 만들고, shadow는 동일 리스트를 쓰거나 bCastShadow로 필터링.
		m_PassCtx.Visible_Main.clear();
		m_PassCtx.Visible_Shadow.clear();

		{
			const auto& objs = scene.GetObjects();
			const uint32 count = static_cast<uint32>(objs.size());

			m_PassCtx.Visible_Main.reserve(count);
			m_PassCtx.Visible_Shadow.reserve(count);

			for (uint32 i = 0; i < count; ++i)
			{
				const RenderScene::RenderObject& obj = objs[i];

				const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				if (!mesh || !mesh->IsValid())
					continue;

				const Box& localBounds = mesh->GetLocalBounds(); // TODO: 실제 함수명으로

				// Frustum test (local bounds + world transform)
				if (IntersectsFrustum(viewFrustum, localBounds, obj.Transform, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
				{
					m_PassCtx.Visible_Main.push_back(i);

					// Shadow visibility policy (임시): view에 보이는 것 중, shadow caster만
					if (obj.bCastShadow)
					{
						m_PassCtx.Visible_Shadow.push_back(i);
					}
				}
			}
		}

		// ------------------------------------------------------------
		// 2) Build FrameMat + Barriers (MUST be BEFORE DrawPackets)
		// ------------------------------------------------------------

		// ShadowMaskedPSO는 FrameMat 생성 시 전달되므로 ShadowRenderPass에서 꺼내온다.
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

		auto& objsMutable = scene.GetObjects(); // packets 만들 때도 필요

		// Per-visible-object: mesh buffers + material RD creation + material resources barriers
		for (uint32 objIndex : m_PassCtx.Visible_Main)
		{
			if (objIndex >= static_cast<uint32>(objsMutable.size()))
				continue;

			RenderScene::RenderObject& obj = objsMutable[objIndex];

			const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
			if (!mesh || !mesh->IsValid())
				continue;

			m_PassCtx.PushBarrier(mesh->GetVertexBuffer(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER);
			m_PassCtx.PushBarrier(mesh->GetIndexBuffer(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER);

			for (const auto& sec : mesh->GetSections())
			{
				if (sec.IndexCount == 0)
					continue;

				const uint32 slot = sec.MaterialSlot;
				if (slot >= static_cast<uint32>(obj.Materials.size()))
					continue;

				MaterialInstance* inst = &obj.Materials[slot];
				if (!inst)
					continue;

				const MaterialTemplate* tmpl = inst->GetTemplate();
				if (!tmpl)
					continue;

				const uint64 key = reinterpret_cast<uint64>(inst);

				auto it = m_PassCtx.FrameMat.find(key);
				if (it == m_PassCtx.FrameMat.end() || inst->IsPsoDirty())
				{
					IPipelineState* pShadowPsoForMat =
						(obj.bCastShadow && obj.bAlphaMasked) ? shadowMaskedPSO : nullptr;

					// NOTE: inst->GetRenderPass() must map to a registered pass
					auto passIt = m_Passes.find(inst->GetRenderPass());
					if (passIt == m_Passes.end() || !passIt->second)
						continue;

					inst->GetGraphicsPipelineDesc().pRenderPass = passIt->second.get()->GetRHIRenderPass();

					Handle<MaterialRenderData> hRD =
						m_PassCtx.pCache->GetOrCreateMaterialRenderData(
							inst,
							ctx,
							m_PassCtx.pMaterialStaticBinder,
							pShadowPsoForMat);

					it = m_PassCtx.FrameMat.emplace(key, hRD).first;
					m_PassCtx.FrameMatKeys.push_back(key);
				}

				MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(it->second);
				if (!rd)
					continue;

				if (auto* matCB = rd->GetMaterialConstantsBuffer())
					m_PassCtx.PushBarrier(matCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER);

				for (auto texHandle : rd->GetBoundTextures())
				{
					auto texRD = m_PassCtx.pCache->TryGetTextureRenderData(texHandle);
					if (texRD)
						m_PassCtx.PushBarrier(texRD->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
				}
			}
		}

		// Apply RD and re-push texture barriers
		for (uint64 key : m_PassCtx.FrameMatKeys)
		{
			auto it = m_PassCtx.FrameMat.find(key);
			if (it == m_PassCtx.FrameMat.end())
				continue;

			MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(it->second);
			if (!rd)
				continue;

			MaterialInstance* inst = reinterpret_cast<MaterialInstance*>(key);
			if (!inst)
				continue;

			rd->Apply(m_PassCtx.pCache, *inst, ctx);

			for (const auto& hTexRD : rd->GetBoundTextures())
			{
				if (auto* texRD = m_PassCtx.pCache->TryGetTextureRenderData(hTexRD))
					m_PassCtx.PushBarrier(texRD->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE);
			}
		}

		// Apply transitions once
		if (!m_PassCtx.PreBarriers.empty())
		{
			ctx->TransitionResourceStates(
				static_cast<uint32>(m_PassCtx.PreBarriers.size()),
				m_PassCtx.PreBarriers.data());
		}

		// ------------------------------------------------------------
		// 3) Build DrawPackets (AFTER FrameMat ready) + sort (NEW)
		// ------------------------------------------------------------

		// Example: build GBuffer packets (you can split per pass later)
		{
			auto& packets = m_PassCtx.GetPassPackets("GBuffer");
			packets.clear();
			packets.reserve(m_PassCtx.Visible_Main.size() * 2); // 섹션 고려 여유

			for (uint32 objIndex : m_PassCtx.Visible_Main)
			{
				if (objIndex >= static_cast<uint32>(objsMutable.size()))
					continue;

				RenderScene::RenderObject& obj = objsMutable[objIndex];

				const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				if (!mesh || !mesh->IsValid())
					continue;

				IBuffer* vb = mesh->GetVertexBuffer();
				IBuffer* ib = mesh->GetIndexBuffer();
				if (!vb || !ib)
					continue;

				const VALUE_TYPE indexType = mesh->GetIndexType();

				for (const auto& sec : mesh->GetSections())
				{
					if (sec.IndexCount == 0)
						continue;

					const uint32 slot = sec.MaterialSlot;
					if (slot >= static_cast<uint32>(obj.Materials.size()))
						continue;

					MaterialInstance* inst = &obj.Materials[slot];
					if (!inst)
						continue;

					const uint64 key = reinterpret_cast<uint64>(inst);
					auto it = m_PassCtx.FrameMat.find(key);
					if (it == m_PassCtx.FrameMat.end())
						continue;

					MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(it->second);
					if (!rd || !rd->GetPSO() || !rd->GetSRB())
						continue;

					DrawPacket pkt = {};
					pkt.VertexBuffer = vb;
					pkt.IndexBuffer = ib;
					pkt.PSO = rd->GetPSO();
					pkt.SRB = rd->GetSRB();
					pkt.ObjectIndex = objIndex;

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
// Build Shadow draw packets (NEW)
// - ctx.Visible_Shadow 기반
// - Opaque : ShadowPSO + ShadowSRB(공용)
// - Masked : ShadowMaskedPSO + MaterialRenderData::GetShadowSRB()
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
			packets.reserve(m_PassCtx.Visible_Shadow.size() * 2); // 섹션 고려

			auto& objs = scene.GetObjects();

			for (uint32 objIndex : m_PassCtx.Visible_Shadow)
			{
				if (objIndex >= static_cast<uint32>(objs.size()))
					continue;

				RenderScene::RenderObject& obj = objs[objIndex];

				if (!obj.bCastShadow)
					continue;

				const StaticMeshRenderData* mesh = m_PassCtx.pCache->TryGetStaticMeshRenderData(obj.MeshHandle);
				if (!mesh || !mesh->IsValid())
					continue;

				IBuffer* vb = mesh->GetVertexBuffer();
				IBuffer* ib = mesh->GetIndexBuffer();
				if (!vb || !ib)
					continue;

				const VALUE_TYPE indexType = mesh->GetIndexType();

				for (const auto& sec : mesh->GetSections())
				{
					if (sec.IndexCount == 0)
						continue;

					DrawPacket pkt = {};
					pkt.VertexBuffer = vb;
					pkt.IndexBuffer = ib;
					pkt.ObjectIndex = objIndex;

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
						if (slot >= static_cast<uint32>(obj.Materials.size()))
							continue;

						MaterialInstance* inst = &obj.Materials[slot];
						if (!inst)
							continue;

						const uint64 key = reinterpret_cast<uint64>(inst);
						auto itMat = m_PassCtx.FrameMat.find(key);
						if (itMat == m_PassCtx.FrameMat.end())
							continue;

						MaterialRenderData* rd = m_PassCtx.pCache->TryGetMaterialRenderData(itMat->second);
						if (!rd)
							continue;

						IShaderResourceBinding* maskedShadowSRB = rd->GetShadowSRB();
						if (!maskedShadowSRB)
							continue;

						pkt.PSO = shadowMaskedPSO;
						pkt.SRB = maskedShadowSRB;
					}

					if (!pkt.PSO || !pkt.SRB)
						continue;

					// --- sort keys (PSO -> SRB -> VB/IB -> draw range) ---
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
		// 4) Execute passes (scene 전달 X)
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

	bool Renderer::ensureObjectTableCapacity(uint32 objectCount)
	{
		IRenderDevice* dev = m_CreateInfo.pDevice.RawPtr();
		ASSERT(dev, "Device is null.");

		if (objectCount == 0)
			objectCount = 1;

		if (m_pObjectTableSB && m_ObjectTableCapacity >= objectCount)
			return true;

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
		desc.Size = uint64(desc.ElementByteStride) * uint64(newCap);

		RefCntAutoPtr<IBuffer> newBuf;
		dev->CreateBuffer(desc, nullptr, &newBuf);
		ASSERT(newBuf, "Object table create failed.");

		m_pObjectTableSB = newBuf;
		m_ObjectTableCapacity = newCap;
		m_PassCtx.pObjectTableSB = m_pObjectTableSB.RawPtr();

		if (m_pMaterialStaticBinder)
		{
			m_pMaterialStaticBinder->SetObjectTableSRV(m_pObjectTableSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
		}

		return true;
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
