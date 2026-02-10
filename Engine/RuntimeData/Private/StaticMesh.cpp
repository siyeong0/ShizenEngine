#include "pch.h"
#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

#include <limits>
#include <unordered_set>

namespace shz
{
	// ------------------------------------------------------------
	// Geometry setters
	// ------------------------------------------------------------
	void StaticMesh::ReserveVertices(uint32 count)
	{
		m_Positions.reserve(count);
		m_Normals.reserve(count);
		m_Tangents.reserve(count);
		m_TexCoords.reserve(count);
	}

	// ------------------------------------------------------------
	// Indices
	// ------------------------------------------------------------
	void StaticMesh::SetIndicesU32(std::vector<uint32>&& indices)
	{
		m_IndexType = VT_UINT32;
		m_IndicesU32 = std::move(indices);

		m_IndicesU16.clear();
	}

	void StaticMesh::SetIndicesU16(std::vector<uint16>&& indices)
	{
		m_IndexType = VT_UINT16;
		m_IndicesU16 = std::move(indices);

		m_IndicesU32.clear();
	}

	void StaticMesh::ApplyUniformScale(float s)
	{
		ASSERT(!m_Positions.empty(), "Mesh is not initialized.");
		ASSERT(s > 1e-6f && std::isfinite(s), "Invalid scale factor.");

		for (float3& p : m_Positions)
		{
			p *= s;
		}

		// Bounds become invalid after transform
		RecomputeBounds();
	}

	void StaticMesh::MoveBottomToOrigin(bool centerXZ)
	{
		ASSERT(!m_Positions.empty(), "Mesh is not initialized.");

		const Box& b = m_Bounds;
		float3 minV = b.Min;
		float3 maxV = b.Max;

		// Y: move lowest point to 0
		float3 offset = float3(0, -minV.y, 0);

		if (centerXZ)
		{
			// Put pivot at bottom-center in XZ (common for grass)
			float cx = 0.5f * (minV.x + maxV.x);
			float cz = 0.5f * (minV.z + maxV.z);
			offset.x = -cx;
			offset.z = -cz;
		}

		for (float3& p : m_Positions)
		{
			p += offset;
		}

		RecomputeBounds();
	}


	const void* StaticMesh::GetIndexData() const noexcept
	{
		if (m_IndexType == VT_UINT32)
		{
			if (m_IndicesU32.empty())
			{
				return nullptr;
			}
			return static_cast<const void*>(m_IndicesU32.data());
		}
		else
		{
			if (m_IndicesU16.empty())
			{
				return nullptr;
			}
			return static_cast<const void*>(m_IndicesU16.data());
		}
	}

	uint32 StaticMesh::GetIndexDataSizeBytes() const noexcept
	{
		if (m_IndexType == VT_UINT32)
		{
			const uint64 bytes = static_cast<uint64>(m_IndicesU32.size()) * sizeof(uint32);
			return static_cast<uint32>(bytes);
		}
		else
		{
			const uint64 bytes = static_cast<uint64>(m_IndicesU16.size()) * sizeof(uint16);
			return static_cast<uint32>(bytes);
		}
	}

	uint32 StaticMesh::GetIndexCount() const noexcept
	{
		if (m_IndexType == VT_UINT32)
		{
			return static_cast<uint32>(m_IndicesU32.size());
		}
		else
		{
			return static_cast<uint32>(m_IndicesU16.size());
		}
	}

	uint32 StaticMesh::GetIndexAt(uint32 i) const noexcept
	{
		if (m_IndexType == VT_UINT32)
		{
			return m_IndicesU32[i];
		}
		else
		{
			return static_cast<uint32>(m_IndicesU16[i]);
		}
	}

	// ------------------------------------------------------------
	// Material slots
	// ------------------------------------------------------------
	MaterialId& StaticMesh::GetMaterialSlot(uint32 slot) noexcept
	{
		ASSERT(slot < static_cast<uint32>(m_MaterialSlots.size()), "Material slot index out of range.");
		return m_MaterialSlots[slot];
	}

	const MaterialId& StaticMesh::GetMaterialSlot(uint32 slot) const noexcept
	{
		ASSERT(slot < static_cast<uint32>(m_MaterialSlots.size()), "Material slot index out of range.");
		return m_MaterialSlots[slot];
	}

