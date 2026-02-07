#include "pch.h"
#include "Engine/RuntimeData/Public/StaticMeshImporter.h"

#include "Engine/Core/Json/BasicTypesJsonAdaptor.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/RuntimeData/Public/Material.h"

namespace shz
{
	template<typename T>
	bool readBlob(std::ifstream& bin, uint64 off, uint64 count, std::vector<T>& out)
	{
		ASSERT(bin.is_open(), "Binary stream is not open.");
		ASSERT(out.empty(), "Output vector is not empty.");
		ASSERT(count > 0 || off == 0, "Invalid parameters.");

		out.resize((size_t)count);
		bin.seekg((std::streamoff)off, std::ios::beg);
		bin.read((char*)out.data(), (std::streamsize)(count * sizeof(T)));
		return bin.good();
	}

	std::unique_ptr<AssetObject> StaticMeshImporter::operator()(
		AssetManager& assetManager,
		const AssetMeta& meta,
		uint64* pOutResidentBytes,
		std::string* pOutError) const
	{
		using json = nlohmann::json;

		auto setErr = [&](const char* msg)
			{
				if (pOutError) *pOutError = msg;
			};

		ASSERT(pOutResidentBytes != nullptr, "pOutResidentBytes is null.");
		*pOutResidentBytes = 0;
		if (pOutError) pOutError->clear();

		if (meta.SourcePath.empty())
		{
			setErr("StaticMeshImporter: meta.SourcePath is empty.");
			return {};
		}

		std::ifstream in(meta.SourcePath, std::ios::binary);
		if (!in.is_open())
		{
			setErr("StaticMeshImporter: failed to open json.");
			return {};
		}

		json j;
		try { in >> j; }
		catch (...) { setErr("StaticMeshImporter: json parse failed."); return {}; }

		if (j.value("Format", "") != "shzmesh")
		{
			setErr("StaticMeshImporter: invalid format.");
			return {};
		}

		const std::filesystem::path baseDir = std::filesystem::path(meta.SourcePath).parent_path();
		const std::string binName = j.value("Bin", "");
		if (binName.empty())
		{
			setErr("StaticMeshImporter: missing Bin field.");
			return {};
		}

		std::ifstream bin(baseDir / binName, std::ios::binary);
		if (!bin.is_open())
		{
			setErr("StaticMeshImporter: failed to open bin.");
			return {};
		}

		StaticMesh mesh;

		// ------------------------------------------------------------
		// Streams
		// ------------------------------------------------------------
		const json& streams = j.at("Streams");

		std::vector<float3> pos, nrm, tan;
		std::vector<float2> uv0;

		auto loadStream = [&](const char* key, auto& outVec) -> bool
			{
				if (!streams.contains(key))
				{
					return true; // optional streams OK
				}

				const json& s = streams.at(key);
				const uint64 off = s.at("Offset").get<uint64>();
				const uint64 cnt = s.at("Count").get<uint64>();
				return readBlob(bin, off, cnt, outVec);
			};

		if (!loadStream("Positions", pos)) { setErr("StaticMeshImporter: failed to read Positions."); return {}; }
		if (!loadStream("Normals", nrm)) { setErr("StaticMeshImporter: failed to read Normals.");   return {}; }
		if (!loadStream("Tangents", tan)) { setErr("StaticMeshImporter: failed to read Tangents.");  return {}; }
		if (!loadStream("TexCoord0", uv0)) { setErr("StaticMeshImporter: failed to read TexCoord0."); return {}; }

		mesh.SetPositions(std::move(pos));
		mesh.SetNormals(std::move(nrm));
		mesh.SetTangents(std::move(tan));
		mesh.SetTexCoords(std::move(uv0));

		// ------------------------------------------------------------
		// Indices
		// ------------------------------------------------------------
		const std::string idxType = j.value("IndexType", "u32");
		const json& ij = j.at("Indices");
		const uint64 idxOff = ij.at("Offset").get<uint64>();
		const uint64 idxCnt = ij.at("Count").get<uint64>();

		if (idxType == "u16")
		{
			std::vector<uint16> indices;
			if (!readBlob(bin, idxOff, idxCnt, indices)) { setErr("StaticMeshImporter: failed to read indices u16."); return {}; }
			mesh.SetIndicesU16(std::move(indices));
		}
		else
		{
			std::vector<uint32> indices;
			if (!readBlob(bin, idxOff, idxCnt, indices)) { setErr("StaticMeshImporter: failed to read indices u32."); return {}; }
			mesh.SetIndicesU32(std::move(indices));
		}

		// ------------------------------------------------------------
		// Sections
		// ------------------------------------------------------------
		if (j.contains("Sections") && j["Sections"].is_array())
		{
			std::vector<StaticMesh::Section> secs;
			secs.reserve((size_t)j["Sections"].size());

			for (const auto& sj : j["Sections"])
			{
				StaticMesh::Section s;
				s.FirstIndex = sj.value("FirstIndex", 0u);
				s.IndexCount = sj.value("IndexCount", 0u);
				s.BaseVertex = sj.value("BaseVertex", 0u);
				s.MaterialSlot = sj.value("MaterialSlot", 0u);

				if (sj.contains("LocalBounds"))
					s.LocalBounds = sj["LocalBounds"].get<Box>();

				secs.push_back(std::move(s));
			}
			mesh.SetSections(std::move(secs));
		}

		// ------------------------------------------------------------
		// MaterialSlots
		// ------------------------------------------------------------
		if (j.contains("MaterialSlots") && j["MaterialSlots"].is_array())
		{
			std::vector<AssetRef<Material>> mats;
			mats.reserve((size_t)j["MaterialSlots"].size());

			for (const auto& mj : j["MaterialSlots"])
			{
				const std::string srcPath =
					mj.contains("Material") && mj["Material"].is_object()
					? mj["Material"].value("SourcePath", std::string{})
					: std::string{};

				ASSERT(!srcPath.empty(), "Material source path missing in version 2.");
				if (!srcPath.empty())
				{
					AssetRef<Material> ref = assetManager.RegisterAsset<Material>(srcPath);
					ASSERT(ref.IsValid(), "Failed to register material asset: %s", srcPath.c_str());
					mats.push_back(std::move(ref));
				}
			}

			mesh.SetMaterialSlots(std::move(mats));
		}

		mesh.RecomputeBounds();

		if (!mesh.IsValid())
		{
			setErr("StaticMeshImporter: mesh invalid after load.");
			return {};
		}

		// Rough resident bytes estimate
		{
			uint64 bytes = 0;
			bytes += (uint64)mesh.GetPositions().size() * sizeof(float3);
			bytes += (uint64)mesh.GetNormals().size() * sizeof(float3);
			bytes += (uint64)mesh.GetTangents().size() * sizeof(float3);
			bytes += (uint64)mesh.GetTexCoords().size() * sizeof(float2);

			bytes += (mesh.GetIndexType() == VT_UINT16)
				? (uint64)mesh.GetIndicesU16().size() * sizeof(uint16)
				: (uint64)mesh.GetIndicesU32().size() * sizeof(uint32);

			bytes += (uint64)mesh.GetSections().size() * sizeof(StaticMesh::Section);
			bytes += (uint64)mesh.GetMaterials().size() * sizeof(AssetRef<Material>);

			*pOutResidentBytes = bytes;
		}

		return std::make_unique<TypedAssetObject<StaticMesh>>(static_cast<StaticMesh&&>(mesh));
	}
} // namespace shz
