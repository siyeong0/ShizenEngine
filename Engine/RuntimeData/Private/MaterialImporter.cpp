#include "pch.h"
#include "Engine/RuntimeData/Public/MaterialImporter.h"

#include <fstream>
#include <nlohmann/json.hpp>

#include "Engine/RuntimeData/Public/Material.h"

namespace shz
{
	using json = nlohmann::json;

	static inline void setErr(std::string* out, const std::string& s)
	{
		if (out) *out = s;
	}

	static inline SamplerDesc jsonToSampler(const json& j)
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

	std::unique_ptr<AssetObject> MaterialImporter::operator()(
		AssetManager& /*assetManager*/,
		const AssetMeta& meta,
		uint64* pOutResidentBytes,
		std::string* pOutError) const
	{
		ASSERT(pOutResidentBytes != nullptr, "pOutResidentBytes is null.");
		*pOutResidentBytes = 0;
		if (pOutError) pOutError->clear();

		if (meta.SourcePath.empty())
		{
			setErr(pOutError, "MaterialAssetImporter: meta.SourcePath is empty.");
			return {};
		}

		std::ifstream in(meta.SourcePath);
		if (!in.is_open())
		{
			setErr(pOutError, "MaterialAssetImporter: failed to open json.");
			return {};
		}

		json j;
		in >> j;

		if (j.value("Format", "") != "shzmat" || j.value("Version", 0) != 1)
		{
			setErr(pOutError, "MaterialAssetImporter: invalid format/version.");
			return {};
		}

		Material m;
		m.SetName(j.value("Name", ""));
		m.SetTemplateName(j.value("TemplateName", ""));
		m.SetRenderPassName(j.value("RenderPassName", ""));

		// Options
		if (j.contains("Options"))
		{
			auto& o = m.GetOptions();
			const auto& oj = j["Options"];

			o.BlendMode = (MATERIAL_BLEND_MODE)oj.value("BlendMode", (int)o.BlendMode);
			o.CullMode = (CULL_MODE)oj.value("CullMode", (int)o.CullMode);
			o.FrontCounterClockwise = oj.value("FrontCounterClockwise", o.FrontCounterClockwise);

			o.DepthEnable = oj.value("DepthEnable", o.DepthEnable);
			o.DepthWriteEnable = oj.value("DepthWriteEnable", o.DepthWriteEnable);
			o.DepthFunc = (COMPARISON_FUNCTION)oj.value("DepthFunc", (int)o.DepthFunc);

			o.TextureBindingMode = (MATERIAL_TEXTURE_BINDING_MODE)oj.value("TextureBindingMode", (int)o.TextureBindingMode);

			o.LinearWrapSamplerName = oj.value("LinearWrapSamplerName", o.LinearWrapSamplerName);
			if (oj.contains("LinearWrapSamplerDesc"))
				o.LinearWrapSamplerDesc = jsonToSampler(oj["LinearWrapSamplerDesc"]);

			o.bTwoSided = oj.value("TwoSided", o.bTwoSided);
			o.bCastShadow = oj.value("CastShadow", o.bCastShadow);
		}

		// Values
		if (j.contains("Values"))
		{
			for (const auto& vj : j["Values"])
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
		if (j.contains("Resources"))
		{
			for (const auto& rj : j["Resources"])
			{
				const std::string rname = rj.value("Name", "");
				const auto rtype = (MATERIAL_RESOURCE_TYPE)rj.value("Type", (int)MATERIAL_RESOURCE_TYPE_UNKNOWN);
				const uint64 stable = rj.value("StableID", 0ull);

				AssetID texId = {};
				if (rj.contains("TextureAssetID"))
				{
					const auto& idj = rj["TextureAssetID"];
					texId.Hi = idj.value("Hi", 0ull);
					texId.Lo = idj.value("Lo", 0ull);
				}

				if (!rname.empty() && texId)
					m.SetTextureAssetRef(rname.c_str(), rtype, AssetRef<Texture>(texId), stable);

				const bool hasS = rj.value("HasSamplerOverride", false);
				if (hasS && rj.contains("SamplerOverrideDesc"))
					m.SetSamplerOverride(rname.c_str(), jsonToSampler(rj["SamplerOverrideDesc"]), stable);
			}
		}

		*pOutResidentBytes = (uint64)m.GetName().size() + (uint64)m.GetTemplateName().size();
		return std::make_unique<TypedAssetObject<Material>>(static_cast<Material&&>(m));
	}
}