	// ------------------------------------------------------------
	// Validation / policy
	// ------------------------------------------------------------
	bool StaticMesh::IsValid() const noexcept
	{
		// Positions are required.
		if (m_Positions.empty())
		{
			return false;
		}

		// Indices are required.
		if (GetIndexCount() == 0)
		{
			return false;
		}

		const size_t vtxCount = m_Positions.size();

		// Optional streams: if present, they must match vertex count.
		if (!m_Normals.empty())
		{
			if (m_Normals.size() != vtxCount)
			{
				return false;
			}
		}

		if (!m_Tangents.empty())
		{
			if (m_Tangents.size() != vtxCount)
			{
				return false;
			}
		}

		if (!m_TexCoords.empty())
		{
			if (m_TexCoords.size() != vtxCount)
			{
				return false;
			}
		}

		// Sections are optional. If provided, they must be in-range.
		const uint32 indexCount = GetIndexCount();
		for (const Section& sec : m_Sections)
		{
			if (sec.IndexCount == 0)
			{
				return false;
			}

			const uint64 end = static_cast<uint64>(sec.FirstIndex) + static_cast<uint64>(sec.IndexCount);
			if (end > static_cast<uint64>(indexCount))
			{
				return false;
			}

			// If materials exist, ensure section slot is within range.
			if (!m_MaterialSlots.empty())
			{
				if (sec.MaterialSlot >= static_cast<uint32>(m_MaterialSlots.size()))
				{
					return false;
				}
			}
		}

		return true;
	}

	bool StaticMesh::HasCPUData() const noexcept
	{
		if (m_Positions.empty())
		{
			return false;
		}

		if (GetIndexCount() == 0)
		{
			return false;
		}

		return true;
	}

	// ------------------------------------------------------------
	// Bounds
	// ------------------------------------------------------------
	void StaticMesh::RecomputeBounds()
	{
		if (m_Positions.empty())
		{
			m_Bounds = Box{};
			for (Section& sec : m_Sections)
			{
				sec.LocalBounds = Box{};
			}
			return;
		}

		float3 minV(
			+std::numeric_limits<float>::infinity(),
			+std::numeric_limits<float>::infinity(),
			+std::numeric_limits<float>::infinity());

		float3 maxV(
			-std::numeric_limits<float>::infinity(),
			-std::numeric_limits<float>::infinity(),
			-std::numeric_limits<float>::infinity());

		for (const float3& p : m_Positions)
		{
			if (p.x < minV.x) { minV.x = p.x; }
			if (p.y < minV.y) { minV.y = p.y; }
			if (p.z < minV.z) { minV.z = p.z; }

			if (p.x > maxV.x) { maxV.x = p.x; }
			if (p.y > maxV.y) { maxV.y = p.y; }
			if (p.z > maxV.z) { maxV.z = p.z; }
		}

		m_Bounds = Box(minV, maxV);

		RecomputeSectionBounds();
	}

	void StaticMesh::RecomputeSectionBounds()
	{
		if (m_Sections.empty())
		{
			return;
		}

		if (!HasCPUData())
		{
			for (Section& sec : m_Sections)
			{
				sec.LocalBounds = Box{};
			}
			return;
		}

		for (Section& sec : m_Sections)
		{
			if (sec.IndexCount == 0)
			{
				sec.LocalBounds = Box{};
				continue;
			}

			float3 minV(
				+std::numeric_limits<float>::infinity(),
				+std::numeric_limits<float>::infinity(),
				+std::numeric_limits<float>::infinity());

			float3 maxV(
				-std::numeric_limits<float>::infinity(),
				-std::numeric_limits<float>::infinity(),
				-std::numeric_limits<float>::infinity());

			const uint32 end = sec.FirstIndex + sec.IndexCount;
			for (uint32 i = sec.FirstIndex; i < end; ++i)
			{
				const uint32 idx = GetIndexAt(i);

				if (idx >= static_cast<uint32>(m_Positions.size()))
				{
					continue;
				}

				const float3& p = m_Positions[idx];

				if (p.x < minV.x) { minV.x = p.x; }
				if (p.y < minV.y) { minV.y = p.y; }
				if (p.z < minV.z) { minV.z = p.z; }

				if (p.x > maxV.x) { maxV.x = p.x; }
				if (p.y > maxV.y) { maxV.y = p.y; }
				if (p.z > maxV.z) { maxV.z = p.z; }
			}

			sec.LocalBounds = Box(minV, maxV);
		}
	}

