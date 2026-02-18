// Engine/RenderSystem/Private/ShadowSystem.cpp

#include "pch.h"
#include "Engine/RenderSystem/Public/ShadowSystem.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassBuilder.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/RHI/Interface/GraphicsTypes.h"

namespace shz
{
	// ------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------
	void ShadowSystem::BuildLightViewBasis(const float3& lightDirWs, float3& X, float3& Y, float3& Z)
	{
		Z = lightDirWs.Normalized();

		float3 up = float3(0, 1, 0);
		if (Abs(Vector3::Dot(up, Z)) > 0.99f)
			up = float3(0, 0, 1);

		X = Vector3::Cross(up, Z).Normalized();
		Y = Vector3::Cross(Z, X).Normalized();
	}

	void ShadowSystem::GetFrustumMinimumBoundingSphere(
		float proj11, float proj22,
		float nearZ, float farZ,
		float3& outCenterView,
		float& outRadius)
	{
		const float tanHalfFovX = 1.0f / std::max(proj11, 1e-6f);
		const float tanHalfFovY = 1.0f / std::max(proj22, 1e-6f);
		const float tanHalfFov = std::max(tanHalfFovX, tanHalfFovY);

		const float nz = nearZ;
		const float fz = farZ;

		const float rFar = fz * tanHalfFov;
		const float rNear = nz * tanHalfFov;

		const float cz = 0.5f * (nz + fz);
		outCenterView = float3(0.0f, 0.0f, cz);

		outRadius = std::sqrt(rFar * rFar + (fz - cz) * (fz - cz));
		outRadius = std::max(outRadius, std::sqrt(rNear * rNear + (cz - nz) * (cz - nz)));
	}

	hlsl::CascadeAttribs& ShadowSystem::GetCascadeRef(hlsl::ShadowMapAttribs& A, uint32 idx)
	{
		switch (idx)
		{
		default:
		case 0: return A.Cascades0;
		case 1: return A.Cascades1;
		case 2: return A.Cascades2;
		case 3: return A.Cascades3;
		case 4: return A.Cascades4;
		case 5: return A.Cascades5;
		case 6: return A.Cascades6;
		case 7: return A.Cascades7;
		}
	}

	void ShadowSystem::SetCascadeCamSpaceZEnd(hlsl::ShadowMapAttribs& A, uint32 idx, float z)
	{
		// (주의) HLSL 쪽은 CascadeCamSpaceZEnd0/1 고정 필드 형태여야 함.
		if (idx == 0)      A.CascadeCamSpaceZEnd0.x = z;
		else if (idx == 1) A.CascadeCamSpaceZEnd0.y = z;
		else if (idx == 2) A.CascadeCamSpaceZEnd0.z = z;
		else if (idx == 3) A.CascadeCamSpaceZEnd0.w = z;
		else if (idx == 4) A.CascadeCamSpaceZEnd1.x = z;
		else if (idx == 5) A.CascadeCamSpaceZEnd1.y = z;
		else if (idx == 6) A.CascadeCamSpaceZEnd1.z = z;
		else if (idx == 7) A.CascadeCamSpaceZEnd1.w = z;
	}

