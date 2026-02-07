#include "pch.h"
#include "Engine/RuntimeData/Public/MaterialExporter.h"

#include "Engine/Core/Json/BasicTypesJsonAdaptor.hpp"
#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/RuntimeData/Public/Material.h"
#include "Engine/AssetManager/Public/AssetMeta.h"
#include "Engine/AssetManager/Public/AssetObject.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace shz
{
	bool MaterialExporter::operator()(
		AssetManager& assetManager,
		const AssetMeta& meta,
		const AssetObject* pObject,
		const std::string& outPath,
		std::string* pOutError) const
	{
		using json = nlohmann::json;

		(void)assetManager;
		(void)meta;

		auto fail = [&](const char* msg) -> bool
			{
				if (pOutError) *pOutError = msg;
				return false;
			};

		ASSERT(pObject != nullptr, "Object is null.");

		const Material* pMat = AssetObjectCast<Material>(pObject);
		ASSERT(pMat, "Failed to cast AssetObject to Material.");

		auto assetRefToJson = [&](const AssetRef<Texture>& r) -> json
			{
				json jr;
				jr["SourcePath"] = r.GetSourcePath();
				return jr;
			};

		auto writeTypedValue = [&](const MaterialValueBlob& blob) -> json
			{
				json jv;
				jv["Type"] = static_cast<int>(blob.Type);
				jv["ByteSize"] = static_cast<int>(blob.Data.size());

				const uint32 want = ValueTypeByteSize(blob.Type);
				const bool sizeOk = (want != 0) ? (blob.Data.size() == want) : false;

				// ------------------------------------------------------------
				// typed encoding: use BasicTypesJsonAdaptor for vec types
				// ------------------------------------------------------------
				if (sizeOk)
				{
					jv["Encoding"] = "typed";

					// Scalar cases: no vector adaptor needed
					if (blob.Type == MATERIAL_VALUE_TYPE_FLOAT)
					{
						float v = 0.0f;
						std::memcpy(&v, blob.Data.data(), sizeof(float));
						jv["Value"] = v;
						return jv;
					}
					if (blob.Type == MATERIAL_VALUE_TYPE_INT)
					{
						int32 v = 0;
						std::memcpy(&v, blob.Data.data(), sizeof(int32));
						jv["Value"] = v;
						return jv;
					}
					if (blob.Type == MATERIAL_VALUE_TYPE_UINT)
					{
						uint32 v = 0;
						std::memcpy(&v, blob.Data.data(), sizeof(uint32));
						jv["Value"] = v;
						return jv;
					}

					// Vector cases: decode bytes -> vec, then json = vec;
					switch (blob.Type)
					{
					case MATERIAL_VALUE_TYPE_FLOAT2:
					{
						float2 v;
						std::memcpy(&v, blob.Data.data(), sizeof(float2));
						jv["Value"] = v; // <-- adaptor kicks in
						return jv;
					}
					case MATERIAL_VALUE_TYPE_FLOAT3:
					{
						float3 v;
						std::memcpy(&v, blob.Data.data(), sizeof(float3));
						jv["Value"] = v;
						return jv;
					}
					case MATERIAL_VALUE_TYPE_FLOAT4:
					{
						float4 v;
						std::memcpy(&v, blob.Data.data(), sizeof(float4));
						jv["Value"] = v;
						return jv;
					}

					case MATERIAL_VALUE_TYPE_INT2:
					{
						int2 v;
						std::memcpy(&v, blob.Data.data(), sizeof(int2));
						jv["Value"] = v;
						return jv;
					}
					case MATERIAL_VALUE_TYPE_INT3:
					{
						int3 v;
						std::memcpy(&v, blob.Data.data(), sizeof(int3));
						jv["Value"] = v;
						return jv;
					}
					case MATERIAL_VALUE_TYPE_INT4:
					{
						int4 v;
						std::memcpy(&v, blob.Data.data(), sizeof(int4));
						jv["Value"] = v;
						return jv;
					}

					case MATERIAL_VALUE_TYPE_UINT2:
					{
						uint2 v;
						std::memcpy(&v, blob.Data.data(), sizeof(uint2));
						jv["Value"] = v;
						return jv;
					}
					case MATERIAL_VALUE_TYPE_UINT3:
					{
						uint3 v;
						std::memcpy(&v, blob.Data.data(), sizeof(uint3));
						jv["Value"] = v;
						return jv;
					}
					case MATERIAL_VALUE_TYPE_UINT4:
					{
						uint4 v;
						std::memcpy(&v, blob.Data.data(), sizeof(uint4));
						jv["Value"] = v;
						return jv;
					}

					default:
						break;
					}

					// should not reach if SizeOfMaterialValueType is correct
					jv.erase("Encoding");
				}

				// ------------------------------------------------------------
				// bytes encoding fallback
				// ------------------------------------------------------------
				{
					json bytes = json::array();
					bytes.reserve(blob.Data.size());
					for (uint8 b : blob.Data)
						bytes.push_back((int)b);

					jv["Value"] = std::move(bytes);
					jv["Encoding"] = "bytes";
					jv["Warning"] = "ByteSize mismatched with Type; stored as raw bytes.";
				}

				return jv;
			};

		// ---------------------------------------------------------------------
		// Build JSON
		// ---------------------------------------------------------------------
		json root;
		root["Schema"] = "shz.Material.v1";
		root["Name"] = pMat->GetName();

		root["Options"]["BlendMode"] = static_cast<int>(pMat->GetBlendMode());
		root["Options"]["CullMode"] = static_cast<int>(pMat->GetCullMode());
		root["Options"]["FrontCCW"] = pMat->GetFrontCounterClockwise();
		root["Options"]["DepthEnable"] = pMat->GetDepthEnable();
		root["Options"]["DepthWriteEnable"] = pMat->GetDepthWriteEnable();
		root["Options"]["DepthFunc"] = static_cast<int>(pMat->GetDepthFunc());

		// Values
		{
			json jvals = json::object();
			for (const auto& kv : pMat->GetAllValues())
				jvals[kv.first] = writeTypedValue(kv.second);
			root["Values"] = std::move(jvals);
		}

		// Textures
		{
			json jtex = json::object();
			for (const auto& kv : pMat->GetAllTextures())
			{
				json jt;
				jt["Texture"] = assetRefToJson(kv.second.Texture);
				jtex[kv.first] = std::move(jt);
			}
			root["Textures"] = std::move(jtex);
		}

		// ---------------------------------------------------------------------
		// Write file
		// ---------------------------------------------------------------------
		std::ofstream ofs(outPath, std::ios::binary);
		if (!ofs.is_open())
		{
			ASSERTION_FAILED("Failed to open output file.");
			return fail("Failed to open output file.");
		}

		ofs << root.dump(2);
		if (!ofs.good())
		{
			ASSERTION_FAILED("Failed to write json.");
			return fail("Failed to write json.");
		}

		return true;
	}
} // namespace shz
