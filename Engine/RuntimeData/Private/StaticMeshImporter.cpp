#include "pch.h"
#include "Engine/RuntimeData/Public/StaticMeshImporter.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/RuntimeData/Public/TextureAsset.h"
#include "Engine/RuntimeData/Public/StaticMeshAsset.h"
#include "Engine/RuntimeData/Public/MaterialAsset.h"

namespace shz
{
	using json = nlohmann::json;

	static inline void setErr(std::string* out, const std::string& s)
	{
		if (out) *out = s;
	}

	template<typename T>
	static inline bool readBlob(std::ifstream& bin, uint64 off, uint64 count, std::vector<T>& out)
	{
		out.clear();
		if (count == 0)
			return true;

		out.resize((size_t)count);
		bin.seekg((std::streamoff)off, std::ios::beg);
		bin.read((char*)out.data(), (std::streamsize)(count * sizeof(T)));
		return bin.good();
	}

	static inline Box jsonToBox(const json& j)
	{
		const auto& mn = j.at("Min");
		const auto& mx = j.at("Max");
		return Box(
			float3(mn[0].get<float>(), mn[1].get<float>(), mn[2].get<float>()),
			float3(mx[0].get<float>(), mx[1].get<float>(), mx[2].get<float>()));
	}

	static inline SamplerDesc jsonToSamplerDesc(const json& j)
	{
		SamplerDesc d = {};
		d.MinFilter = (FILTER_TYPE)j.value("MinFilter", (int)d.MinFilter);
		d.MagFilter = (FILTER_TYPE)j.value("MagFilter", (int)d.MagFilter);
		d.MipFilter = (FILTER_TYPE)j.value("MipFilter", (int)d.MipFilter);
		d.AddressU = (TEXTURE_ADDRESS_MODE)j.value("AddressU", (int)d.AddressU);
		d.AddressV = (TEXTURE_ADDRESS_MODE)j.value("AddressV", (int)d.AddressV);
		d.AddressW = (TEXTURE_ADDRESS_MODE)j.value("AddressW", (int)d.AddressW);
		d.MipLODBias = j.value("MipLODBias", d.MipLODBias);
		d.MaxAnisotropy = (uint32)j.value("MaxAnisotropy", (int)d.MaxAnisotropy);
		d.ComparisonFunc = (COMPARISON_FUNCTION)j.value("ComparisonFunc", (int)d.ComparisonFunc);
		const auto& bc = j["BorderColor"];
		d.BorderColor[0] = bc[0].get<float>();
		d.BorderColor[1] = bc[1].get<float>();
		d.BorderColor[2] = bc[2].get<float>();
		d.BorderColor[3] = bc[3].get<float>();
		d.MinLOD = j.value("MinLOD", d.MinLOD);
		d.MaxLOD = j.value("MaxLOD", d.MaxLOD);
		return d;
	}

