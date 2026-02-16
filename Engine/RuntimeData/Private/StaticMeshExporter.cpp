#include "pch.h"
#include "Engine/RuntimeData/Public/StaticMeshExporter.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/RuntimeData/Public/Material.h"
#include "Engine/AssetManager/Public/AssetTypeTraits.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

namespace shz
{
	using json = nlohmann::json;

	static inline void setErr(std::string* out, const std::string& s)
	{
		if (out) *out = s;
	}

	template<typename T>
	static inline uint64 writeBlob(std::ofstream& bin, const std::vector<T>& v)
	{
		if (v.empty())
			return 0;

		const uint64 off = (uint64)bin.tellp();
		bin.write((const char*)v.data(), (std::streamsize)(v.size() * sizeof(T)));
		return off;
	}

	static inline json boxToJson(const Box& b)
	{
		return json{
			{"Min", {b.Min().x, b.Min().y, b.Min().z}},
			{"Max", {b.Max().x, b.Max().y, b.Max().z}},
		};
	}

	static inline json samplerToJson(const SamplerDesc& sd)
	{
		return json{
			{"MinFilter",(int)sd.MinFilter},
			{"MagFilter",(int)sd.MagFilter},
			{"MipFilter",(int)sd.MipFilter},
			{"AddressU",(int)sd.AddressU},
			{"AddressV",(int)sd.AddressV},
			{"AddressW",(int)sd.AddressW},
			{"MipLODBias",sd.MipLODBias},
			{"MaxAnisotropy",sd.MaxAnisotropy},
			{"ComparisonFunc",(int)sd.ComparisonFunc},
			{"BorderColor", {
				sd.BorderColor[0],
				sd.BorderColor[1],
				sd.BorderColor[2],
				sd.BorderColor[3],
			}},
			{"MinLOD",sd.MinLOD},
			{"MaxLOD",sd.MaxLOD},
		};
	}

