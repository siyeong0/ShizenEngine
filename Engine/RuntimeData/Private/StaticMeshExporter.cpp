#include "pch.h"
#include "Engine/RuntimeData/Public/StaticMeshExporter.h"

#include "Engine/Core/Json/BasicTypesJsonAdaptor.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/RuntimeData/Public/Material.h"
#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/AssetManager/Public/AssetMeta.h"
#include "Engine/AssetManager/Public/AssetObject.h"

namespace shz
{
	template<typename T>
	uint64 writeBlob(std::ofstream& bin, const std::vector<T>& v)
	{
		ASSERT(bin.is_open(), "Binary stream is not open.");
		ASSERT(!v.empty(), "Input vector is empty.");
		const uint64 off = (uint64)bin.tellp();
		bin.write((const char*)v.data(), (std::streamsize)(v.size() * sizeof(T)));
		return off;
	}

	bool StaticMeshExporter::operator()(
		AssetManager& /*assetManager*/,
		const AssetMeta& /*meta*/,
		const AssetObject* pObject,
		const std::string& outPath,
		std::string* pOutError) const
	{
		using json = nlohmann::json;

		auto setErr = [&](const char* msg) -> bool
			{
				if (pOutError) *pOutError = msg;
				return false;
			};

		auto assetRefToJson = [&](const AssetRef<Material>& r) -> json
			{
				json jr;
				jr["SourcePath"] = r.GetSourcePath();
				return jr;
			};

		ASSERT(pObject != nullptr, "Object is null.");
		ASSERT(!outPath.empty(), "outPath is empty.");

		const StaticMesh* pMesh = AssetObjectCast<StaticMesh>(pObject);
		ASSERT(pMesh, "Failed to cast AssetObject to StaticMesh.");
		ASSERT(pMesh->IsValid(), "Mesh is invalid.");


		std::filesystem::path jsonPath(outPath);
		ASSERT(jsonPath.extension() == ".json", "OutPath must have .json extension.");
		std::filesystem::path binPath(jsonPath);
		binPath.replace_extension(".bin");

		std::filesystem::create_directories(jsonPath.parent_path());

		std::ofstream bin(binPath, std::ios::binary | std::ios::trunc);
		if (!bin.is_open())
			return setErr("StaticMeshExporter: failed to open bin file.");

		// ------------------------------------------------------------
		// Write streams to .bin
		// ------------------------------------------------------------
		const uint64 posOff = writeBlob(bin, pMesh->GetPositions());
		const uint64 nrmOff = writeBlob(bin, pMesh->GetNormals());
		const uint64 tanOff = writeBlob(bin, pMesh->GetTangents());
		const uint64 uv0Off = writeBlob(bin, pMesh->GetTexCoords());

		uint64 idxOff = 0;
		const bool bU16 = (pMesh->GetIndexType() == VT_UINT16);
		const std::string idxType = bU16 ? "u16" : "u32";
		if (bU16)
		{
			idxOff = writeBlob(bin, pMesh->GetIndicesU16());
		}
		else
		{
			idxOff = writeBlob(bin, pMesh->GetIndicesU32());
		}

		ASSERT(bin.good(), "Binary stream is in bad state after writing.");
		
		// ------------------------------------------------------------
		// Build JSON
		// ------------------------------------------------------------
		json j;
		j["Format"] = "shzmesh";
		j["Bin"] = binPath.filename().string();

		j["VertexCount"] = pMesh->GetVertexCount();
		j["IndexCount"] = pMesh->GetIndexCount();
		j["IndexType"] = idxType;

		j["Streams"] = json::object();
		j["Streams"]["Positions"] = json{ {"Offset", posOff}, {"Count", (uint64)pMesh->GetPositions().size()}, {"Stride", (uint64)sizeof(float3)} };
		j["Streams"]["Normals"] = json{ {"Offset", nrmOff}, {"Count", (uint64)pMesh->GetNormals().size()},   {"Stride", (uint64)sizeof(float3)} };
		j["Streams"]["Tangents"] = json{ {"Offset", tanOff}, {"Count", (uint64)pMesh->GetTangents().size()},  {"Stride", (uint64)sizeof(float3)} };
		j["Streams"]["TexCoord0"] = json{ {"Offset", uv0Off}, {"Count", (uint64)pMesh->GetTexCoords().size()}, {"Stride", (uint64)sizeof(float2)} };

		j["Indices"] = json{ {"Offset", idxOff}, {"Count", (uint64)pMesh->GetIndexCount()} };

		// Bounds
		j["Bounds"] = json(pMesh->GetBounds());

		// Sections
		{
			json secs = json::array();
			for (const StaticMesh::Section& s : pMesh->GetSections())
			{
				json sj;
				sj["FirstIndex"] = s.FirstIndex;
				sj["IndexCount"] = s.IndexCount;
				sj["BaseVertex"] = s.BaseVertex;
				sj["MaterialSlot"] = s.MaterialSlot;
				sj["LocalBounds"] = boxToJson(s.LocalBounds);
				secs.push_back(std::move(sj));
			}
			j["Sections"] = std::move(secs);
		}

		// MaterialSlots : vector<AssetRef<Material>>
		{
			json mats = json::array();
			for (const AssetRef<Material>& r : pMesh->GetMaterials())
			{
				json mj;
				mj["Material"] = assetRefToJson(r);
				mats.push_back(std::move(mj));
			}
			j["MaterialSlots"] = std::move(mats);
		}

		// ------------------------------------------------------------
		// Write json
		// ------------------------------------------------------------
		std::ofstream out(jsonPath, std::ios::trunc);
		if (!out.is_open())
		{
			ASSERTION_FAILED("Failed to open json file.");
			return setErr("Failed to open json file.");
		}

		out << j.dump(2);
		if (!out.good())
		{
			ASSERTION_FAILED("Failed to write json.");
			return setErr("Failed to write json.");
		}

		return true;
	}
} // namespace shz
