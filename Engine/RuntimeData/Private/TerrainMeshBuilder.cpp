#include "pch.h"
#include "Engine/RuntimeData/Public/TerrainMeshBuilder.h"

#include "Engine/RuntimeData/Public/Material.h"

namespace shz
{
	static inline void setError(std::string* pOutError, const char* msg)
	{
		if (pOutError)
		{
			*pOutError = (msg != nullptr) ? msg : "Unknown error.";
		}
	}

	static inline uint32 idx2D(uint32 x, uint32 z, uint32 w)
	{
		return z * w + x;
	}

	static float3 computeNormalCentralDiff(
		const TerrainHeightField& hf,
		uint32 x, uint32 z,
		const TerrainMeshBuildSettings& s)
	{
		const uint32 w = hf.GetWidth();
		const uint32 h = hf.GetHeight();

		const uint32 x0 = (x > 0) ? (x - 1) : x;
		const uint32 x1 = (x + 1 < w) ? (x + 1) : x;

		const uint32 z0 = (z > 0) ? (z - 1) : z;
		const uint32 z1 = (z + 1 < h) ? (z + 1) : z;

		// World heights
		const float hl = hf.GetWorldHeightAt(x0, z);
		const float hr = hf.GetWorldHeightAt(x1, z);
		const float hd = hf.GetWorldHeightAt(x, z0);
		const float hu = hf.GetWorldHeightAt(x, z1);

		const float dx = (hr - hl);
		const float dz = (hu - hd);

		// Convert height slope into a normal.
		// TangentX ~ (2*spacingX, dx, 0), TangentZ ~ (0, dz, 2*spacingZ)
		// Cross gives a stable up-ish vector. We use a simple heuristic:
		//
		// n = normalize( (-dx, upBias, -dz) )  where dx,dz are in world units
		//
		// upBias should be scaled relative to spacing to avoid overly steep normals.
		const float up = std::max(0.001f, s.NormalUpBias);

		return float3{ -dx, up, -dz }.Normalized();
	}

