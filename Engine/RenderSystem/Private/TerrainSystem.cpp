#include "pch.h"
#include "Engine/RenderSystem/Public/TerrainSystem.h"

#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassBuilder.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	struct TerrainVertex final
	{
		float3 Pos; // x,z in [0..16]
		float2 UV;
		float3 Normal;
		float3 Tangent;
	};

	enum ETerrainStitchMask : uint8
	{
		Stitch_None = 0,
		Stitch_Left = 1 << 0,
		Stitch_Right = 1 << 1,
		Stitch_Bottom = 1 << 2,
		Stitch_Top = 1 << 3,
	};

	// ------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------
	static inline float u16ToNormalized(uint16 v) noexcept
	{
		return static_cast<float>(v) * (1.0f / 65535.0f);
	}

	static inline uint16 normalizedToU16(float n) noexcept
	{
		const float c = Clamp01(n);
		const float scaled = c * 65535.0f;
		const uint32 iv = static_cast<uint32>(scaled + 0.5f);
		return static_cast<uint16>(iv > 65535u ? 65535u : iv);
	}

	static void buildGrid17x17VB(std::vector<TerrainVertex>& outVerts)
	{
		outVerts.clear();
		outVerts.reserve(17 * 17);

		for (uint32 z = 0; z < 17; ++z)
		{
			for (uint32 x = 0; x < 17; ++x)
			{
				const float u = float(x) / 16.0f;
				const float v = float(z) / 16.0f;

				TerrainVertex vtx = {};
				vtx.Pos = float3{ float(x), 0.0f, float(z) };
				vtx.UV = float2{ u, v };
				vtx.Normal = float3{ 0.0f, 1.0f, 0.0f }; // Not used
				vtx.Tangent = float3{ 1.0f, 0.0f, 0.0f }; // Not used
				outVerts.emplace_back(vtx);
			}
		}
	}

	static void buildGridIndicesLOD_Stitched(uint32 step, uint8 stitchMask, std::vector<uint16>& outIdxU16)
	{
		outIdxU16.clear();

		ASSERT(step == 1 || step == 2 || step == 4 || step == 8 || step == 16, "Invalid step.");
		ASSERT((16 % step) == 0, "Step must divide 16.");

		const uint32 vertsPerSide = 17;
		const uint32 quadsPerSide = 16 / step;

		auto vid = [&](uint32 gx, uint32 gz) -> uint16
		{
			ASSERT(gx <= 16 && gz <= 16, "Grid index out of range.");
			return uint16(gz * vertsPerSide + gx);
		};

		// ------------------------------------------------------------
		// Winding stabilizer: enforce same winding as (0,0)->(s,0)->(0,s)
		// ------------------------------------------------------------
		const int32 refSign = +1;

		auto cross2XZSign = [&](uint32 ax, uint32 az, uint32 bx, uint32 bz, uint32 cx, uint32 cz) -> int32
		{
			const int32 x1 = int32(bx) - int32(ax);
			const int32 z1 = int32(bz) - int32(az);
			const int32 x2 = int32(cx) - int32(ax);
			const int32 z2 = int32(cz) - int32(az);

			const int32 c = x1 * z2 - z1 * x2;
			return (c > 0) ? +1 : (c < 0 ? -1 : 0);
		};

		auto pushTriGrid = [&](uint32 ax, uint32 az, uint32 bx, uint32 bz, uint32 cx, uint32 cz)
		{
			uint16 a = vid(ax, az);
			uint16 b = vid(bx, bz);
			uint16 c = vid(cx, cz);

			const int32 sgn = cross2XZSign(ax, az, bx, bz, cx, cz);
			if (sgn != 0 && sgn != refSign)
			{
				std::swap(b, c);
			}

			outIdxU16.push_back(a);
			outIdxU16.push_back(b);
			outIdxU16.push_back(c);
		};

		auto pushQuadStd = [&](uint32 x0, uint32 z0)
		{
			const uint32 x1 = x0 + step;
			const uint32 z1 = z0 + step;

			pushTriGrid(x0, z0, x1, z0, x0, z1);
			pushTriGrid(x1, z0, x1, z1, x0, z1);
		};

		// step==16 (LOD4): single quad only
		if (step == 16)
		{
			outIdxU16.reserve(6);
			pushQuadStd(0, 0);
			ASSERT((outIdxU16.size() % 3) == 0, "Index count must be multiple of 3.");
			return;
		}

		const bool bStitchL = (stitchMask & Stitch_Left) != 0;
		const bool bStitchR = (stitchMask & Stitch_Right) != 0;
		const bool bStitchB = (stitchMask & Stitch_Bottom) != 0;
		const bool bStitchT = (stitchMask & Stitch_Top) != 0;

		const uint32 s = step;
		const uint32 s2 = step * 2;

		// rough upper bound
		outIdxU16.reserve(quadsPerSide * quadsPerSide * 6 + quadsPerSide * 12);

		// ------------------------------------------------------------
		// A) Interior (exclude outer ring; edges handled separately)
		// ------------------------------------------------------------
		if (quadsPerSide > 2)
		{
			for (uint32 qz = 1; qz < quadsPerSide - 1; ++qz)
			{
				for (uint32 qx = 1; qx < quadsPerSide - 1; ++qx)
				{
					pushQuadStd(qx * step, qz * step);
				}
			}
		}

		// ------------------------------------------------------------
		// Stitch strip emitters (diff=1 assumed)
		// IMPORTANT:
		// - Boundary uses ONLY even vertices (0,2s,4s..)
		// - Inner line uses (0,s,2s) to connect without T-junction
		// ------------------------------------------------------------
		auto emitStitchStripX = [&](uint32 zBoundary, uint32 zInner, uint32 xBegin, uint32 xEnd)
		{
			// xBegin/xEnd in grid space, must be multiples of 2s, inclusive range [xBegin .. xEnd] where xEnd<=16-2s
			for (uint32 x = xBegin; (x + s2) <= xEnd; x += s2)
			{
				const uint32 x0 = x;
				const uint32 x1 = x + s;
				const uint32 x2 = x + s2;

				// Triangulate 2s segment with 3 tris:
				// boundary edge uses (x0,zB) -> (x2,zB)
				// inner row uses (x0,zI),(x1,zI),(x2,zI)
				pushTriGrid(x0, zBoundary, x2, zBoundary, x1, zInner); // A C E
				pushTriGrid(x0, zBoundary, x1, zInner, x0, zInner);  // A E D
				pushTriGrid(x2, zBoundary, x2, zInner, x1, zInner);  // C F E
			}
		};

		auto emitStitchStripZ = [&](uint32 xBoundary, uint32 xInner, uint32 zBegin, uint32 zEnd)
		{
			for (uint32 z = zBegin; (z + s2) <= zEnd; z += s2)
			{
				const uint32 z0 = z;
				const uint32 z1 = z + s;
				const uint32 z2 = z + s2;

				pushTriGrid(xBoundary, z0, xBoundary, z2, xInner, z1); // A C E
				pushTriGrid(xBoundary, z0, xInner, z1, xInner, z0); // A E D
				pushTriGrid(xBoundary, z2, xInner, z2, xInner, z1); // C F E
			}
		};

		// ------------------------------------------------------------
		// Corner patch:
		// When two stitched edges meet, we draw ONE 2s¡¿2s quad using only even boundary verts.
		// This avoids overlap / weird triangles / T-junction at the corner.
		// ------------------------------------------------------------
		const bool bCornerBL = bStitchB && bStitchL;
		const bool bCornerBR = bStitchB && bStitchR;
		const bool bCornerTL = bStitchT && bStitchL;
		const bool bCornerTR = bStitchT && bStitchR;

		// Emit corner patches first (purely deterministic)
		if (bCornerBL)
		{
			uint32 x0 = 0; uint32 z0 = 0;
			uint32 x1 = x0 + s; uint32 z1 = z0 + s;
			uint32 x2 = x0 + s2; uint32 z2 = z0 + s2;
			// big
			pushTriGrid(x0, z0, x1, z1, x0, z2);
			pushTriGrid(x0, z0, x1, z1, x2, z0);
			// small
			pushTriGrid(x2, z0, x1, z1, x2, z1);
			pushTriGrid(x0, z2, x1, z1, x1, z2);
		}
		if (bCornerBR)
		{
			uint32 x0 = 16 - s2; uint32 z0 = 0;
			uint32 x1 = x0 + s; uint32 z1 = z0 + s;
			uint32 x2 = x0 + s2; uint32 z2 = z0 + s2;
			// big
			pushTriGrid(x0, z0, x1, z1, x2, z0);
			pushTriGrid(x2, z2, x1, z1, x2, z0);
			// small
			pushTriGrid(x0, z0, x1, z1, x0, z1);
			pushTriGrid(x1, z2, x1, z1, x2, z2);
		}
		if (bCornerTL)
		{
			uint32 x0 = 0; uint32 z0 = 16 - s2;
			uint32 x1 = x0 + s; uint32 z1 = z0 + s;
			uint32 x2 = x0 + s2; uint32 z2 = z0 + s2;
			// big
			pushTriGrid(x0, z0, x1, z1, x0, z2);
			pushTriGrid(x2, z2, x1, z1, x0, z2);
			// small
			pushTriGrid(x0, z0, x1, z1, x1, z0);
			pushTriGrid(x2, z1, x1, z1, x2, z2);
		}
		if (bCornerTR)
		{
			uint32 x0 = 16 - s2; uint32 z0 = 16 - s2;
			uint32 x1 = x0 + s; uint32 z1 = z0 + s;
			uint32 x2 = x0 + s2; uint32 z2 = z0 + s2;
			// big
			pushTriGrid(x2, z2, x1, z1, x0, z2);
			pushTriGrid(x2, z2, x1, z1, x2, z0);
			// small
			pushTriGrid(x0, z1, x1, z1, x0, z2);
			pushTriGrid(x1, z0, x1, z1, x2, z0);
		}

		// ------------------------------------------------------------
		// B) Bottom edge
		// ------------------------------------------------------------
		if (!bStitchB)
		{
			// If left/right are stitched, bottom must NOT use odd boundary verts at that corner.
			// So skip the corner quad and let left/right stitch handle it (or corner patch if both stitched).
			const uint32 qxBegin = bStitchL ? 1u : 0u;
			const uint32 qxEnd = bStitchR ? (quadsPerSide - 1u) : quadsPerSide;

			for (uint32 qx = qxBegin; qx < qxEnd; ++qx)
			{
				pushQuadStd(qx * step, 0);
			}
		}
		else
		{
			// Skip corner spans if corner patch exists (2s region is owned by patch)
			const uint32 xBegin = bCornerBL ? s2 : 0u;
			const uint32 xEnd = bCornerBR ? (16 - s2) : 16u;
			emitStitchStripX(/*zBoundary=*/0, /*zInner=*/s, xBegin, xEnd);
		}

		// ------------------------------------------------------------
		// C) Top edge
		// ------------------------------------------------------------
		if (!bStitchT)
		{
			const uint32 qxBegin = bStitchL ? 1u : 0u;
			const uint32 qxEnd = bStitchR ? (quadsPerSide - 1u) : quadsPerSide;

			for (uint32 qx = qxBegin; qx < qxEnd; ++qx)
			{
				pushQuadStd(qx * step, 16 - step);
			}
		}
		else
		{
			const uint32 xBegin = bCornerTL ? s2 : 0u;
			const uint32 xEnd = bCornerTR ? (16 - s2) : 16u;
			emitStitchStripX(/*zBoundary=*/16, /*zInner=*/16 - s, xBegin, xEnd);
		}

		// ------------------------------------------------------------
		// D) Left edge
		// ------------------------------------------------------------
		if (quadsPerSide >= 2)
		{
			if (!bStitchL)
			{
				// Left non-stitch: ALWAYS exclude corners to avoid double-emission with Bottom/Top.
				const uint32 qzBegin = 1u;
				const uint32 qzEnd = (quadsPerSide > 1) ? (quadsPerSide - 1u) : 0u;

				for (uint32 qz = qzBegin; qz < qzEnd; ++qz)
				{
					pushQuadStd(0, qz * step);
				}
			}
			else
			{
				const uint32 zBegin = bCornerBL ? s2 : 0u;
				const uint32 zEnd = bCornerTL ? (16 - s2) : 16u;
				emitStitchStripZ(/*xBoundary=*/0, /*xInner=*/s, zBegin, zEnd);
			}
		}

		// ------------------------------------------------------------
		// E) Right edge
		// ------------------------------------------------------------
		if (quadsPerSide >= 2)
		{
			if (!bStitchR)
			{
				// Right non-stitch: ALWAYS exclude corners to avoid double-emission with Bottom/Top.
				const uint32 qzBegin = 1u;
				const uint32 qzEnd = (quadsPerSide > 1) ? (quadsPerSide - 1u) : 0u;

				for (uint32 qz = qzBegin; qz < qzEnd; ++qz)
				{
					pushQuadStd(16 - step, qz * step);
				}
			}
			else
			{
				const uint32 zBegin = bCornerBR ? s2 : 0u;
				const uint32 zEnd = bCornerTR ? (16 - s2) : 16u;
				emitStitchStripZ(/*xBoundary=*/16, /*xInner=*/16 - s, zBegin, zEnd);
			}
		}

		ASSERT((outIdxU16.size() % 3) == 0, "Index count must be multiple of 3.");
	}

	// ------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------
	void TerrainSystem::Initialize(AssetManager& assetManager, const CreateInfo& ci)
	{
		Cleanup();
		m_CI = ci;

		ASSERT(!m_CI.HeightMapPath.empty(), "TerrainSystem HeightMapPath is empty.");
		ASSERT(m_CI.WorldSpacingX > 0.f && m_CI.WorldSpacingZ > 0.f, "Invalid spacing.");
		ASSERT(m_CI.HeightScale >= 0.f, "HeightScale must be >= 0.");

		m_ChunkSize = m_CI.ChunkSize;
		m_WorldSpacingX = m_CI.WorldSpacingX;
		m_WorldSpacingZ = m_CI.WorldSpacingZ;
		m_HeightScale = m_CI.HeightScale;
		m_HeightOffset = m_CI.HeightOffset;
		m_bCenterXZ = m_CI.bCenterXZ;

		// Height texture (CPU)
		m_HeightTexRef = assetManager.RegisterAsset<Texture>(m_CI.HeightMapPath);
		m_HeightTex = assetManager.LoadBlocking<Texture>(m_HeightTexRef);
		ASSERT(m_HeightTex && m_HeightTex->IsValid(), "Failed to load height Texture asset.");

		// Optional diffuse (CPU)
		if (!m_CI.DiffusePath.empty())
		{
			m_DiffuseTexRef = assetManager.RegisterAsset<Texture>(m_CI.DiffusePath);
		}

		// CPU height array
		buildHeightU16FromHeightTexture(*m_HeightTex);
		ASSERT(m_Width > 0 && m_Height > 0, "Terrain height texture has invalid dimensions.");
		ASSERT(m_HeightU16.size() == size_t(m_Width) * size_t(m_Height), "Height data size mismatch.");
	}

	void TerrainSystem::Cleanup()
	{
		m_CI = {};

		m_Width = 0;
		m_Height = 0;

		m_ChunkSize = 64.0f;
		m_WorldSpacingX = 1.0f;
		m_WorldSpacingZ = 1.0f;

		m_HeightScale = 100.0f;
		m_HeightOffset = 0.0f;

		m_bCenterXZ = true;

		m_HeightTexRef = {};
		m_DiffuseTexRef = {};

		m_HeightTex.Reset();

		m_HeightU16.clear();
		m_HeightU16.shrink_to_fit();

		m_pGridVB.Release();

		for (uint32 lod = 0; lod < 5; ++lod)
		{
			for (uint32 mask = 0; mask < 16; ++mask)
			{
				m_pLodIB[lod][mask].Release();
				m_LodIndexCount[lod][mask] = 0;
			}
		}
	}

	float TerrainSystem::GetWorldOriginX() const noexcept
	{
		return m_bCenterXZ ? (-0.5f * GetWorldSizeX()) : 0.0f;
	}

	float TerrainSystem::GetWorldOriginZ() const noexcept
	{
		return m_bCenterXZ ? (-0.5f * GetWorldSizeZ()) : 0.0f;
	}

	// ------------------------------------------------------------
	// Pass registration (feature-owned)
	// ------------------------------------------------------------
	void TerrainSystem::InstallPasses(Renderer& renderer)
	{
		renderer.AddPass(
			"TerrainGBuffer",
			[](RenderPassBuilder& b)
			{
				const uint64 kAlbedo = STRING_HASH("GBuffer0_Albedo");
				const uint64 kNormal = STRING_HASH("GBuffer1_Normal");
				const uint64 kMRAO = STRING_HASH("GBuffer2_MRAO");
				const uint64 kEmissive = STRING_HASH("GBuffer3_Emissive");
				const uint64 kDepth = STRING_HASH("GBufferDepth");

				b.DeclareTextureRTVWrite(kAlbedo);
				b.DeclareTextureRTVWrite(kNormal);
				b.DeclareTextureRTVWrite(kMRAO);
				b.DeclareTextureRTVWrite(kEmissive);
				b.DeclareTextureDSVWrite(kDepth);
				b.DeclareTextureSRVRead(STRING_HASH("HeightField"));

				b.SetClearColor(kAlbedo, 0.f, 0.f, 0.f, 0.f);
				b.SetClearColor(kNormal, 0.f, 0.f, 0.f, 0.f);
				b.SetClearColor(kMRAO, 0.f, 0.f, 0.f, 0.f);
				b.SetClearColor(kEmissive, 0.f, 0.f, 0.f, 0.f);
				b.SetClearDepthStencil(kDepth, 1.f, 0);
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");

				IDeviceContext* pCtx = ctx.pImmediateContext;

				const float worldOriginX = GetWorldOriginX();
				const float worldOriginZ = GetWorldOriginZ();
				const float worldSizeX = GetWorldSizeX();
				const float worldSizeZ = GetWorldSizeZ();

				const uint32 numChunksX = uint32(std::ceil(worldSizeX / m_ChunkSize));
				const uint32 numChunksZ = uint32(std::ceil(worldSizeZ / m_ChunkSize));
				if (numChunksX == 0 || numChunksZ == 0)
					return;

				pCtx->SetPipelineState(m_TerrainBinding.pPSO);
				pCtx->CommitShaderResources(m_TerrainBinding.pSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				{
					IBuffer* vbs[] = { m_pGridVB };
					uint64 offs[] = { 0 };
					pCtx->SetVertexBuffers(
						0, 1, vbs, offs,
						RESOURCE_STATE_TRANSITION_MODE_VERIFY,
						SET_VERTEX_BUFFERS_FLAG_RESET);
				}

				ASSERT(ctx.pViewFamily, "ViewFamily is null.");
				const ViewFamily& viewFamily = *ctx.pViewFamily;
				ASSERT(!viewFamily.Views.empty(), "ViewFamily has no views.");
				const View& view = viewFamily.Views[0];

				ViewFrustumExt frustumMain = {};
				{
					const Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix;
					ExtractViewFrustumPlanesFromMatrix(viewProj, frustumMain);
				}

				auto idx2D = [&](uint32 cx, uint32 cz) -> uint32
				{
					return cz * numChunksX + cx;
				};

				struct LodRange final { float End; };
				const LodRange lodRanges[5] =
				{
					{ 128.0f },
					{ 256.0f },
					{ 512.0f },
					{ 1024.0f },
					{ FLT_MAX },
				};

				auto selectLodOnly = [&](float dist) -> uint32
				{
					if (dist < lodRanges[0].End) return 0;
					if (dist < lodRanges[1].End) return 1;
					if (dist < lodRanges[2].End) return 2;
					if (dist < lodRanges[3].End) return 3;
					return 4;
				};

				auto morphForClampedLod = [&](float dist, uint32 lod) -> float
				{
					if (lod >= 4)
						return 0.0f;

					const float d1 = lodRanges[lod].End;
					const float d0 = d1 * 0.85f;

					const float t = Clamp01((dist - d0) / Max(1e-6f, (d1 - d0)));
					const float s = t * t * (3.0f - 2.0f * t); // smoothstep
					return s;
				};

				std::vector<uint8> lodGrid;
				lodGrid.resize(size_t(numChunksX) * size_t(numChunksZ), 4u);

				{
					// const float3 cam = view.CameraPosition;
					const float3 cam = float3{0.0f, 300.0f, 0.0f};

					for (uint32 cz = 0; cz < numChunksZ; ++cz)
					{
						for (uint32 cx = 0; cx < numChunksX; ++cx)
						{
							const float chunkOriginX = worldOriginX + float(cx) * m_ChunkSize;
							const float chunkOriginZ = worldOriginZ + float(cz) * m_ChunkSize;

							const float remainX = worldOriginX + worldSizeX - chunkOriginX;
							const float remainZ = worldOriginZ + worldSizeZ - chunkOriginZ;

							const float chunkSizeX = (remainX > 0.f) ? Min(m_ChunkSize, remainX) : 0.f;
							const float chunkSizeZ = (remainZ > 0.f) ? Min(m_ChunkSize, remainZ) : 0.f;

							if (chunkSizeX <= 1e-6f || chunkSizeZ <= 1e-6f)
							{
								lodGrid[idx2D(cx, cz)] = 4;
								continue;
							}

							const float cxw = chunkOriginX + 0.5f * chunkSizeX;
							const float czw = chunkOriginZ + 0.5f * chunkSizeZ;

							const float dx = cam.x - cxw;
							const float dz = cam.z - czw;
							const float dist = std::sqrt(dx * dx + dz * dz);

							const uint32 lod = selectLodOnly(dist);
							lodGrid[idx2D(cx, cz)] = uint8(Clamp(lod, 0u, 4u));
						}
					}
				}

				{
					bool changed = true;
					uint32 iter = 0;

					while (changed && iter++ < 16)
					{
						changed = false;

						for (uint32 cz = 0; cz < numChunksZ; ++cz)
						{
							for (uint32 cx = 0; cx < numChunksX; ++cx)
							{
								uint8& a = lodGrid[idx2D(cx, cz)];

								auto clampPair = [&](uint32 nx, uint32 nz)
								{
									if (nx >= numChunksX || nz >= numChunksZ)
										return;

									uint8& b = lodGrid[idx2D(nx, nz)];

									if (b > uint8(a + 1)) { b = uint8(a + 1); changed = true; }
									if (a > uint8(b + 1)) { a = uint8(b + 1); changed = true; }
								};

								if (cx > 0) clampPair(cx - 1, cz);
								if (cx + 1 < numChunksX) clampPair(cx + 1, cz);
								if (cz > 0) clampPair(cx, cz - 1);
								if (cz + 1 < numChunksZ) clampPair(cx, cz + 1);
							}
						}
					}
				}

				DrawIndexedAttribs dia = {};
				dia.IndexType = VT_UINT16;
				dia.Flags = DRAW_FLAG_VERIFY_ALL;

				uint32 currentLod = 0xFFFFFFFFu;
				uint32 currentMask = 0xFFFFFFFFu;

				const float yMin = m_HeightOffset;
				const float yMax = m_HeightOffset + m_HeightScale;

				const float3 cam = view.CameraPosition;

				for (uint32 cz = 0; cz < numChunksZ; ++cz)
				{
					for (uint32 cx = 0; cx < numChunksX; ++cx)
					{
						const float chunkOriginX = worldOriginX + float(cx) * m_ChunkSize;
						const float chunkOriginZ = worldOriginZ + float(cz) * m_ChunkSize;

						const float remainX = worldOriginX + worldSizeX - chunkOriginX;
						const float remainZ = worldOriginZ + worldSizeZ - chunkOriginZ;

						const float chunkSizeX = (remainX > 0.f) ? Min(m_ChunkSize, remainX) : 0.f;
						const float chunkSizeZ = (remainZ > 0.f) ? Min(m_ChunkSize, remainZ) : 0.f;

						if (chunkSizeX <= 1e-6f || chunkSizeZ <= 1e-6f)
							continue;

						Box localBounds = {};
						localBounds.Min = float3{ 0.f, yMin, 0.f };
						localBounds.Max = float3{ chunkSizeX, yMax, chunkSizeZ };

						Matrix4x4 chunkWorld = Matrix4x4::Translation(float3{ chunkOriginX, 0.f, chunkOriginZ });

						if (!IntersectsFrustum(frustumMain, localBounds, chunkWorld, FRUSTUM_PLANE_FLAG_FULL_FRUSTUM))
							continue;

						const uint32 lod = uint32(lodGrid[idx2D(cx, cz)]);
						ASSERT(lod < 5, "Invalid LOD.");

						uint8 mask = Stitch_None;

						auto nLod = [&](int32 nx, int32 nz) -> uint32
						{
							if (nx < 0 || nz < 0 || nx >= int32(numChunksX) || nz >= int32(numChunksZ))
								return lod;
							return uint32(lodGrid[idx2D(uint32(nx), uint32(nz))]);
						};

						if (nLod(int32(cx) - 1, int32(cz)) > lod) mask |= Stitch_Left;
						if (nLod(int32(cx) + 1, int32(cz)) > lod) mask |= Stitch_Right;
						if (nLod(int32(cx), int32(cz) - 1) > lod) mask |= Stitch_Bottom;
						if (nLod(int32(cx), int32(cz) + 1) > lod) mask |= Stitch_Top;

						if (lod != currentLod || uint32(mask) != currentMask)
						{
							currentLod = lod;
							currentMask = uint32(mask);

							RefCntAutoPtr<IBuffer>& ib = m_pLodIB[lod][mask];
							ASSERT(ib, "Terrain IB is null (lod=%u mask=%u).", lod, uint32(mask));

							pCtx->SetIndexBuffer(ib, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
							dia.NumIndices = m_LodIndexCount[lod][mask];
						}

						const float cxw = chunkOriginX + 0.5f * chunkSizeX;
						const float czw = chunkOriginZ + 0.5f * chunkSizeZ;

						const float dx = cam.x - cxw;
						const float dz = cam.z - czw;
						const float dist = std::sqrt(dx * dx + dz * dz);

						const float morph = morphForClampedLod(dist, lod);

						hlsl::TerrainDrawConstants dc = {};
						dc.ChunkOriginXZ = float2{ chunkOriginX, chunkOriginZ };
						dc.ChunkSizeXZ = float2{ chunkSizeX,   chunkSizeZ };

						dc.HeightUVScale = float2{ 1, 1 };
						dc.HeightUVBias = float2{ 0, 0 };
						dc.SurfaceUVScale = float2{ 1, 1 };
						dc.SurfaceUVBias = float2{ 0, 0 };

						const float normalSteps[5] = { 1.f, 2.f, 4.f, 8.f, 16.f };
						dc.NormalSampleStep = normalSteps[lod];

						dc.LodIndex = lod;
						dc.LodMorphAlpha = morph;

						// Debug
						auto hash01 = [](uint32 v) -> float
						{
							v ^= v >> 16; v *= 0x7feb352d; v ^= v >> 15; v *= 0x846ca68b; v ^= v >> 16; return float(v & 0x00FFFFFFu) / 16777216.0f;
						};

						uint32 h = (cx + 1) * 73856093u ^ (cz + 1) * 19349663u;
						//uint32 h = lod;
						float r = 0.25f + 0.75f * hash01(h ^ 0x1111u);
						float g = 0.25f + 0.75f * hash01(h ^ 0x2222u);
						float b = 0.25f + 0.75f * hash01(h ^ 0x3333u);
						dc.DebugChunkColor = float4{ r, g, b, 1.0f };


						MapHelper<hlsl::TerrainDrawConstants> map(
							pCtx,
							m_TerrainBinding.Buffers["TERRAIN_DRAW_CONSTANTS"].pBuffer,
							MAP_WRITE,
							MAP_FLAG_DISCARD);

						*map = dc;

						pCtx->DrawIndexed(dia);
					}
				}
			},
			[this, &renderer]()
			{
				// VB
				{
					std::vector<TerrainVertex> verts;
					buildGrid17x17VB(verts);

					BufferDesc vb = {};
					vb.Name = "Terrain.Grid17x17.VB";
					vb.Usage = USAGE_IMMUTABLE;
					vb.BindFlags = BIND_VERTEX_BUFFER;
					vb.Size = uint32(verts.size() * sizeof(TerrainVertex));

					BufferData init = {};
					init.pData = verts.data();
					init.DataSize = vb.Size;

					m_pGridVB = renderer.CreateVertexBuffer(vb, &init);
					ASSERT(m_pGridVB, "Create terrain VB failed.");
				}

				// IBs (LOD x StitchMask)
				const uint32 steps[5] = { 1, 2, 4, 8, 16 };
				for (uint32 lod = 0; lod < 5; ++lod)
				{
					for (uint32 mask = 0; mask < 16; ++mask)
					{
						std::vector<uint16> idx;
						buildGridIndicesLOD_Stitched(steps[lod], uint8(mask), idx);

						m_LodIndexCount[lod][mask] = uint32(idx.size());

						BufferDesc ib = {};
						std::string name =
							"Terrain.Grid17x17.IB.step" + std::to_string(steps[lod]) +
							".mask" + std::to_string(mask);
						ib.Name = name.c_str();
						ib.Usage = USAGE_IMMUTABLE;
						ib.BindFlags = BIND_INDEX_BUFFER;
						ib.Size = uint32(idx.size() * sizeof(uint16));

						BufferData init = {};
						init.pData = idx.data();
						init.DataSize = ib.Size;

						m_pLodIB[lod][mask] = renderer.CreateIndexBuffer(ib, &init);
						ASSERT(m_pLodIB[lod][mask], "Create terrain IB failed (lod=%u mask=%u).", lod, mask);
					}
				}

				{
					MaterialTemplate& matTmpl = renderer.CreateMaterialTemplate("Terrain", m_TerrainVS, m_TerrainPS);
					matTmpl.SetBufferDynamic("TERRAIN_DRAW_CONSTANTS", true);

					MaterialId matId = MaterialManager::GetInstance()->CreateMaterial("TerrainMaterial", "Terrain");
					Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
					mat.SetFloat4("g_BaseColorFactor", float4(1.0f, 1.0f, 1.0f, 1.0f));
					mat.SetFloat3("g_EmissiveFactor", float3(0.f, 0.f, 0.f));
					mat.SetFloat("g_EmissiveIntensity", 0.0f);
					mat.SetFloat("g_RoughnessFactor", 0.85f);
					mat.SetFloat("g_NormalScale", 1.0f);
					mat.SetFloat("g_OcclusionStrength", 1.0f);
					mat.SetFloat("g_AlphaCutoff", 0.5f);
					mat.SetFloat("g_MetallicFactor", 0.0f);
					if (m_DiffuseTexRef.IsValid())
					{
						mat.SetTextureAssetRef("g_BaseColorTex", MATERIAL_RESOURCE_TYPE_TEXTURE2D, m_DiffuseTexRef);
						mat.SetUint("g_MaterialFlags", 1);
					}
					else
					{
						mat.SetUint("g_MaterialFlags", 0);
					}

					m_TerrainBinding = renderer.AcquireMaterialPipelineBinding(matId, STRING_HASH("TerrainGBuffer"));
				}
			});
	}

	// ------------------------------------------------------------
	// CPU height build from Texture (base mip)
	// ------------------------------------------------------------
	static inline uint8  readR_U8(const uint8* p) noexcept { return p[0]; }
	static inline uint16 readR_U16(const uint16* p) noexcept { return p[0]; }
	static inline float  readR_F32(const float* p) noexcept { return p[0]; }

	void TerrainSystem::buildHeightU16FromHeightTexture(const Texture& heightTex)
	{
		ASSERT(heightTex.IsValid(), "Height texture is invalid.");
		ASSERT(!heightTex.GetMips().empty(), "Height texture has no mips.");

		const uint32 w = heightTex.GetWidth();
		const uint32 h = heightTex.GetHeight();
		ASSERT(w > 0 && h > 0, "Invalid height texture dimensions.");

		const TEXTURE_FORMAT fmt = heightTex.GetFormat();
		const TextureFormatAttribs& a = GetTextureFormatAttribs(fmt);

		ASSERT(a.NumComponents > 0 && a.ComponentSize > 0, "Invalid format attribs.");
		ASSERT(a.ComponentType != COMPONENT_TYPE_COMPRESSED, "Compressed formats are not supported.");

		const uint32 bytesPerPixel = a.NumComponents * a.ComponentSize;

		const TextureMip& mip0 = heightTex.GetMips()[0];
		ASSERT(mip0.Width == w && mip0.Height == h, "Mip0 size mismatch.");
		ASSERT(!mip0.Data.empty(), "Mip0 data empty.");

		const uint64 expectedMinBytes = uint64(w) * uint64(h) * uint64(bytesPerPixel);
		ASSERT(uint64(mip0.Data.size()) >= expectedMinBytes, "Mip0 data smaller than expected.");

		m_Width = w;
		m_Height = h;
		m_HeightU16.assign(size_t(w) * size_t(h), 0u);

		const uint8* src = mip0.Data.data();
		const uint64 rowStride = uint64(w) * uint64(bytesPerPixel);

		if (a.ComponentSize == 1)
		{
			const float inv = 1.f / 255.f;
			for (uint32 z = 0; z < h; ++z)
			{
				const uint8* row = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0; x < w; ++x)
				{
					const uint8* px = row + size_t(x) * bytesPerPixel;
					const uint8 r = readR_U8(px);
					m_HeightU16[size_t(z) * size_t(w) + x] = normalizedToU16(Clamp01(float(r) * inv));
				}
			}
		}
		else if (a.ComponentSize == 2)
		{
			const float inv = 1.f / 65535.f;
			for (uint32 z = 0; z < h; ++z)
			{
				const uint8* rowBytes = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0; x < w; ++x)
				{
					const uint16* px = reinterpret_cast<const uint16*>(rowBytes + size_t(x) * bytesPerPixel);
					const uint16 r = readR_U16(px);
					m_HeightU16[size_t(z) * size_t(w) + x] = normalizedToU16(Clamp01(float(r) * inv));
				}
			}
		}
		else if (a.ComponentSize == 4)
		{
			for (uint32 z = 0; z < h; ++z)
			{
				const uint8* rowBytes = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0; x < w; ++x)
				{
					const float* px = reinterpret_cast<const float*>(rowBytes + size_t(x) * bytesPerPixel);
					const float r = readR_F32(px);
					m_HeightU16[size_t(z) * size_t(w) + x] = normalizedToU16(Clamp01(r));
				}
			}
		}
		else
		{
			ASSERT(false, "Unsupported component size.");
		}
	}

	float2 TerrainSystem::WorldXZToDomainUV(const float2& worldXZ) const noexcept
	{
		const float originX = GetWorldOriginX();
		const float originZ = GetWorldOriginZ();

		const float sizeX = std::max(GetWorldSizeX(), 1e-6f);
		const float sizeZ = std::max(GetWorldSizeZ(), 1e-6f);

		const float u = (worldXZ.x - originX) / sizeX;
		const float v = (worldXZ.y - originZ) / sizeZ;
		return float2{ Clamp01(u), Clamp01(v) };
	}

	// ------------------------------------------------------------
	// Height access
	// ------------------------------------------------------------
	float TerrainSystem::GetNormalizedHeightAt(uint32 x, uint32 z) const
	{
		ASSERT(x < m_Width, "X out of range.");
		ASSERT(z < m_Height, "Z out of range.");

		return u16ToNormalized(m_HeightU16[size_t(z) * size_t(m_Width) + x]);
	}

	float TerrainSystem::GetWorldHeightAt(uint32 x, uint32 z) const
	{
		return m_HeightOffset + GetNormalizedHeightAt(x, z) * m_HeightScale;
	}

	float TerrainSystem::SampleNormalizedHeight(float worldX, float worldZ) const
	{
		ASSERT(m_WorldSpacingX > 0.f && m_WorldSpacingZ > 0.f, "Spacing must be > 0.");

		const float originX = GetWorldOriginX();
		const float originZ = GetWorldOriginZ();

		const float gx = (worldX - originX) / m_WorldSpacingX;
		const float gz = (worldZ - originZ) / m_WorldSpacingZ;

		const float maxX = float(m_Width - 1);
		const float maxZ = float(m_Height - 1);

		const float x = Clamp(gx, 0.f, maxX);
		const float z = Clamp(gz, 0.f, maxZ);

		const uint32 x0 = uint32(std::floor(x));
		const uint32 z0 = uint32(std::floor(z));

		const uint32 x1 = (x0 + 1 < m_Width) ? (x0 + 1) : x0;
		const uint32 z1 = (z0 + 1 < m_Height) ? (z0 + 1) : z0;

		const float tx = x - float(x0);
		const float tz = z - float(z0);

		const float h00 = GetNormalizedHeightAt(x0, z0);
		const float h10 = GetNormalizedHeightAt(x1, z0);
		const float h01 = GetNormalizedHeightAt(x0, z1);
		const float h11 = GetNormalizedHeightAt(x1, z1);

		const float hx0 = h00 + (h10 - h00) * tx;
		const float hx1 = h01 + (h11 - h01) * tx;
		return Clamp01(hx0 + (hx1 - hx0) * tz);
	}

	float TerrainSystem::SampleWorldHeight(float worldX, float worldZ) const
	{
		return m_HeightOffset + SampleNormalizedHeight(worldX, worldZ) * m_HeightScale;
	}

	void TerrainSystem::BuildPhysicsHeightSamples(std::vector<float>& outHeightsWorldMeters) const
	{
		outHeightsWorldMeters.resize(m_HeightU16.size());
		for (size_t i = 0; i < m_HeightU16.size(); ++i)
		{
			const float n = float(m_HeightU16[i]) / 65535.0f;
			outHeightsWorldMeters[i] = n * m_HeightScale + m_HeightOffset;
		}
	}
} // namespace shz
