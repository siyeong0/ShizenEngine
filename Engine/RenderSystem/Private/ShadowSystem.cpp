#include "pch.h"
#include "Engine/RenderSystem/Public/ShadowSystem.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassBuilder.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/Renderer/Public/RenderScene.h"

namespace shz
{
	// ------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------

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
		if (idx == 0)      A.CascadeCamSpaceZEnd0.x = z;
		else if (idx == 1) A.CascadeCamSpaceZEnd0.y = z;
		else if (idx == 2) A.CascadeCamSpaceZEnd0.z = z;
		else if (idx == 3) A.CascadeCamSpaceZEnd0.w = z;
		else if (idx == 4) A.CascadeCamSpaceZEnd1.x = z;
		else if (idx == 5) A.CascadeCamSpaceZEnd1.y = z;
		else if (idx == 6) A.CascadeCamSpaceZEnd1.z = z;
		else if (idx == 7) A.CascadeCamSpaceZEnd1.w = z;
	}

	void ShadowSystem::ComputeCascadeSplits(
		float camNear,
		float camFar,
		uint32 numCascades,
		float lambda,
		float* outSplitZ)
	{
		// Practical split scheme:
		// split_i = lerp(linear_i, log_i, lambda)
		const float n = std::max(camNear, 1e-4f);
		const float f = std::max(camFar, n + 1e-3f);

		for (uint32 i = 0; i < numCascades; ++i)
		{
			const float p = float(i + 1) / float(numCascades);

			const float linearZ = n + (f - n) * p;
			const float logZ = n * std::pow(f / n, p);

			const float z = lambda * (logZ - linearZ) + linearZ;
			outSplitZ[i] = (i == numCascades - 1) ? f : z;
		}
	}

	static void BuildFrustumCornersWS_ForZRange(
		const Matrix4x4& cameraWorld,
		const Matrix4x4& invProj,
		float zNear,
		float zFar,
		float3 outCornersWS[8])
	{
		const float2 ndcXY[4] =
		{
			float2(-1, -1),
			float2(+1, -1),
			float2(-1, +1),
			float2(+1, +1),
		};

		float3 dirVS[4]{};

		for (int i = 0; i < 4; ++i)
		{
			// clip (x,y,1,1) -> view
			const float4 pVS4 = float4(ndcXY[i].x, ndcXY[i].y, 1.0f, 1.0f) * invProj;
			const float3 pVS = pVS4 / std::max(pVS4.w, 1e-6f);

			// view-space ray direction (camera at origin in VS)
			dirVS[i] = pVS.Normalized();
		}

		// Intersect with planes z = zNear/zFar in view-space (LH, +Z forward)
		for (int i = 0; i < 4; ++i)
		{
			const float denom = std::max(dirVS[i].z, 1e-6f);

			const float tN = zNear / denom;
			const float tF = zFar / denom;

			const float3 pNvs = dirVS[i] * tN;
			const float3 pFvs = dirVS[i] * tF;

			outCornersWS[i + 0] = cameraWorld.TransformPosition(pNvs); // near 0..3
			outCornersWS[i + 4] = cameraWorld.TransformPosition(pFvs); // far  4..7
		}
	}

	void ShadowSystem::BuildCascade_FrustumFitStabilized(
		uint32 cascadeIdx,
		const float3 frustumCornersWS[8],
		const float3& lightDirWs,
		float shadowMapRes)
	{
		auto& A = m_ShadowAttribs;
		auto& C = GetCascadeRef(A, cascadeIdx);

		// 1) Cascade center + radius (world)
		float3 centerWs = float3(0, 0, 0);
		for (int i = 0; i < 8; ++i)
			centerWs += frustumCornersWS[i];
		centerWs *= (1.0f / 8.0f);

		float radius = 0.0f;
		for (int i = 0; i < 8; ++i)
			radius = std::max(radius, (frustumCornersWS[i] - centerWs).Length());
		radius = std::max(radius, 1.0f);

		// 2) Light view (per-cascade)
		const float3 lightForward = lightDirWs.Normalized();

		float3 up = float3(0, 1, 0);
		if (Abs(Vector3::Dot(up, lightForward)) > 0.99f)
			up = float3(0, 0, 1);

		// Eye distance: radius + padding (keeps light-space Z range sane)
		const float eyeDist = radius + 50.0f;
		const float3 lightPosWs = centerWs - lightForward * eyeDist;

		const Matrix4x4 lightView = Matrix4x4::LookAtLH(lightPosWs, centerWs, up);
		C.WorldToLightView = lightView;

		// 3) Transform frustum corners to light space, build AABB
		float minX = +FLT_MAX, minY = +FLT_MAX, minZ = +FLT_MAX;
		float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

		for (int i = 0; i < 8; ++i)
		{
			const float3 pLS = lightView.TransformPosition(frustumCornersWS[i]);

			minX = std::min(minX, pLS.x);
			minY = std::min(minY, pLS.y);
			minZ = std::min(minZ, pLS.z);

			maxX = std::max(maxX, pLS.x);
			maxY = std::max(maxY, pLS.y);
			maxZ = std::max(maxZ, pLS.z);
		}

		minZ -= m_CI.CasterDistancePadding; // push zn away from casters to prevent clipping (camera-space padding transformed to light-space by enlarging Z range)

		// 4) Z padding
		const float zPad = std::max(m_CI.ZPadding, 0.0f);
		minZ -= zPad;
		maxZ += zPad;

		// guard
		if (maxZ < minZ + 1e-3f)
		{
			const float mid = 0.5f * (minZ + maxZ);
			minZ = mid - 1.0f;
			maxZ = mid + 1.0f;
		}

		// 5) Stabilization (XY)
		float extentX = std::max(maxX - minX, 1e-3f);
		float extentY = std::max(maxY - minY, 1e-3f);

		if (m_CI.EqualizeExtents)
		{
			const float e = std::max(extentX, extentY);
			extentX = e;
			extentY = e;

			const float cx = 0.5f * (minX + maxX);
			const float cy = 0.5f * (minY + maxY);

			minX = cx - 0.5f * extentX;
			maxX = cx + 0.5f * extentX;
			minY = cy - 0.5f * extentY;
			maxY = cy + 0.5f * extentY;
		}

		const float res = shadowMapRes;
		float unitsPerTexelX = extentX / res;
		float unitsPerTexelY = extentY / res;
		float unitsPerTexel = std::max(unitsPerTexelX, unitsPerTexelY);
		unitsPerTexel = std::max(unitsPerTexel, 1e-6f);

		// Center snap in light space
		if (m_CI.SnapCascades)
		{
			const float cx = 0.5f * (minX + maxX);
			const float cy = 0.5f * (minY + maxY);

			const float scx = std::floor(cx / unitsPerTexel + 0.5f) * unitsPerTexel;
			const float scy = std::floor(cy / unitsPerTexel + 0.5f) * unitsPerTexel;

			const float dx = scx - cx;
			const float dy = scy - cy;

			minX += dx; maxX += dx;
			minY += dy; maxY += dy;
		}

		// Quantize extents to texel multiples (prevents subtle breathing)
		if (m_CI.QuantizeExtents)
		{
			float ex = std::max(maxX - minX, 1e-3f);
			float ey = std::max(maxY - minY, 1e-3f);

			const float qx = std::ceil(ex / unitsPerTexel) * unitsPerTexel;
			const float qy = std::ceil(ey / unitsPerTexel) * unitsPerTexel;

			const float cx = 0.5f * (minX + maxX);
			const float cy = 0.5f * (minY + maxY);

			minX = cx - 0.5f * qx;
			maxX = cx + 0.5f * qx;
			minY = cy - 0.5f * qy;
			maxY = cy + 0.5f * qy;
		}

		// 6) Fill scale/bias for shader mapping (posProj = posLS * scale + bias)
		const float invW = 1.0f / std::max(maxX - minX, 1e-6f);
		const float invH = 1.0f / std::max(maxY - minY, 1e-6f);
		const float invD = 1.0f / std::max(maxZ - minZ, 1e-6f);

		const float sx = 2.0f * invW;
		const float sy = 2.0f * invH;
		const float sz = 1.0f * invD;

		const float bx = -(maxX + minX) * invW;
		const float by = -(maxY + minY) * invH;
		const float bz = -minZ * invD;

		C.LightSpaceScale = float4(sx, sy, sz, 0.0f);
		C.LightSpaceScaledBias = float4(bx, by, bz, 0.0f);

		// PCF footprint margin in NDC ([-1..1])
		const float pcfRadiusTexels = 1.0f; // 3x3 => 1, 5x5 => 2
		const float marginXY = (pcfRadiusTexels * 2.0f) / std::max(res, 1.0f);
		C.MarginProjSpace = float4(marginXY, marginXY, 0.0f, 0.0f);

		const Matrix4x4 lightProj = Matrix4x4::OrthoOffCenter(
			minX, maxX,
			minY, maxY,
			minZ, maxZ);

		View& v = m_CascadeViews[cascadeIdx];
		v.PrevViewMatrix = v.ViewMatrix;
		v.PrevProjMatrix = v.ProjMatrix;
		v.PrevViewProjMatrix = v.ViewProjMatrix;

		v.ViewMatrix = lightView;
		v.ProjMatrix = lightProj;
		v.ViewProjMatrix = lightView * lightProj;

		v.CameraPosition = lightPosWs;

		v.FieldOfViewY = 0.0f;
		v.AspectRatio = 1.0f;

		v.Viewport =
		{
			0, 0,
			static_cast<int32>(m_CI.ShadowMapResolution),
			static_cast<int32>(m_CI.ShadowMapResolution)
		};

		v.NearPlane = minZ;
		v.FarPlane = maxZ;

		v.bOrthographic = true;
		v.OrthographicSize = std::max(maxX - minX, maxY - minY);

		ViewFrustumExt fr{};
		ExtractViewFrustumPlanesFromMatrix(v.ViewProjMatrix, fr);
		m_CascadeFrustums[cascadeIdx] = fr;
	}

	// ------------------------------------------------------------
	// API: getters
	// ------------------------------------------------------------
	const View& ShadowSystem::GetCascadeView(uint32 idx) const
	{
		ASSERT(idx < m_CI.NumCascades, "GetCascadeView: idx OOB.");
		return m_CascadeViews[idx];
	}

	const ViewFrustumExt& ShadowSystem::GetCascadeFrustum(uint32 idx) const
	{
		ASSERT(idx < m_CI.NumCascades, "GetCascadeFrustum: idx OOB.");
		return m_CascadeFrustums[idx];
	}

	// ------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------
	void ShadowSystem::Initialize(Renderer& renderer, const CreateInfo& ci)
	{
		m_CI = ci;
		m_CI.NumCascades = std::min(m_CI.NumCascades, MAX_CASCADES);

		m_ShadowMapTexId = STRING_HASH("ShadowMapArray");
		m_ShadowMapSRVId = STRING_HASH("ShadowMapArray_SRV");
		m_ShadowAttribsCBId = STRING_HASH("SHADOW_MAP_ATTRIBS");

		for (uint32 i = 0; i < MAX_CASCADES; ++i)
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

		const float sm = (float)m_CI.ShadowMapResolution;
		m_ShadowAttribs.ShadowMapDim = float4(sm, sm, 1.0f / sm, 1.0f / sm);

		m_ShadowAttribs.CascadeTransitionRegion = m_CI.CascadeTransitionRegion;
		m_ShadowAttribs.ReceiverPlaneDepthBiasClamp = m_CI.ReceiverPlaneDepthBiasClamp;
		m_ShadowAttribs.FixedDepthBias = m_CI.FixedDepthBias;
		m_ShadowAttribs.FixedFilterSize = m_CI.FixedFilterSize;
		m_ShadowAttribs.MaxAnisotropy = 1;
		m_ShadowAttribs.FilterWorldSize = 0.0f;

		for (uint32 c = 0; c < MAX_CASCADES; ++c)
			SetCascadeCamSpaceZEnd(m_ShadowAttribs, c, FLT_MAX);

		// clear cache
		for (uint32 c = 0; c < MAX_CASCADES; ++c)
		{
			m_CascadeViews[c] = View{};
			m_CascadeFrustums[c] = ViewFrustumExt{};
		}
	}

	void ShadowSystem::Shutdown()
	{
		// no-op
	}

	void ShadowSystem::UpdateShadowMatrices(Renderer& renderer, const View& mainView, const float3& lightDirWs)
	{
		const Matrix4x4 cameraView = mainView.ViewMatrix;
		const Matrix4x4 cameraProj = mainView.ProjMatrix;
		const Matrix4x4 cameraWorld = cameraView.Inversed(); // affine ok

		const float camNear = mainView.NearPlane;

		// ------------------------------------------------------------
		// IMPORTANT: shadow visible distance = 500m (or ci.ShadowFarDistance)
		// ------------------------------------------------------------
		const float camFar = std::max(m_CI.ShadowFarDistance, camNear + 1.0f);

		// 1) splits
		float zEnds[MAX_CASCADES]{};
		ComputeCascadeSplits(camNear, camFar, m_CI.NumCascades, m_CI.PartitioningFactor, zEnds);

		// 2) fill per cascade
		float prevEnd = camNear;
		for (uint32 c = 0; c < m_CI.NumCascades; ++c)
		{
			const float zEnd = zEnds[c];

			auto& C = GetCascadeRef(m_ShadowAttribs, c);
			C.StartEndZ = float4(prevEnd, zEnd, 0, 0);
			SetCascadeCamSpaceZEnd(m_ShadowAttribs, c, zEnd);

			const Matrix4x4 invProj = cameraProj.Inversed();

			float3 cornersWS[8]{};
			BuildFrustumCornersWS_ForZRange(cameraWorld, invProj, prevEnd, zEnd, cornersWS);

			BuildCascade_FrustumFitStabilized(
				c, cornersWS, lightDirWs, (float)m_CI.ShadowMapResolution);

			prevEnd = zEnd;
		}

		for (uint32 c = m_CI.NumCascades; c < MAX_CASCADES; ++c)
		{
			SetCascadeCamSpaceZEnd(m_ShadowAttribs, c, FLT_MAX);
			auto& C = GetCascadeRef(m_ShadowAttribs, c);
			C.WorldToLightView = Matrix4x4::Identity();
			C.LightSpaceScale = float4(0, 0, 0, 0);
			C.LightSpaceScaledBias = float4(0, 0, 0, 0);
			C.StartEndZ = float4(0, 0, 0, 0);
			C.MarginProjSpace = float4(0, 0, 0, 0);

			m_CascadeViews[c] = View{};
			m_CascadeFrustums[c] = ViewFrustumExt{};
		}

		// Upload
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

					b.DeclareBufferIndirectArgsRead(STRING_HASH("IndirectArgsBuffer"));
					b.DeclareBufferIndirectArgsRead(STRING_HASH("IndirectDrawCountBuffer"));
				},
				[this, c](RenderPassContext& ctx)
				{
					ASSERT(ctx.pImmediateContext, "Context is null.");
					ASSERT(ctx.pScene, "Scene is null.");
					ASSERT(ctx.pRegistry, "Registry is null.");

					IDeviceContext* pContext = ctx.pImmediateContext;

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

					// ------------------------------------------------------------
					// NEW: cascade별 ObjectTable pack (필수!)
					// ------------------------------------------------------------
					IBuffer* pShadowObjectTable = ctx.pRegistry->GetBuffer(STRING_HASH("ShadowPassObjectTable"));
					ASSERT(pShadowObjectTable, "ShadowPassObjectTable missing.");

					const std::vector<uint32>& remap = ctx.ShadowCascadeInstanceRemaps[c];
					const std::vector<hlsl::ObjectConstants>& tableCPU = ctx.pScene->GetObjectConstantsTableCPU();

					{
						MapHelper<hlsl::ObjectConstants> map(pContext, pShadowObjectTable, MAP_WRITE, MAP_FLAG_DISCARD);
						hlsl::ObjectConstants* dst = map;

						for (size_t i = 0; i < remap.size(); ++i)
						{
							const uint32 oc = remap[i];
							ASSERT(oc < (uint32)tableCPU.size(), "Shadow remap OOB.");
							dst[i] = tableCPU[oc];
						}
					}

					const std::vector<DrawPacket>& packets = ctx.ShadowCascadeDrawPackets[c];

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

						// per-draw constants (StartInstanceLocation = objectTable packed offset)
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

					// Indirect는 현재 구조상 cascade별 CPU cull이 어려워서 그대로 실행(원하면 확장)
					for (const DrawIndirectPacket& pkt : ctx.ShadowIndirectPackets)
					{
						ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid indirect packet.");

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
							uint64 offs[] = { 0 };
							pContext->SetVertexBuffers(
								0, 1, ppVB, offs,
								RESOURCE_STATE_TRANSITION_MODE_VERIFY,
								SET_VERTEX_BUFFERS_FLAG_RESET);
							pLastVB = pkt.VertexBuffer;
						}

						if (pLastIB != pkt.IndexBuffer)
						{
							pContext->SetIndexBuffer(pkt.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
							pLastIB = pkt.IndexBuffer;
						}

						DrawIndexedIndirectAttribs dia = pkt.DrawAttribs;

						{
							MapHelper<hlsl::DrawConstants> map(
								pContext,
								ctx.pRegistry->GetBuffer(STRING_HASH("DRAW_CONSTANTS")),
								MAP_WRITE,
								MAP_FLAG_DISCARD);

							map->StartInstanceLocation = pkt.StartInstanceLocation;
						}

						pContext->DrawIndexedIndirect(dia);
					}
				});
		}
	}
} // namespace shz