	// ------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------
	void ShadowSystem::Initialize(Renderer& renderer, const CreateInfo& ci)
	{
		m_CI = ci;
		m_CI.NumCascades = std::min(m_CI.NumCascades, kMaxCascades);

		m_ShadowMapTexId = STRING_HASH("ShadowMapArray");
		m_ShadowMapSRVId = STRING_HASH("ShadowMapArray_SRV");
		m_ShadowAttribsCBId = STRING_HASH("SHADOW_MAP_ATTRIBS");

		for (uint32 i = 0; i < kMaxCascades; ++i)
		{
			char name[64]{};
			std::snprintf(name, sizeof(name), "ShadowMapArray_Cascade%u_DSV", i);
			m_ShadowMapCascadeDSVIds[i] = STRING_HASH(name);
		}

		// Shadow map array
		{
			TextureDesc td = {};
			td.Name = "ShadowMapArray";
			td.Type = RESOURCE_DIM_TEX_2D_ARRAY;
			td.Width = m_CI.ShadowMapResolution;
			td.Height = m_CI.ShadowMapResolution;
			td.MipLevels = 1;
			td.ArraySize = static_cast<uint32>(m_CI.NumCascades);
			td.SampleCount = 1;
			td.Usage = USAGE_DEFAULT;
			td.Format = TEX_FORMAT_R32_TYPELESS;
			td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

			renderer.AddTexture(m_ShadowMapTexId, renderer.CreateTexture(td));

			// per-slice DSV
			for (uint32 c = 0; c < m_CI.NumCascades; ++c)
			{
				TextureViewDesc dsv = {};
				dsv.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
				dsv.Format = TEX_FORMAT_D32_FLOAT;
				dsv.FirstArraySlice = c;
				dsv.NumArraySlices = 1;

				renderer.AddTextureView(m_ShadowMapTexId, m_ShadowMapCascadeDSVIds[c], dsv);
			}

			// array SRV
			{
				TextureViewDesc srv = {};
				srv.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
				srv.Format = TEX_FORMAT_R32_FLOAT;
				srv.FirstArraySlice = 0;
				srv.NumArraySlices = static_cast<uint32>(m_CI.NumCascades);

				renderer.AddTextureView(m_ShadowMapTexId, m_ShadowMapSRVId, srv);
			}

			renderer.RegisterStaticTextureResource("g_ShadowMapArray", m_ShadowMapTexId);
		}

		// Shadow attribs CB
		{
			BufferDesc bd = {};
			bd.Name = "SHADOW_MAP_ATTRIBS";
			bd.Usage = USAGE_DYNAMIC;
			bd.BindFlags = BIND_UNIFORM_BUFFER;
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;
			bd.Size = sizeof(hlsl::ShadowMapAttribs);

			renderer.AddBuffer(m_ShadowAttribsCBId, bd);
			renderer.RegisterStaticBufferCBV("SHADOW_MAP_ATTRIBS", m_ShadowAttribsCBId);
		}

		// init defaults
		std::memset(&m_ShadowAttribs, 0, sizeof(m_ShadowAttribs));
		m_ShadowAttribs.NumCascades = (int)m_CI.NumCascades;
		m_ShadowAttribs.NumCascadesF = (float)m_CI.NumCascades;

		// init z-ends to +inf
		for (uint32 c = 0; c < kMaxCascades; ++c)
			SetCascadeCamSpaceZEnd(m_ShadowAttribs, c, FLT_MAX);
	}

	void ShadowSystem::Shutdown()
	{
		// no-op (registry owns resources)
	}

