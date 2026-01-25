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

		std::string name = j.value("Name", "");
		std::string templateName = j.value("TemplateName", "");
		Material m(name, templateName);

		m.SetRenderPassName(j.value("RenderPassName", ""));

		// Options
		// Options
		if (j.contains("Options"))
		{
			const auto& oj = j["Options"];

			// Blend / Raster
			{
				const MATERIAL_BLEND_MODE blend = (MATERIAL_BLEND_MODE)oj.value("BlendMode", (int)m.GetBlendMode());
				m.SetBlendMode(blend);
				const CULL_MODE cull = (CULL_MODE)oj.value("CullMode", (int)m.GetCullMode());
				m.SetCullMode(cull);
				const bool frontCCW = oj.value("FrontCounterClockwise", m.GetFrontCounterClockwise());
				m.SetFrontCounterClockwise(frontCCW);
			}

			// Depth
			{
				const bool depthEnable = oj.value("DepthEnable", m.GetDepthEnable());
				m.SetDepthEnable(depthEnable);
				const bool depthWrite = oj.value("DepthWriteEnable", m.GetDepthWriteEnable());
				m.SetDepthWriteEnable(depthWrite);
				const COMPARISON_FUNCTION depthFunc = (COMPARISON_FUNCTION)oj.value("DepthFunc", (int)m.GetDepthFunc());
				m.SetDepthFunc(depthFunc);
			}

			// Texture binding mode 
			{
				const MATERIAL_TEXTURE_BINDING_MODE bindMode = (MATERIAL_TEXTURE_BINDING_MODE)oj.value("TextureBindingMode", (int)m.GetTextureBindingMode());
				m.SetTextureBindingMode(bindMode);
			}

			// LinearWrap sampler (layout ¿µÇâ)
			{
				const std::string samplerName = oj.value("LinearWrapSamplerName", m.GetLinearWrapSamplerName());
				m.SetLinearWrapSamplerName(samplerName);

				if (oj.contains("LinearWrapSamplerDesc"))
				{
					const SamplerDesc desc = jsonToSampler(oj["LinearWrapSamplerDesc"]);
					m.SetLinearWrapSamplerDesc(desc);
				}
			}
		}


		// Values
		if (j.contains("Values"))
		{
			for (const auto& vj : j["Values"])
			{
				const std::string name = vj.value("Name", "");
				const auto type = (MATERIAL_VALUE_TYPE)vj.value("Type", (int)MATERIAL_VALUE_TYPE_UNKNOWN);
				const std::vector<uint8> data = vj.value("Data", std::vector<uint8>{});

				if (!name.empty() && !data.empty() && type != MATERIAL_VALUE_TYPE_UNKNOWN)
					m.SetRaw(name.c_str(), type, data.data(), (uint32)data.size());
			}
		}

		// Resources
		if (j.contains("Resources"))
		{
			for (const auto& rj : j["Resources"])
			{
				const std::string rname = rj.value("Name", "");
				const auto rtype = (MATERIAL_RESOURCE_TYPE)rj.value("Type", (int)MATERIAL_RESOURCE_TYPE_UNKNOWN);

				AssetID texId = {};
				if (rj.contains("TextureAssetID"))
				{
					const auto& idj = rj["TextureAssetID"];
					texId.Hi = idj.value("Hi", 0ull);
					texId.Lo = idj.value("Lo", 0ull);
				}

				if (!rname.empty() && texId)
					m.SetTextureAssetRef(rname.c_str(), rtype, AssetRef<Texture>(texId));

				const bool hasS = rj.value("HasSamplerOverride", false);
				if (hasS && rj.contains("SamplerOverrideDesc"))
				{
					m.SetSamplerOverrideDesc(
						rname.c_str(),
						jsonToSampler(rj["SamplerOverrideDesc"]));
				}
			}
		}

		*pOutResidentBytes = (uint64)m.GetName().size() + (uint64)m.GetTemplateName().size();
		return std::make_unique<TypedAssetObject<Material>>(static_cast<Material&&>(m));
	}
}