	std::unique_ptr<AssetObject> StaticMeshAssetImporter::operator()(
		AssetManager& assetManager,
		const AssetMeta& meta,
		uint64* pOutResidentBytes,
		std::string* pOutError) const
	{
		ASSERT(pOutResidentBytes != nullptr, "pOutResidentBytes is null.");
		*pOutResidentBytes = 0;
		if (pOutError) pOutError->clear();

		if (meta.SourcePath.empty())
		{
			setErr(pOutError, "StaticMeshAssetImporter: meta.SourcePath is empty.");
			return {};
		}

		std::ifstream in(meta.SourcePath);
		if (!in.is_open())
		{
			setErr(pOutError, "StaticMeshAssetImporter: failed to open json.");
			return {};
		}

		json j;
		in >> j;

		if (j.value("Format", "") != "shzmesh" || j.value("Version", 0) != 1)
		{
			setErr(pOutError, "StaticMeshAssetImporter: invalid format/version.");
			return {};
		}

		const std::filesystem::path baseDir = std::filesystem::path(meta.SourcePath).parent_path();
		const std::string binName = j.value("Bin", "");
		if (binName.empty())
		{
			setErr(pOutError, "StaticMeshAssetImporter: missing Bin field.");
			return {};
		}

		std::ifstream bin(baseDir / binName, std::ios::binary);
		if (!bin.is_open())
		{
			setErr(pOutError, "StaticMeshAssetImporter: failed to open bin.");
			return {};
		}

		StaticMeshAsset mesh;

		// Streams
		const auto& streams = j.at("Streams");

		std::vector<float3> pos, nrm, tan;
		std::vector<float2> uv0;

		auto loadStream = [&](const char* key, auto& outVec) -> bool
		{
			const auto& s = streams.at(key);
			const uint64 off = s.at("Offset").get<uint64>();
			const uint64 cnt = s.at("Count").get<uint64>();
			return readBlob(bin, off, cnt, outVec);
		};

		if (!loadStream("Positions", pos)) { setErr(pOutError, "Failed to read Positions."); return {}; }
		loadStream("Normals", nrm);
		loadStream("Tangents", tan);
		loadStream("TexCoord0", uv0);

		mesh.SetPositions(std::move(pos));
		mesh.SetNormals(std::move(nrm));
		mesh.SetTangents(std::move(tan));
		mesh.SetTexCoords(std::move(uv0));

		// Indices
		const std::string idxType = j.value("IndexType", "u32");
		const auto& ij = j.at("Indices");
		const uint64 idxOff = ij.at("Offset").get<uint64>();
		const uint64 idxCnt = ij.at("Count").get<uint64>();

		if (idxType == "u16")
		{
			std::vector<uint16> indices;
			if (!readBlob(bin, idxOff, idxCnt, indices)) { setErr(pOutError, "Failed to read indices u16."); return {}; }
			mesh.SetIndicesU16(std::move(indices));
		}
		else
		{
			std::vector<uint32> indices;
			if (!readBlob(bin, idxOff, idxCnt, indices)) { setErr(pOutError, "Failed to read indices u32."); return {}; }
			mesh.SetIndicesU32(std::move(indices));
		}

		// Sections
		if (j.contains("Sections"))
		{
			std::vector<StaticMeshAsset::Section> secs;
			for (const auto& sj : j["Sections"])
			{
				StaticMeshAsset::Section s;
				s.FirstIndex = sj.value("FirstIndex", 0u);
				s.IndexCount = sj.value("IndexCount", 0u);
				s.BaseVertex = sj.value("BaseVertex", 0u);
				s.MaterialSlot = sj.value("MaterialSlot", 0u);
				if (sj.contains("LocalBounds"))
					s.LocalBounds = jsonToBox(sj["LocalBounds"]);
				secs.push_back(std::move(s));
			}
			mesh.SetSections(std::move(secs));
		}

		// Material slots (inline)
		if (j.contains("MaterialSlots"))
		{
			std::vector<MaterialAsset> mats;
			for (const auto& mj : j["MaterialSlots"])
			{
				MaterialAsset m;
				m.SetName(mj.value("Name", ""));
				m.SetTemplateName(mj.value("TemplateName", ""));
				m.SetRenderPassName(mj.value("RenderPassName", ""));

				// Options
				if (mj.contains("Options"))
				{
					auto& o = m.GetOptions();
					const auto& oj = mj["Options"];

					o.BlendMode = (MATERIAL_BLEND_MODE)oj.value("BlendMode", (int)o.BlendMode);
					o.CullMode = (CULL_MODE)oj.value("CullMode", (int)o.CullMode);
					o.FrontCounterClockwise = oj.value("FrontCounterClockwise", o.FrontCounterClockwise);

					o.DepthEnable = oj.value("DepthEnable", o.DepthEnable);
					o.DepthWriteEnable = oj.value("DepthWriteEnable", o.DepthWriteEnable);
					o.DepthFunc = (COMPARISON_FUNCTION)oj.value("DepthFunc", (int)o.DepthFunc);

					o.TextureBindingMode = (MATERIAL_TEXTURE_BINDING_MODE)oj.value("TextureBindingMode", (int)o.TextureBindingMode);

					o.LinearWrapSamplerName = oj.value("LinearWrapSamplerName", o.LinearWrapSamplerName);
					if (oj.contains("LinearWrapSamplerDesc"))
						o.LinearWrapSamplerDesc = jsonToSamplerDesc(oj["LinearWrapSamplerDesc"]);

					o.bTwoSided = oj.value("TwoSided", o.bTwoSided);
					o.bCastShadow = oj.value("CastShadow", o.bCastShadow);
				}

				// Values
				if (mj.contains("Values"))
				{
					for (const auto& vj : mj["Values"])
					{
						const std::string name = vj.value("Name", "");
						const auto type = (MATERIAL_VALUE_TYPE)vj.value("Type", (int)MATERIAL_VALUE_TYPE_UNKNOWN);
						const uint64 stable = vj.value("StableID", 0ull);
						const std::vector<uint8> data = vj.value("Data", std::vector<uint8>{});

						if (!name.empty() && !data.empty() && type != MATERIAL_VALUE_TYPE_UNKNOWN)
							m.SetRaw(name.c_str(), type, data.data(), (uint32)data.size(), stable);
					}
				}

				// Resources
				if (mj.contains("Resources"))
				{
					for (const auto& rj : mj["Resources"])
					{
						const std::string rname = rj.value("Name", "");
						const auto rtype = (MATERIAL_RESOURCE_TYPE)rj.value("Type", (int)MATERIAL_RESOURCE_TYPE_UNKNOWN);
						const uint64 stable = rj.value("StableID", 0ull);
						const std::string sourcePath = rj.value("SourcePath", "");

						AssetID texId = {};
						if (rj.contains("TextureAssetID"))
						{
							const auto& idj = rj["TextureAssetID"];
							texId.Hi = idj.value("Hi", 0ull);
							texId.Lo = idj.value("Lo", 0ull);
						}

						if (!rname.empty() && !sourcePath.empty())
						{
							m.SetTextureAssetRef(rname.c_str(), rtype, assetManager.RegisterAsset<TextureAsset>(sourcePath), stable);
						}

						const bool hasS = rj.value("HasSamplerOverride", false);
						if (hasS && rj.contains("SamplerOverrideDesc"))
						{
							m.SetSamplerOverride(rname.c_str(), jsonToSamplerDesc(rj["SamplerOverrideDesc"]), stable);
						}
					}
				}

				mats.push_back(std::move(m));
			}
			mesh.SetMaterialSlots(std::move(mats));
		}

		// Bounds: 저장된 값은 참고만 하고, 안전하게 재계산
		mesh.RecomputeBounds();

		if (!mesh.IsValid())
		{
			setErr(pOutError, "StaticMeshAssetImporter: mesh invalid after load.");
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

			*pOutResidentBytes = bytes;
		}

		return std::make_unique<TypedAssetObject<StaticMeshAsset>>(static_cast<StaticMeshAsset&&>(mesh));
	}
}