	bool StaticMeshExporter::operator()(
		AssetManager& /*assetManager*/,
		const AssetMeta& /*meta*/,
		const AssetObject* pObject,
		const std::string& outPath,
		std::string* pOutError) const
	{
		if (!pObject)
		{
			setErr(pOutError, "StaticMeshAssetExporter: object is null.");
			return false;
		}

		const StaticMeshLevel* mesh = AssetObjectCast<StaticMeshLevel>(pObject);
		if (!mesh)
		{
			setErr(pOutError, "StaticMeshAssetExporter: type mismatch (not StaticMeshAsset).");
			return false;
		}

		if (!mesh->IsValid())
		{
			setErr(pOutError, "StaticMeshAssetExporter: mesh is invalid.");
			return false;
		}

		if (outPath.empty())
		{
			setErr(pOutError, "StaticMeshAssetExporter: outPath is empty.");
			return false;
		}

		std::filesystem::path jsonPath(outPath);
		ASSERT(jsonPath.extension() == ".json", "OutPath must have .shzmesh.json extension.");
		std::filesystem::path binPath(jsonPath);
		binPath.replace_extension(".bin");

		std::filesystem::create_directories(std::filesystem::path(jsonPath).parent_path());

		std::ofstream bin(binPath, std::ios::binary | std::ios::trunc);
		if (!bin.is_open())
		{
			setErr(pOutError, "StaticMeshAssetExporter: failed to open bin file.");
			return false;
		}

		// Write streams
		const uint64 posOff = writeBlob(bin, mesh->GetPositions());
		const uint64 nrmOff = writeBlob(bin, mesh->GetNormals());
		const uint64 tanOff = writeBlob(bin, mesh->GetTangents());
		const uint64 uv0Off = writeBlob(bin, mesh->GetTexCoords());

		uint64 idxOff = 0;
		const std::string idxType = (mesh->GetIndexType() == VT_UINT16) ? "u16" : "u32";

		if (mesh->GetIndexType() == VT_UINT16)
			idxOff = writeBlob(bin, mesh->GetIndicesU16());
		else
			idxOff = writeBlob(bin, mesh->GetIndicesU32());

		// JSON header
		json j;
		j["Format"] = "shzmesh";
		j["Version"] = 2; // updated (material schema updated)
		j["Bin"] = binPath.filename().string();

		j["VertexCount"] = mesh->GetVertexCount();
		j["IndexCount"] = mesh->GetIndexCount();
		j["IndexType"] = idxType;

		j["Streams"] = json::object();
		j["Streams"]["Positions"] = json{ {"Offset", posOff}, {"Count", mesh->GetPositions().size()}, {"Stride", (uint64)sizeof(float3)} };
		j["Streams"]["Normals"] = json{ {"Offset", nrmOff}, {"Count", mesh->GetNormals().size()},   {"Stride", (uint64)sizeof(float3)} };
		j["Streams"]["Tangents"] = json{ {"Offset", tanOff}, {"Count", mesh->GetTangents().size()},  {"Stride", (uint64)sizeof(float3)} };
		j["Streams"]["TexCoord0"] = json{ {"Offset", uv0Off}, {"Count", mesh->GetTexCoords().size()}, {"Stride", (uint64)sizeof(float2)} };
		j["Indices"] = json{ {"Offset", idxOff}, {"Count", (uint64)mesh->GetIndexCount()} };

		// Bounds
		j["Bounds"] = boxToJson(mesh->GetBoxBounds());

		// Sections
		j["Sections"] = json::array();
		for (const StaticMeshLevel::Section& s : mesh->GetSections())
		{
			j["Sections"].push_back(json{
				{"FirstIndex", s.FirstIndex},
				{"IndexCount", s.IndexCount},
				{"BaseVertex", s.BaseVertex},
				{"MaterialSlot", s.MaterialSlot},
				});
		}

		// Material slots (inline: keep format, but update to new Material storage)
		j["MaterialSlots"] = json::array();
		for (const MaterialId& id : mesh->GetMaterialSlots())
		{
			const Material& m = MaterialManager::GetInstance()->GetMaterial(id);

			json mj;
			mj["Name"] = m.GetName();
			mj["TemplateName"] = m.GetTemplateName();

			// Options
			mj["Options"] = json{
				{"BlendMode", (int)m.GetBlendMode()},
				{"CullMode", (int)m.GetCullMode()},
				{"FrontCounterClockwise", m.GetFrontCounterClockwise()},

				{"DepthEnable", m.GetDepthEnable()},
				{"DepthWriteEnable", m.GetDepthWriteEnable()},
				{"DepthFunc", (int)m.GetDepthFunc()},
			};

			// Values (new: iterate simplified value map)
			mj["Values"] = json::array();
			for (const auto& kv : m.GetAllValues())
			{
				const std::string& name = kv.first;
				const MaterialValueBlob& v = kv.second;

				mj["Values"].push_back(json{
					{"Name", name},
					{"Type", (int)v.Type},
					{"Data", v.Data},
					});
			}

			// Resources (new: iterate template resources, pull from simplified texture map + sampler overrides from bindings)
			mj["Resources"] = json::array();

			const uint32 resCount = m.GetTemplate().GetResourceCount();
			for (uint32 ri = 0; ri < resCount; ++ri)
			{
				const MaterialResourceDesc& rr = m.GetTemplate().GetResource(ri);
				if (!IsTextureType(rr.Type))
				{
					continue;
				}

				json rj;
				rj["Name"] = rr.Name;
				rj["Type"] = (int)rr.Type;

				// Texture (optional)
				AssetID tid = {};
				bool bHasTexture = false;

				if (const MaterialTexture* mt = m.GetTextureOrNull(rr.Name))
				{
					if (mt->Texture.IsValid())
					{
						tid = mt->Texture.GetID();
						bHasTexture = true;
						rj["SourcePath"] = tid.SourcePath;
					}
				}

				if (!bHasTexture)
				{
					rj["SourcePath"] = "";
				}

				// Preserve TextureAssetID field for backward tooling (only if valid)
				if (bHasTexture)
				{
					rj["TextureAssetID"] = json{ {"Hi", tid.Hi}, {"Lo", tid.Lo} };
				}

				mj["Resources"].push_back(std::move(rj));
			}

			j["MaterialSlots"].push_back(std::move(mj));
		}

		std::ofstream out(jsonPath, std::ios::trunc);
		if (!out.is_open())
		{
			setErr(pOutError, "StaticMeshAssetExporter: failed to open json file.");
			return false;
		}

		out << j.dump(2);
		return true;
	}
} // namespace shz
