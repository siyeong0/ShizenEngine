#include "pch.h"
#include "Engine/AssetRuntime/Pipeline/Public/MaterialExporter.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "Engine/AssetRuntime/AssetData/Public/MaterialAsset.h"

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

	bool MaterialAssetExporter::operator()(
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

		const MaterialAsset* mat = AssetObjectCast<MaterialAsset>(pObject);
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
		j["Version"] = 1;

		j["Name"] = mat->GetName();
		j["TemplateKey"] = mat->GetTemplateKey();

		const auto& o = mat->GetOptions();
		j["Options"] = json{
			{"BlendMode", (int)o.BlendMode},
			{"CullMode", (int)o.CullMode},
			{"FrontCounterClockwise", o.FrontCounterClockwise},
			{"DepthEnable", o.DepthEnable},
			{"DepthWriteEnable", o.DepthWriteEnable},
			{"DepthFunc", (int)o.DepthFunc},
			{"TextureBindingMode", (int)o.TextureBindingMode},
			{"LinearWrapSamplerName", o.LinearWrapSamplerName},
			{"LinearWrapSamplerDesc", samplerToJson(o.LinearWrapSamplerDesc)},
			{"TwoSided", o.bTwoSided},
			{"CastShadow", o.bCastShadow},
		};

		j["Values"] = json::array();
		for (uint32 i = 0; i < mat->GetValueOverrideCount(); ++i)
		{
			const auto& v = mat->GetValueOverride(i);
			j["Values"].push_back(json{
				{"StableID", v.StableID},
				{"Name", v.Name},
				{"Type", (int)v.Type},
				{"Data", v.Data},
				});
		}

		j["Resources"] = json::array();
		for (uint32 i = 0; i < mat->GetResourceBindingCount(); ++i)
		{
			const auto& r = mat->GetResourceBinding(i);
			const AssetID tid = r.TextureRef.GetID();

			json rj;
			rj["StableID"] = r.StableID;
			rj["Name"] = r.Name;
			rj["Type"] = (int)r.Type;
			rj["TextureAssetID"] = json{ {"Hi", tid.Hi}, {"Lo", tid.Lo} };
			rj["HasSamplerOverride"] = r.bHasSamplerOverride;
			if (r.bHasSamplerOverride)
				rj["SamplerOverrideDesc"] = samplerToJson(r.SamplerOverrideDesc);

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
}