	// ------------------------------------------------------------
	// Distribute cascades (CHANGED: WorldToLightView + scale/bias must match your stable LightView/Ortho)
	// ------------------------------------------------------------
	void ShadowSystem::DistributeCascades(
		const Matrix4x4& cameraView,
		const Matrix4x4& cameraProj,
		const Matrix4x4& cameraWorld,
		const float3& lightDirWs)
	{
		auto& A = m_ShadowAttribs;

		A.NumCascades = (int)m_CI.NumCascades;
		A.NumCascadesF = (float)m_CI.NumCascades;

		const float sm = (float)m_CI.ShadowMapResolution;
		A.ShadowMapDim = float4(sm, sm, 1.0f / sm, 1.0f / sm);

		A.CascadeTransitionRegion = m_CI.CascadeTransitionRegion;
		A.ReceiverPlaneDepthBiasClamp = m_CI.ReceiverPlaneDepthBiasClamp;
		A.FixedDepthBias = m_CI.FixedDepthBias;
		A.FixedFilterSize = m_CI.FixedFilterSize;
		A.MaxAnisotropy = 1;
		A.FilterWorldSize = 0.0f;

		// ------------------------------------------------------------
		// 0) Stable-light params (MUST match your renderer code)
		// ------------------------------------------------------------
		const float ShadowHalfExtent = 100.0f;
		const float ShadowDepth = 200.0f;
		const float PadZ = 30.0f;

		const float WorldMinY = -500.0f;
		const float WorldMaxY = 1000.0f;

		const float3 lightForward = lightDirWs.Normalized();

		float3 up = float3(0, 1, 0);
		if (Abs(Vector3::Dot(up, lightForward)) > 0.99f)
			up = float3(0, 0, 1);

		// ------------------------------------------------------------
		// 1) unitsPerTexel + centerWs quantization (match exactly)
		// ------------------------------------------------------------
		const float extentXY = ShadowHalfExtent * 2.0f;
		const float unitsPerTexel = extentXY / std::max(sm, 1.0f);

		// NOTE: use cameraWorld translation -> camera position (to match your main code)
		const float3 cameraPosWs = float3(cameraWorld._m30, cameraWorld._m31, cameraWorld._m32);

		float3 centerWs = cameraPosWs;
		centerWs.x = std::floor(centerWs.x / unitsPerTexel + 0.5f) * unitsPerTexel;
		centerWs.z = std::floor(centerWs.z / unitsPerTexel + 0.5f) * unitsPerTexel;

		// ------------------------------------------------------------
		// 2) light view (match)
		// ------------------------------------------------------------
		const float3 lightPosWs = centerWs - lightForward * ShadowDepth;
		const Matrix4x4 lightView = Matrix4x4::LookAtLH(lightPosWs, centerWs, up);

		// IMPORTANT:
		// g_ShadowAttribs.WorldToLightView must be THIS FULL view matrix (with translation),
		// because shader does: mul(worldPos, WorldToLightView).
		A.WorldToLightView = lightView;

		// ------------------------------------------------------------
		// 3) Ortho window XY (match) + optional snap in light-space (match)
		// ------------------------------------------------------------
		float minX = -ShadowHalfExtent;
		float maxX = +ShadowHalfExtent;
		float minY = -ShadowHalfExtent;
		float maxY = +ShadowHalfExtent;

		// ------------------------------------------------------------
		// 4) near/far from world Y bounds projected (match) then override (match)
		// ------------------------------------------------------------
		float nearZ = +FLT_MAX;
		float farZ = -FLT_MAX;

		const float x0 = centerWs.x - ShadowHalfExtent;
		const float x1 = centerWs.x + ShadowHalfExtent;
		const float z0 = centerWs.z - ShadowHalfExtent;
		const float z1 = centerWs.z + ShadowHalfExtent;

		const float3 samplesWS[8] =
		{
			float3(x0, WorldMinY, z0),
			float3(x1, WorldMinY, z0),
			float3(x0, WorldMinY, z1),
			float3(x1, WorldMinY, z1),

			float3(x0, WorldMaxY, z0),
			float3(x1, WorldMaxY, z0),
			float3(x0, WorldMaxY, z1),
			float3(x1, WorldMaxY, z1),
		};

		for (int i = 0; i < 8; ++i)
		{
			const float4 pLs4 = float4(samplesWS[i], 1.0f) * lightView;
			nearZ = Min(nearZ, pLs4.z);
			farZ = Max(farZ, pLs4.z);
		}

		nearZ -= PadZ;
		farZ += PadZ;

		if (farZ < nearZ + 1.0f)
		{
			const float mid = 0.5f * (nearZ + farZ);
			nearZ = mid - 1.0f;
			farZ = mid + 1.0f;
		}

		// match your final override EXACTLY
		nearZ = -PadZ;
		farZ = ShadowDepth * 3.0f + PadZ;

		// optional: projection window snap in light-space (match your step #4)
		{
			const float4 centerLs4 = float4(centerWs, 1.0f) * lightView;
			const float2 centerLs = float2(centerLs4.x, centerLs4.y);

			const float2 snapped =
			{
				std::floor(centerLs.x / unitsPerTexel + 0.5f) * unitsPerTexel,
				std::floor(centerLs.y / unitsPerTexel + 0.5f) * unitsPerTexel
			};

			const float2 delta = snapped - centerLs;

			minX += delta.x; maxX += delta.x;
			minY += delta.y; maxY += delta.y;
		}

		// ------------------------------------------------------------
		// 5) Fill cascade 0 mapping from this OrthoOffCenter
		//    NOTE: We are NOT changing your pass ShadowView/proj here.
		//          This only builds scale/bias mapping consistent with OrthoOffCenter.
		// ------------------------------------------------------------
		{
			auto& C = GetCascadeRef(A, 0);

			// StartEndZ is "camera view-space" partitioning; keep your existing distribution logic.
			// We'll compute all zEnds below; this block only fills scale/bias/margins for cascade0.
			const float sx = 2.0f / std::max(maxX - minX, 1e-6f);
			const float sy = 2.0f / std::max(maxY - minY, 1e-6f);
			const float bx = -(maxX + minX) / std::max(maxX - minX, 1e-6f);
			const float by = -(maxY + minY) / std::max(maxY - minY, 1e-6f);

			// D3D depth [0..1] for Ortho:
			// z_ndc = (z - near) / (far - near)
			const float sz = 1.0f / std::max(farZ - nearZ, 1e-6f);
			const float bz = -nearZ / std::max(farZ - nearZ, 1e-6f);

			C.LightSpaceScale = float4(sx, sy, sz, 0.0f);
			C.LightSpaceScaledBias = float4(bx, by, bz, 0.0f);
			C.MarginProjSpace = float4(0, 0, 0, 0);
		}

		// ------------------------------------------------------------
		// 6) Camera Z partition (unchanged logic) -> fills StartEndZ + CascadeCamSpaceZEnd
		// ------------------------------------------------------------
		const float camNear = 0.1f;
		const float camFar = 1000.0f;

		float zEnds[kMaxCascades]{};
		for (uint32 c = 0; c < m_CI.NumCascades; ++c)
		{
			const float prevEnd = (c == 0) ? camNear : zEnds[c - 1];
			const float p = float(c + 1) / float(m_CI.NumCascades);

			float z;
			if (c < m_CI.NumCascades - 1)
			{
				const float ratio = camFar / std::max(camNear, 1e-6f);
				const float logZ = camNear * std::pow(ratio, p);
				const float linearZ = camNear + (camFar - camNear) * p;
				z = m_CI.PartitioningFactor * (logZ - linearZ) + linearZ;
			}
			else
			{
				z = camFar;
			}

			zEnds[c] = z;

			auto& C = GetCascadeRef(A, c);
			C.StartEndZ = float4(prevEnd, z, 0, 0);

			SetCascadeCamSpaceZEnd(A, c, z);
		}

		for (uint32 c = m_CI.NumCascades; c < kMaxCascades; ++c)
			SetCascadeCamSpaceZEnd(A, c, FLT_MAX);

		// ------------------------------------------------------------
		// 7) Other cascades (1..N-1):
		//    We keep same "stable shadow" method but expand extents/depth per cascade.
		//    (If you currently draw all cascades with SAME lightView/proj, then set NumCascades=1)
		// ------------------------------------------------------------
		for (uint32 c = 1; c < m_CI.NumCascades; ++c)
		{
			// simple growth
			const float scale = std::pow(2.0f, (float)c); // 2^c
			const float halfExtentC = ShadowHalfExtent * scale;
			const float depthC = ShadowDepth * scale;

			float minXc = -halfExtentC;
			float maxXc = +halfExtentC;
			float minYc = -halfExtentC;
			float maxYc = +halfExtentC;

			// keep the SAME unitsPerTexel snap base (important for stability)
			// but if you want per-cascade texel snap, you can compute unitsPerTexelC here.

			// reuse the same light-space snap based on centerWs
			{
				const float4 centerLs4 = float4(centerWs, 1.0f) * lightView;
				const float2 centerLs = float2(centerLs4.x, centerLs4.y);

				const float2 snapped =
				{
					std::floor(centerLs.x / unitsPerTexel + 0.5f) * unitsPerTexel,
					std::floor(centerLs.y / unitsPerTexel + 0.5f) * unitsPerTexel
				};

				const float2 delta = snapped - centerLs;
				minXc += delta.x; maxXc += delta.x;
				minYc += delta.y; maxYc += delta.y;
			}

			// near/far: follow your final override style
			const float nearZc = -PadZ;
			const float farZc = depthC * 3.0f + PadZ;

			auto& C = GetCascadeRef(A, c);

			const float sx = 2.0f / std::max(maxXc - minXc, 1e-6f);
			const float sy = 2.0f / std::max(maxYc - minYc, 1e-6f);
			const float bx = -(maxXc + minXc) / std::max(maxXc - minXc, 1e-6f);
			const float by = -(maxYc + minYc) / std::max(maxYc - minYc, 1e-6f);

			const float sz = 1.0f / std::max(farZc - nearZc, 1e-6f);
			const float bz = -nearZc / std::max(farZc - nearZc, 1e-6f);

			C.LightSpaceScale = float4(sx, sy, sz, 0.0f);
			C.LightSpaceScaledBias = float4(bx, by, bz, 0.0f);
			C.MarginProjSpace = float4(0, 0, 0, 0);
		}

		// clear remaining cascades
		for (uint32 c = m_CI.NumCascades; c < kMaxCascades; ++c)
		{
			auto& C = GetCascadeRef(A, c);
			C.LightSpaceScale = float4(0, 0, 0, 0);
			C.LightSpaceScaledBias = float4(0, 0, 0, 0);
			C.StartEndZ = float4(0, 0, 0, 0);
			C.MarginProjSpace = float4(0, 0, 0, 0);
		}
	}

