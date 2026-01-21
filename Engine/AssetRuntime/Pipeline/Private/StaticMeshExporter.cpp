#include "pch.h"
#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshExporter.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "Engine/AssetRuntime/AssetData/Public/StaticMeshAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Common/AssetTypeTraits.h"

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
			{"Min", {b.Min.x, b.Min.y, b.Min.z}},
			{"Max", {b.Max.x, b.Max.y, b.Max.z}},
		};
	}

	bool StaticMeshAssetExporter::operator()(
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

		const StaticMeshAsset* mesh = AssetObjectCast<StaticMeshAsset>(pObject);
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
		std::string idxType = (mesh->GetIndexType() == VT_UINT16) ? "u16" : "u32";

		if (mesh->GetIndexType() == VT_UINT16)
			idxOff = writeBlob(bin, mesh->GetIndicesU16());
		else
			idxOff = writeBlob(bin, mesh->GetIndicesU32());

		// JSON header
		json j;
		j["Format"] = "shzmesh";
		j["Version"] = 1;
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
		j["Bounds"] = boxToJson(mesh->GetBounds());

		// Sections
		j["Sections"] = json::array();
		for (const StaticMeshAsset::Section& s : mesh->GetSections())
		{
			j["Sections"].push_back(json{
				{"FirstIndex", s.FirstIndex},
				{"IndexCount", s.IndexCount},
				{"BaseVertex", s.BaseVertex},
				{"MaterialSlot", s.MaterialSlot},
				{"LocalBounds", boxToJson(s.LocalBounds)},
				});
		}

		// Material slots (inline: 현재 포맷 유지)
		j["MaterialSlots"] = json::array();
		for (const MaterialAsset& m : mesh->GetMaterialSlots())
		{
			json mj;
			mj["Name"] = m.GetName();
			mj["TemplateKey"] = m.GetTemplateKey();
			mj["RenderPassName"] = m.GetRenderPassName();

			// Options (MaterialCommonOptions + extra)
			const auto& o = m.GetOptions();
			mj["Options"] = json{
				{"BlendMode", (int)o.BlendMode},
				{"CullMode", (int)o.CullMode},
				{"FrontCounterClockwise", o.FrontCounterClockwise},
				{"DepthEnable", o.DepthEnable},
				{"DepthWriteEnable", o.DepthWriteEnable},
				{"DepthFunc", (int)o.DepthFunc},
				{"TextureBindingMode", (int)o.TextureBindingMode},
				{"LinearWrapSamplerName", o.LinearWrapSamplerName},
				{"LinearWrapSamplerDesc", json{
					{"MinFilter",(int)o.LinearWrapSamplerDesc.MinFilter},
					{"MagFilter",(int)o.LinearWrapSamplerDesc.MagFilter},
					{"MipFilter",(int)o.LinearWrapSamplerDesc.MipFilter},
					{"AddressU",(int)o.LinearWrapSamplerDesc.AddressU},
					{"AddressV",(int)o.LinearWrapSamplerDesc.AddressV},
					{"AddressW",(int)o.LinearWrapSamplerDesc.AddressW},
					{"MipLODBias",o.LinearWrapSamplerDesc.MipLODBias},
					{"MaxAnisotropy",o.LinearWrapSamplerDesc.MaxAnisotropy},
					{"ComparisonFunc",(int)o.LinearWrapSamplerDesc.ComparisonFunc},
					{"BorderColor", {
						o.LinearWrapSamplerDesc.BorderColor[0],
						o.LinearWrapSamplerDesc.BorderColor[1],
						o.LinearWrapSamplerDesc.BorderColor[2],
						o.LinearWrapSamplerDesc.BorderColor[3],
					}},
					{"MinLOD",o.LinearWrapSamplerDesc.MinLOD},
					{"MaxLOD",o.LinearWrapSamplerDesc.MaxLOD},
				}},
				{"TwoSided", o.bTwoSided},
				{"CastShadow", o.bCastShadow},
			};

			// Values
			mj["Values"] = json::array();
			for (uint32 i = 0; i < m.GetValueOverrideCount(); ++i)
			{
				const auto& v = m.GetValueOverride(i);
				mj["Values"].push_back(json{
					{"StableID", v.StableID},
					{"Name", v.Name},
					{"Type", (int)v.Type},
					{"Data", v.Data},
					});
			}

			// Resources
			mj["Resources"] = json::array();
			for (uint32 i = 0; i < m.GetResourceBindingCount(); ++i)
			{
				const auto& r = m.GetResourceBinding(i);
				json rj;
				rj["StableID"] = r.StableID;
				rj["Name"] = r.Name;
				rj["Type"] = (int)r.Type;
				rj["SourcePath"] = r.TextureRef.GetID().SourcePath;

				const AssetID tid = r.TextureRef.GetID();
				rj["TextureAssetID"] = json{ {"Hi", tid.Hi}, {"Lo", tid.Lo} };

				rj["HasSamplerOverride"] = r.bHasSamplerOverride;
				if (r.bHasSamplerOverride)
				{
					const auto& sd = r.SamplerOverrideDesc;
					rj["SamplerOverrideDesc"] = json{
						{"MinFilter",(int)sd.MinFilter},
						{"MagFilter",(int)sd.MagFilter},
						{"MipFilter",(int)sd.MipFilter},
						{"AddressU",(int)sd.AddressU},
						{"AddressV",(int)sd.AddressV},
						{"AddressW",(int)sd.AddressW},
						{"MipLODBias",sd.MipLODBias},
						{"MaxAnisotropy",sd.MaxAnisotropy},
						{"ComparisonFunc",(int)sd.ComparisonFunc},
						{"BorderColor", json::array({ sd.BorderColor[0], sd.BorderColor[1], sd.BorderColor[2], sd.BorderColor[3] })},
						{"MinLOD",sd.MinLOD},
						{"MaxLOD",sd.MaxLOD},
					};
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
}