	bool TerrainMeshBuilder::BuildStaticMesh(
		StaticMesh* pOutMesh,
		const TerrainHeightField& hf,
		Material&& terrainMaterial,
		const TerrainMeshBuildSettings& settings)
	{
		ASSERT(pOutMesh != nullptr, "pOutMesh is null.");
		ASSERT(hf.IsValid(), "Heightfield is invalid.");

		const uint32 w = hf.GetWidth();
		const uint32 h = hf.GetHeight();

		ASSERT(w >= 4 && h >= 4, "Heightfield resolution is too small.");

		pOutMesh->Clear();

		const uint64 numVertices64 = static_cast<uint64>(w) * static_cast<uint64>(h);
		if (numVertices64 > static_cast<uint64>(std::numeric_limits<uint32>::max()))
		{
			ASSERT(false, "Too many vertices.");
			return false;
		}

		const uint32 numVertices = static_cast<uint32>(numVertices64);

		// ------------------------------------------------------------
		// Build vertex streams
		// ------------------------------------------------------------
		std::vector<float3> positions;
		std::vector<float3> normals;
		std::vector<float2> uvs;

		positions.resize(numVertices);

		if (settings.bGenerateNormals)
		{
			normals.resize(numVertices);
		}

		if (settings.bGenerateTexCoords)
		{
			uvs.resize(numVertices);
		}

		const float spacingX = hf.GetWorldSpacingX();
		const float spacingZ = hf.GetWorldSpacingZ();

		const float sizeX = hf.GetWorldSizeX();
		const float sizeZ = hf.GetWorldSizeZ();

		const float originX = settings.bCenterXZ ? (-0.5f * sizeX) : 0.f;
		const float originY = settings.YOffset;
		const float originZ = settings.bCenterXZ ? (-0.5f * sizeZ) : 0.f;

		for (uint32 z = 0; z < h; ++z)
		{
			for (uint32 x = 0; x < w; ++x)
			{
				const uint32 i = idx2D(x, z, w);

				const float wx = originX + static_cast<float>(x) * spacingX;
				const float wz = originZ + static_cast<float>(z) * spacingZ;
				const float wy = originY + hf.GetWorldHeightAt(x, z);

				positions[i] = float3{ wx, wy, wz };

				if (settings.bGenerateNormals)
				{
					normals[i] = computeNormalCentralDiff(hf, x, z, settings);
				}

				if (settings.bGenerateTexCoords)
				{
					const float u = (w > 1) ? (static_cast<float>(x) / static_cast<float>(w - 1)) : 0.f;
					const float v = (h > 1) ? (static_cast<float>(z) / static_cast<float>(h - 1)) : 0.f;
					uvs[i] = float2{ Clamp01(u), Clamp01(v) };
				}
			}
		}

		pOutMesh->SetPositions(std::move(positions));

		if (settings.bGenerateNormals)
		{
			pOutMesh->SetNormals(std::move(normals));
		}

		if (settings.bGenerateTexCoords)
		{
			pOutMesh->SetTexCoords(std::move(uvs));
		}

		// ------------------------------------------------------------
		// Build indices (grid -> 2 tris per quad)
		// ------------------------------------------------------------
		const uint64 numQuads64 = static_cast<uint64>(w - 1) * static_cast<uint64>(h - 1);
		const uint64 numIndices64 = numQuads64 * 6ull;

		if (numIndices64 > static_cast<uint64>(std::numeric_limits<uint32>::max()))
		{
			ASSERT(false, "Too many indices.");
			return false;
		}

		const uint32 numIndices = static_cast<uint32>(numIndices64);

		const bool bCanUseU16 = (numVertices <= 65535u);
		const bool bUseU16 = settings.bPreferU16Indices && bCanUseU16;

		if (bUseU16)
		{
			std::vector<uint16> indices;
			indices.resize(numIndices);

			uint32 out = 0;

			for (uint32 z = 0; z < h - 1; ++z)
			{
				for (uint32 x = 0; x < w - 1; ++x)
				{
					const uint32 i0 = idx2D(x, z, w);
					const uint32 i1 = idx2D(x + 1, z, w);
					const uint32 i2 = idx2D(x, z + 1, w);
					const uint32 i3 = idx2D(x + 1, z + 1, w);

					// Two triangles per quad.
					// Default winding (CCW in x-z plane with y-up assumption):
					//  tri0: i0, i2, i1
					//  tri1: i1, i2, i3
					if (!settings.bFlipWinding)
					{
						indices[out++] = static_cast<uint16>(i0);
						indices[out++] = static_cast<uint16>(i2);
						indices[out++] = static_cast<uint16>(i1);

						indices[out++] = static_cast<uint16>(i1);
						indices[out++] = static_cast<uint16>(i2);
						indices[out++] = static_cast<uint16>(i3);
					}
					else
					{
						indices[out++] = static_cast<uint16>(i0);
						indices[out++] = static_cast<uint16>(i1);
						indices[out++] = static_cast<uint16>(i2);

						indices[out++] = static_cast<uint16>(i1);
						indices[out++] = static_cast<uint16>(i3);
						indices[out++] = static_cast<uint16>(i2);
					}
				}
			}

			pOutMesh->SetIndicesU16(std::move(indices));
		}
		else
		{
			std::vector<uint32> indices;
			indices.resize(numIndices);

			uint32 out = 0;

			for (uint32 z = 0; z < h - 1; ++z)
			{
				for (uint32 x = 0; x < w - 1; ++x)
				{
					const uint32 i0 = idx2D(x, z, w);
					const uint32 i1 = idx2D(x + 1, z, w);
					const uint32 i2 = idx2D(x, z + 1, w);
					const uint32 i3 = idx2D(x + 1, z + 1, w);

					if (!settings.bFlipWinding)
					{
						indices[out++] = i0;
						indices[out++] = i1;
						indices[out++] = i2;

						indices[out++] = i1;
						indices[out++] = i3;
						indices[out++] = i2;
					}
					else
					{
						indices[out++] = i0;
						indices[out++] = i2;
						indices[out++] = i1;

						indices[out++] = i1;
						indices[out++] = i2;
						indices[out++] = i3;
					}
				}
			}

			pOutMesh->SetIndicesU32(std::move(indices));
		}

		// ------------------------------------------------------------
		// Sections / materials
		// ------------------------------------------------------------
		StaticMesh::Section section = {};
		section.FirstIndex = 0;
		section.IndexCount = pOutMesh->GetIndexCount();
		section.BaseVertex = 0;
		section.MaterialSlot = 0;
		// Bounds will be computed by mesh recompute

		std::vector<StaticMesh::Section> sections;
		sections.emplace_back(static_cast<StaticMesh::Section&&>(section));
		pOutMesh->SetSections(std::move(sections));

		std::vector<Material> materials;
		materials.emplace_back(std::move(terrainMaterial));
		pOutMesh->SetMaterialSlots(std::move(materials));

		// ------------------------------------------------------------
		// Bounds
		// ------------------------------------------------------------
		pOutMesh->RecomputeBounds();

		if (!pOutMesh->IsValid())
		{
			ASSERT(false, "Built StaticMesh is invalid.");
			return false;
		}

		return true;
	}
} // namespace shz