	void ShadowSystem::UpdateShadowMatrices(Renderer& renderer, const View& mainView, const float3& lightDirWs)
	{
		const Matrix4x4 cameraView = mainView.ViewMatrix;
		const Matrix4x4 cameraProj = mainView.ProjMatrix;
		const Matrix4x4 cameraWorld = cameraView.Inversed();

		DistributeCascades(cameraView, cameraProj, cameraWorld, lightDirWs);

		// upload CB (HLSL struct 그대로)
		renderer.UpdateBuffer<hlsl::ShadowMapAttribs>(m_ShadowAttribsCBId, m_ShadowAttribs);
	}

	// ------------------------------------------------------------
	// RenderGraph passes
	// ------------------------------------------------------------
	void ShadowSystem::InstallPasses(Renderer& renderer)
	{
		for (uint32 c = 0; c < m_CI.NumCascades; ++c)
		{
			char passName[64]{};
			std::snprintf(passName, sizeof(passName), "Shadow.Cascade%u", c);

			const uint64 dsvId = m_ShadowMapCascadeDSVIds[c];

			renderer.AddPass(
				passName,
				EPassExecutionDomain::RenderPass,
				[dsvId](RenderPassBuilder& b)
				{
					b.DeclareTextureDSVWrite(dsvId);
					b.SetClearDepthStencil(dsvId, 1.f, 0);
				},
				[this, c](RenderPassContext& ctx)
				{
					ASSERT(ctx.pImmediateContext, "Context is null.");
					IDeviceContext* pContext = ctx.pImmediateContext;

					// viewport
					Viewport vp = {};
					vp.Width = float(m_CI.ShadowMapResolution);
					vp.Height = float(m_CI.ShadowMapResolution);
					vp.MinDepth = 0.f;
					vp.MaxDepth = 1.f;
					pContext->SetViewports(1, &vp, 0, 0);

					// ShadowConstants (cascade index)
					{
						MapHelper<hlsl::ShadowConstants> map(
							pContext,
							ctx.pRegistry->GetBuffer(STRING_HASH("SHADOW_CONSTANTS")),
							MAP_WRITE,
							MAP_FLAG_DISCARD);

						map->CascadeIndex = c;
					}

					const std::vector<DrawPacket>& packets = ctx.ShadowDrawPackets;

					IPipelineState* pLastPSO = nullptr;
					IShaderResourceBinding* pLastSRB = nullptr;
					IBuffer* pLastVB = nullptr;
					IBuffer* pLastIB = nullptr;

					for (const DrawPacket& pkt : packets)
					{
						if (!pkt.PSO || !pkt.SRB || !pkt.VertexBuffer || !pkt.IndexBuffer)
							continue;

						if (pLastPSO != pkt.PSO)
						{
							pLastPSO = pkt.PSO;
							pLastSRB = nullptr;
							pContext->SetPipelineState(pLastPSO);
						}

						if (pLastSRB != pkt.SRB)
						{
							pLastSRB = pkt.SRB;
							pContext->CommitShaderResources(pLastSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
						}

						if (pLastVB != pkt.VertexBuffer)
						{
							IBuffer* ppVB[] = { pkt.VertexBuffer };
							uint64 offsets[] = { 0 };
							pContext->SetVertexBuffers(
								0, 1, ppVB, offsets,
								RESOURCE_STATE_TRANSITION_MODE_VERIFY,
								SET_VERTEX_BUFFERS_FLAG_RESET);
							pLastVB = pkt.VertexBuffer;
						}

						if (pLastIB != pkt.IndexBuffer)
						{
							pContext->SetIndexBuffer(pkt.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
							pLastIB = pkt.IndexBuffer;
						}

						// per-draw constants
						{
							MapHelper<hlsl::DrawConstants> map(
								pContext,
								ctx.pRegistry->GetBuffer(STRING_HASH("DRAW_CONSTANTS")),
								MAP_WRITE,
								MAP_FLAG_DISCARD);

							map->StartInstanceLocation = pkt.StartInstanceLocation;
							map->_pad0 = 0;
						}

						pContext->DrawIndexed(pkt.DrawAttribs);
					}
				});
		}
	}
} // namespace shz
