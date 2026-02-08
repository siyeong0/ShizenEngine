#include "pch.h"
#include "Engine/RuntimeData/Public/MaterialExporter.h"

#include <filesystem>
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

	bool MaterialExporter::operator()(
		AssetManager& /*assetManager*/,
		const AssetMeta& /*meta*/,
		const AssetObject* pObject,
		const std::string& outPath,
		std::string* pOutError) const
	{
		if (!pObject)
		{
			setErr(pOutError, "MaterialAssetExporter: object is null.");
			return false;
		}

		const Material* mat = AssetObjectCast<Material>(pObject);
		if (!mat)
		{
			setErr(pOutError, "MaterialAssetExporter: type mismatch (not MaterialAsset).");
			return false;
		}

		if (outPath.empty())
		{
			setErr(pOutError, "MaterialAssetExporter: outPath is empty.");
			return false;
		}

		std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());

		json j;
		j["Format"] = "shzmat";
		j["Version"] = 2; // updated

		j["Name"] = mat->GetName();
		j["TemplateName"] = mat->GetTemplateName();

		j["Options"] = json{
			{"BlendMode", (int)mat->GetBlendMode()},
			{"CullMode", (int)mat->GetCullMode()},
			{"FrontCounterClockwise", mat->GetFrontCounterClockwise()},
			{"DepthEnable", mat->GetDepthEnable()},
			{"DepthWriteEnable", mat->GetDepthWriteEnable()},
			{"DepthFunc", (int)mat->GetDepthFunc()},
		};

		// ---------------------------------------------------------------------
		// Values: now rely on simplified MaterialValueBlob map (no snapshot cache)
		// ---------------------------------------------------------------------
		j["Values"] = json::array();
		for (const auto& kv : mat->GetAllValues())
		{
			const std::string& name = kv.first;
			const MaterialValueBlob& v = kv.second;

			j["Values"].push_back(json{
				{"Name", name},
				{"Type", (int)v.Type},
				{"Data", v.Data},
				});
		}

		// ---------------------------------------------------------------------
		// Resources: now rely on simplified MaterialTexture map + sampler overrides
		// - Texture ref is stored in MaterialTexture::Texture
		// - Sampler overrides are stored per-template-resource in MaterialTextureBinding
		// ---------------------------------------------------------------------
		j["Resources"] = json::array();

		const uint32 resCount = mat->GetTemplate().GetResourceCount();
		for (uint32 i = 0; i < resCount; ++i)
		{
			const MaterialResourceDesc& rr = mat->GetTemplate().GetResource(i);
			if (!IsTextureType(rr.Type))
			{
				continue;
			}

			json rj;
			rj["Name"] = rr.Name;
			rj["Type"] = (int)rr.Type;

			// Texture (optional)
			if (const MaterialTexture* mt = mat->GetTextureOrNull(rr.Name))
			{
				if (mt->Texture.IsValid())
				{
					rj["SourcePath"] = mt->Texture.GetID().SourcePath;
				}
				else
				{
					rj["SourcePath"] = "";
				}
			}
			else
			{
				rj["SourcePath"] = "";
			}

			// Sampler override (optional)
			if (i < mat->GetTextureBindingCount())
			{
				const MaterialTextureBinding& tb = mat->GetTextureBinding(i);
				rj["HasSamplerOverride"] = tb.bHasSamplerOverride;
				if (tb.bHasSamplerOverride)
				{
					rj["SamplerOverrideDesc"] = samplerToJson(tb.SamplerOverrideDesc);
				}
			}
			else
			{
				rj["HasSamplerOverride"] = false;
			}

			j["Resources"].push_back(std::move(rj));
		}

		std::ofstream out(outPath, std::ios::trunc);
		if (!out.is_open())
		{
			setErr(pOutError, "MaterialAssetExporter: failed to open output json.");
			return false;
		}

		out << j.dump(2);
		return true;
	}
} // namespace shz
