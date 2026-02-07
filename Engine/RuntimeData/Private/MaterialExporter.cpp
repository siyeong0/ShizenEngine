#include "pch.h"
#include "Engine/RuntimeData/Public/MaterialImporter.h"

#include "Engine/Core/Json/BasicTypesJsonAdaptor.hpp"
#include "Engine/RuntimeData/Public/Material.h"
#include "Engine/AssetManager/Public/AssetManager.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace shz
{
	std::unique_ptr<AssetObject> MaterialImporter::operator()(
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

		ASSERT(!meta.SourcePath.empty(), "meta.SourcePath is empty.");

		std::ifstream in(meta.SourcePath, std::ios::binary);
		ASSERT(in.is_open(), "Failed to open material json.");

		json root;
		try
		{
			in >> root;
		}
		catch (...)
		{
			setErr("MaterialImporter: json parse failed.");
			return {};
		}

		if (root.value("Schema", "") != "shz.Material.v1")
		{
			setErr("MaterialImporter: invalid schema.");
			return {};
		}

		const std::string matName = root.value("Name", "");
		Material m(matName);

		// Options
		if (root.contains("Options") && root["Options"].is_object())
		{
			const json& opt = root["Options"];
			m.SetBlendMode((MATERIAL_BLEND_MODE)opt.value("BlendMode", (int)m.GetBlendMode()));
			m.SetCullMode((CULL_MODE)opt.value("CullMode", (int)m.GetCullMode()));
			m.SetCCW(opt.value("FrontCCW", m.GetFrontCounterClockwise()));

			m.SetDepthTestEnable(opt.value("DepthEnable", m.GetDepthEnable()));
			m.SetDepthWriteEnable(opt.value("DepthWriteEnable", m.GetDepthWriteEnable()));
			m.SetDepthComparisonFunc((COMPARISON_FUNCTION)opt.value("DepthFunc", (int)m.GetDepthFunc()));
		}

		// Values
		auto readBlob = [&](const json& jv, MaterialValueBlob& out) -> bool
			{
				ASSERT(jv.is_object(), "Invalid json for MaterialValueBlob.");

				out.Type = (MATERIAL_VALUE_TYPE)jv.value("Type", (int)MATERIAL_VALUE_TYPE_UNKNOWN);
				const std::string enc = jv.value("Encoding", std::string{});

				const uint32 want = ValueTypeByteSize(out.Type);
				ASSERT(want > 0, "Invalid MATERIAL_VALUE_TYPE in MaterialValueBlob.");

				// bytes
				if (enc == "bytes")
				{
					ASSERT(jv.contains("Value") && jv["Value"].is_array(), "Invalid json for MaterialValueBlob bytes encoding.");

					const json& arr = jv["Value"];
					out.Data.resize(arr.size());
					for (size_t i = 0; i < arr.size(); ++i)
					{
						out.Data[i] = arr[i].is_number_integer() ? (uint8)arr[i].get<int>() : 0u;
					}

					return !out.Data.empty();
				}

				// typed
				if (enc == "typed")
				{
					ASSERT(jv.contains("Value"), "Invalid json for MaterialValueBlob typed encoding.");

					out.Data.resize(want);

					// Scalar
					if (out.Type == MATERIAL_VALUE_TYPE_FLOAT)
					{
						const float v = jv["Value"].get<float>();
						std::memcpy(out.Data.data(), &v, sizeof(float));
						return true;
					}
					if (out.Type == MATERIAL_VALUE_TYPE_INT)
					{
						const int32 v = jv["Value"].get<int32>();
						std::memcpy(out.Data.data(), &v, sizeof(int32));
						return true;
					}
					if (out.Type == MATERIAL_VALUE_TYPE_UINT)
					{
						const uint32 v = jv["Value"].get<uint32>();
						std::memcpy(out.Data.data(), &v, sizeof(uint32));
						return true;
					}

					switch (out.Type)
					{
					case MATERIAL_VALUE_TYPE_FLOAT2:
					{
						const float2 v = jv["Value"].get<float2>();
						std::memcpy(out.Data.data(), &v, sizeof(float2));
						return true;
					}
					case MATERIAL_VALUE_TYPE_FLOAT3:
					{
						const float3 v = jv["Value"].get<float3>();
						std::memcpy(out.Data.data(), &v, sizeof(float3));
						return true;
					}
					case MATERIAL_VALUE_TYPE_FLOAT4:
					{
						const float4 v = jv["Value"].get<float4>();
						std::memcpy(out.Data.data(), &v, sizeof(float4));
						return true;
					}

					case MATERIAL_VALUE_TYPE_INT2:
					{
						const int2 v = jv["Value"].get<int2>();
						std::memcpy(out.Data.data(), &v, sizeof(int2));
						return true;
					}
					case MATERIAL_VALUE_TYPE_INT3:
					{
						const int3 v = jv["Value"].get<int3>();
						std::memcpy(out.Data.data(), &v, sizeof(int3));
						return true;
					}
					case MATERIAL_VALUE_TYPE_INT4:
					{
						const int4 v = jv["Value"].get<int4>();
						std::memcpy(out.Data.data(), &v, sizeof(int4));
						return true;
					}

					case MATERIAL_VALUE_TYPE_UINT2:
					{
						const uint2 v = jv["Value"].get<uint2>();
						std::memcpy(out.Data.data(), &v, sizeof(uint2));
						return true;
					}
					case MATERIAL_VALUE_TYPE_UINT3:
					{
						const uint3 v = jv["Value"].get<uint3>();
						std::memcpy(out.Data.data(), &v, sizeof(uint3));
						return true;
					}
					case MATERIAL_VALUE_TYPE_UINT4:
					{
						const uint4 v = jv["Value"].get<uint4>();
						std::memcpy(out.Data.data(), &v, sizeof(uint4));
						return true;
					}

					default:
						return false;
					}
				}

				return false;
			};

		if (root.contains("Values") && root["Values"].is_object())
		{
			const json& vals = root["Values"];
			for (auto it = vals.begin(); it != vals.end(); ++it)
			{
				const std::string paramName = it.key();
				const json& jv = it.value();

				MaterialValueBlob blob;
				if (readBlob(jv, blob))
				{
					m.SetRawValue(paramName, blob.Type, blob.Data.data(), (uint32)blob.Data.size());
				}
			}
		}

		// Textures
		if (root.contains("Textures") && root["Textures"].is_object())
		{
			const json& texRoot = root["Textures"];
			for (auto it = texRoot.begin(); it != texRoot.end(); ++it)
			{
				const std::string slotName = it.key();
				const json& jt = it.value();
				ASSERT(jt.is_object(), "Invalid json for Material texture.");
				ASSERT(jt.contains("Texture") && jt["Texture"].is_object(), "Invalid json for Material texture.");

				const std::string srcPath = jt["Texture"].value("SourcePath", std::string{});
				if (!srcPath.empty())
				{
					AssetRef<Texture> ref = assetManager.RegisterAsset<Texture>(srcPath);
					ASSERT(ref.IsValid(), "Failed to register texture asset: %s", srcPath.c_str());
					if (ref.IsValid())
					{
						m.SetTexture(slotName, ref);
					}
				}
			}
		}

		*pOutResidentBytes = (uint64)m.GetName().size();
		return std::make_unique<TypedAssetObject<Material>>(static_cast<Material&&>(m));
	}
} // namespace shz