	// ------------------------------------------------------------
	// Memory
	// ------------------------------------------------------------
	void StaticMesh::StripCPUData()
	{
		m_Positions.clear();
		m_Normals.clear();
		m_Tangents.clear();
		m_TexCoords.clear();

		m_IndicesU32.clear();
		m_IndicesU16.clear();
	}

	void StaticMesh::Clear()
	{
		m_Positions.clear();
		m_Normals.clear();
		m_Tangents.clear();
		m_TexCoords.clear();

		m_IndicesU32.clear();
		m_IndicesU16.clear();

		m_Sections.clear();
		m_MaterialSlots.clear();

		m_IndexType = VT_UINT32;
		m_Bounds = Box{};
	}


	static inline bool IsRGBA8(TEXTURE_FORMAT fmt)
	{
		return fmt == TEX_FORMAT_RGBA8_UNORM || fmt == TEX_FORMAT_RGBA8_UNORM_SRGB;
	}

	static inline float SignedArea(const std::vector<float2>& poly)
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
	}

	static inline bool PointInTri(const float2& p, const float2& a, const float2& b, const float2& c)
	{
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
	}

	static inline bool IsConvexCCW(const float2& prev, const float2& cur, const float2& next)
	{
		const float2 a = cur - prev;
		const float2 b = next - cur;
		const float cross = a.x * b.y - a.y * b.x;
		return cross > 0.0f;
	}

	static std::vector<float2> RdpSimplify(const std::vector<float2>& inPts, float epsilon)
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

		if ((int)out.size() < 3)
			return inPts;

		return out;
	}

	static bool EarClipTriangulate(const std::vector<float2>& polyCCW, std::vector<uint32>& outIndices)
	{
		const int n = (int)polyCCW.size();
		if (n < 3)
			return false;

		std::vector<int> V(n);
		for (int i = 0; i < n; ++i) V[i] = i;

		auto IsEar = [&](int iPrev, int iCur, int iNext) -> bool
		{
			const float2 a = polyCCW[iPrev];
			const float2 b = polyCCW[iCur];
			const float2 c = polyCCW[iNext];

			if (!IsConvexCCW(a, b, c))
				return false;

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
		while ((int)V.size() > 3 && guard++ < 100000)
		{
			bool clipped = false;
			const int m = (int)V.size();

			for (int i = 0; i < m; ++i)
			{
				const int iPrev = V[(i + m - 1) % m];
				const int iCur = V[i];
				const int iNext = V[(i + 1) % m];

				if (IsEar(iPrev, iCur, iNext))
				{
					outIndices.push_back((uint32)iPrev);
					outIndices.push_back((uint32)iCur);
					outIndices.push_back((uint32)iNext);

					V.erase(V.begin() + i);
					clipped = true;
					break;
				}
			}

			if (!clipped)
				return false;
		}

		if ((int)V.size() == 3)
		{
			outIndices.push_back((uint32)V[0]);
			outIndices.push_back((uint32)V[1]);
			outIndices.push_back((uint32)V[2]);
			return true;
		}

		return false;
	}

	// ------------------------------------------------------------
	// CreateBillboard (StaticMesh-level)
	// ------------------------------------------------------------
	StaticMesh CreateBillboard(AssetRef<Texture> texureRef, const std::string& templateName, MATERIAL_BLEND_MODE blendMode, float2 scale, float2 pivot)
	{
		ASSERT(scale.x > 0.0f && scale.y > 0.0f, "Scale value must be positive.");
		ASSERT(pivot.x >= 0.0f && pivot.y >= 0.0f && pivot.x <= 1.0f && pivot.y <= 1.0f,
			"pivot value range must be 0~1.");

		StaticMesh mesh;

		// We will always create one material slot for billboard.
		// (MaterialId default == 0 or some "invalid" is assumed; up to your engine)
		{
			std::vector<MaterialId> slots;
			slots.resize(1);
			mesh.SetMaterialSlots(std::move(slots));
		}

		// Try build cutout from CPU texture data.
		// NOTE: This is RuntimeData-level; we assume Texture asset is already CPU-resident/loaded somewhere else.
		// If you cannot access the texture here, keep this path disabled and always fallback to quad.
		const Texture* pTex = nullptr;
		{
			// If AssetRef can give you pointer directly in RuntimeData layer, use it.
			// Otherwise replace this with whatever is valid in your codebase.
			// e.g. pTex = texureRef.Get();   (if you have it)
			//
			// For now, we keep it null-safe: will fallback to quad if not available.
		}

		auto BuildQuad = [&]()
		{
			std::vector<float3> pos(4);
			std::vector<float2> uv(4);
			std::vector<uint32> idx = { 0, 1, 2, 0, 2, 3 };

			const float x0 = -pivot.x * scale.x;
			const float x1 = (1.0f - pivot.x) * scale.x;

			const float y0 = -pivot.y * scale.y;
			const float y1 = (1.0f - pivot.y) * scale.y;

			//  3 ---- 2
			//  |      |
			//  0 ---- 1
			// UV: (0,0)=top-left, (1,1)=bottom-right
			pos[0] = float3{ x0, y0, 0.0f }; uv[0] = float2{ 0.0f, 1.0f };
			pos[1] = float3{ x1, y0, 0.0f }; uv[1] = float2{ 1.0f, 1.0f };
			pos[2] = float3{ x1, y1, 0.0f }; uv[2] = float2{ 1.0f, 0.0f };
			pos[3] = float3{ x0, y1, 0.0f }; uv[3] = float2{ 0.0f, 0.0f };

			mesh.SetPositions(std::move(pos));
			mesh.SetTexCoords(std::move(uv));
			mesh.SetIndicesU32(std::move(idx));

			StaticMesh::Section sec = {};
			sec.FirstIndex = 0;
			sec.IndexCount = 6;
			sec.BaseVertex = 0;
			sec.MaterialSlot = 0;
			mesh.SetSections(std::vector<StaticMesh::Section>{ sec });

			mesh.RecomputeBounds();
		};

		auto TryBuildCutout = [&]() -> bool
		{
			if (pTex == nullptr)
				return false;

			const Texture& tex = *pTex;

			if (!IsRGBA8(tex.GetFormat()))
				return false;

			const uint32 srcW = tex.GetWidth();
			const uint32 srcH = tex.GetHeight();
			if (srcW == 0 || srcH == 0)
				return false;

			const uint8* data = tex.GetData();
			if (data == nullptr)
				return false;

			auto GetAlphaAt = [&](int x, int y) -> uint8
			{
				x = std::max(0, std::min((int)srcW - 1, x));
				y = std::max(0, std::min((int)srcH - 1, y));
				const uint32 idx = (uint32)(y * (int)srcW + x) * 4u;
				return data[idx + 3u];
			};

			// Downsample cap
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

			auto SampleAlpha = [&](int x, int y) -> uint8
			{
				const int sx = (int)((double)x * (double)(srcW - 1) / (double)std::max(1, gw - 1));
				const int sy = (int)((double)y * (double)(srcH - 1) / (double)std::max(1, gh - 1));
				return GetAlphaAt(sx, sy);
			};

			const uint8 alphaThreshold = 128;

			std::vector<uint8> mask((size_t)gw * (size_t)gh, 0);
			for (int y = 0; y < gh; ++y)
				for (int x = 0; x < gw; ++x)
					mask[(size_t)y * (size_t)gw + (size_t)x] = (SampleAlpha(x, y) >= alphaThreshold) ? 1 : 0;

			// 1-iter dilation
			{
				std::vector<uint8> tmp = mask;
				for (int y = 1; y < gh - 1; ++y)
				{
					for (int x = 1; x < gw - 1; ++x)
					{
						if (mask[(size_t)y * (size_t)gw + (size_t)x]) continue;

						uint8 any = 0;
						for (int oy = -1; oy <= 1; ++oy)
							for (int ox = -1; ox <= 1; ++ox)
								any |= mask[(size_t)(y + oy) * (size_t)gw + (size_t)(x + ox)];

						tmp[(size_t)y * (size_t)gw + (size_t)x] = any ? 1 : 0;
					}
				}
				mask.swap(tmp);
			}

			struct IPoint { int x = 0; int y = 0; };
			struct Segment { IPoint A = {}; IPoint B = {}; };
			struct KeyHash { size_t operator()(uint64 v) const noexcept { return std::hash<uint64>{}(v); } };

			auto Pack = [](const IPoint& p) -> uint64 { return (uint64)(uint32)p.x | ((uint64)(uint32)p.y << 32); };

			auto EdgePoint = [&](int cellX, int cellY, int edge) -> IPoint
			{
				switch (edge)
				{
				default:
				case 0: return { (cellX * 2) + 1, (cellY * 2) + 0 }; // top
				case 1: return { (cellX * 2) + 2, (cellY * 2) + 1 }; // right
				case 2: return { (cellX * 2) + 1, (cellY * 2) + 2 }; // bottom
				case 3: return { (cellX * 2) + 0, (cellY * 2) + 1 }; // left
				}
			};

			std::vector<Segment> segments;
			segments.reserve((size_t)(gw * gh));

			auto M = [&](int x, int y) -> uint8 { return mask[(size_t)y * (size_t)gw + (size_t)x]; };

			auto AddSeg = [&](const IPoint& a, const IPoint& b)
			{
				Segment s;
				s.A = a;
				s.B = b;
				segments.push_back(s);
			};

			for (int y = 0; y < gh - 1; ++y)
			{
				for (int x = 0; x < gw - 1; ++x)
				{
					const uint8 a = M(x, y);
					const uint8 b = M(x + 1, y);
					const uint8 c = M(x + 1, y + 1);
					const uint8 d = M(x, y + 1);

					const int code = (int)a | ((int)b << 1) | ((int)c << 2) | ((int)d << 3);
					if (code == 0 || code == 15) continue;

					const bool centerFilled = ((int)a + (int)b + (int)c + (int)d) >= 2;

					auto Emit = [&](int e0, int e1)
					{
						AddSeg(EdgePoint(x, y, e0), EdgePoint(x, y, e1));
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

			// adjacency
			std::unordered_map<uint64, std::vector<IPoint>, KeyHash> adj;
			adj.reserve(segments.size() * 2);

			for (const Segment& s : segments)
			{
				adj[Pack(s.A)].push_back(s.B);
				adj[Pack(s.B)].push_back(s.A);
			}

			std::unordered_set<uint64, KeyHash> usedEdge;
			usedEdge.reserve(segments.size() * 2);

			auto EdgeKey = [&](const IPoint& a, const IPoint& b) -> uint64
			{
				const uint64 ka = Pack(a);
				const uint64 kb = Pack(b);
				return (ka < kb) ? (ka ^ (kb * 0x9E3779B185EBCA87ull)) : (kb ^ (ka * 0x9E3779B185EBCA87ull));
			};

			struct Loop { std::vector<IPoint> P; };
			std::vector<Loop> loops;

			for (const Segment& s0 : segments)
			{
				const uint64 ek0 = EdgeKey(s0.A, s0.B);
				if (usedEdge.find(ek0) != usedEdge.end())
					continue;

				Loop loop;
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

					IPoint next = nbrs[0];
					if ((int)nbrs.size() > 1 && (next.x == prev.x && next.y == prev.y))
						next = nbrs[1];

					const uint64 ek = EdgeKey(cur, next);
					if (usedEdge.find(ek) != usedEdge.end())
					{
						if (next.x == start.x && next.y == start.y)
							break;

						if ((int)nbrs.size() > 1)
						{
							IPoint alt = nbrs[1];
							if (!(alt.x == prev.x && alt.y == prev.y))
							{
								const uint64 ekAlt = EdgeKey(cur, alt);
								if (usedEdge.find(ekAlt) == usedEdge.end())
									next = alt;
								else
									break;
							}
							else break;
						}
						else break;
					}

					usedEdge.insert(EdgeKey(cur, next));

					prev = cur;
					cur = next;

					if (cur.x == start.x && cur.y == start.y)
						break;

					loop.P.push_back(cur);
				}

				if (!loop.P.empty() && loop.P.back().x == start.x && loop.P.back().y == start.y)
					loop.P.pop_back();

				if ((int)loop.P.size() >= 3)
					loops.push_back(std::move(loop));
			}

			if (loops.empty())
				return false;

			// choose largest loop by area
			int bestIdx = 0;
			float bestAbs = -1.0f;

			std::vector<std::vector<float2>> loopUVs;
			loopUVs.reserve(loops.size());

			for (int li = 0; li < (int)loops.size(); ++li)
			{
				const Loop& L = loops[li];
				std::vector<float2> uv;
				uv.reserve(L.P.size());

				for (const IPoint& q : L.P)
				{
					const float u = (float)q.x / (float)(gw * 2);
					const float v = (float)q.y / (float)(gh * 2);
					uv.push_back(float2{ u, v });
				}

				loopUVs.push_back(std::move(uv));

				const float aArea = SignedArea(loopUVs.back());
				const float absA = fabsf(aArea);
				if (absA > bestAbs) { bestAbs = absA; bestIdx = li; }
			}

			std::vector<float2> poly = loopUVs[bestIdx];
			if ((int)poly.size() < 3)
				return false;

			if (SignedArea(poly) < 0.0f)
				std::reverse(poly.begin(), poly.end());

			// simplify
			{
				std::vector<float2> open = poly;
				open.push_back(poly[0]);

				const float eps = std::max(1.0f / (float)gw, 1.0f / (float)gh) * 4.0f;
				std::vector<float2> simp = RdpSimplify(open, eps);

				if (!simp.empty() && (simp.back().x == simp.front().x && simp.back().y == simp.front().y))
					simp.pop_back();

				const int kMaxVerts = 12;
				if ((int)simp.size() > kMaxVerts)
				{
					std::vector<float2> th;
					th.reserve(kMaxVerts);
					const int step = (int)std::ceil((float)simp.size() / (float)kMaxVerts);
					for (int i = 0; i < (int)simp.size(); i += std::max(1, step))
						th.push_back(simp[i]);

					if ((int)th.size() >= 3)
						simp = std::move(th);
				}

				if ((int)simp.size() >= 3)
					poly = std::move(simp);

				if (SignedArea(poly) < 0.0f)
					std::reverse(poly.begin(), poly.end());
			}

			std::vector<uint32> tri;
			if (!EarClipTriangulate(poly, tri))
				return false;

			// Build mesh streams
			std::vector<float3> pos;
			std::vector<float2> uv;
			pos.reserve(poly.size());
			uv.reserve(poly.size());

			auto UVToPos = [&](const float2& t) -> float3
			{
				const float x = (t.x - pivot.x) * scale.x;
				const float y = ((1.0f - t.y) - pivot.y) * scale.y;
				return float3{ x, y, 0.0f };
			};

			for (const float2& t : poly)
			{
				uv.push_back(t);
				pos.push_back(UVToPos(t));
			}

			mesh.SetPositions(std::move(pos));
			mesh.SetTexCoords(std::move(uv));
			mesh.SetIndicesU32(std::move(tri));

			StaticMesh::Section sec = {};
			sec.FirstIndex = 0;
			sec.IndexCount = mesh.GetIndexCount();
			sec.BaseVertex = 0;
			sec.MaterialSlot = 0;
			mesh.SetSections(std::vector<StaticMesh::Section>{ sec });

			mesh.RecomputeBounds();
			return true;
		};

		// If texture CPU data isn't available here, this will fallback to quad.
		// if (!TryBuildCutout())
		{
			BuildQuad();
		}

		MaterialId matId = MaterialManager::GetInstance()->CreateMaterial(templateName + "_Billboard", templateName);
		Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
		mat.SetBlendMode(blendMode);
		mat.SetTextureAssetRef("g_BaseColorTex", texureRef);
		mat.SetUint("g_MaterialFlags", 1);

		mat.SetFloat4("g_BaseColorFactor", float4{ 1.0f,1.0f,1.0f,1.0f });
		mat.SetFloat3("g_EmissiveFactor", float3{1.0f, 1.0f, 1.0f});
		mat.SetFloat("g_EmissiveIntensity", 0.0f);
		mat.SetFloat("g_RoughnessFactor", 0.5f);
		mat.SetFloat("g_NormalScale", 1.0f);
		mat.SetFloat("g_OcclusionStrength", 1.0f);
		mat.SetFloat("g_AlphaCutoff", 0.3f);
		mat.SetFloat("g_MetallicFactor", 0.0f);

		std::vector<MaterialId> materials = { matId };
		mesh.SetMaterialSlots(std::move(materials));

		return mesh;
	}

} // namespace shz
