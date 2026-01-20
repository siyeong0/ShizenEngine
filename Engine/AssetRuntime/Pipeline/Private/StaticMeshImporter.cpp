#include "pch.h"
#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshImporter.h"

#include <filesystem>

#include "Engine/AssetRuntime/Pipeline/Public/AssimpImporter.h"

namespace shz
{
	static inline void setError(std::string* pOutError, const char* msg)
	{
		if (pOutError)
		{
			*pOutError = (msg != nullptr) ? msg : "Unknown error.";
		}
	}

	static inline void setError(std::string* pOutError, const std::string& msg)
	{
		if (pOutError)
		{
			*pOutError = msg;
		}
	}

	std::unique_ptr<AssetObject> StaticMeshImporter::operator()(
		AssetManager& assetManager,
		const AssetMeta& meta,
		uint64* pOutResidentBytes,
		std::string* pOutError) const
	{
		ASSERT(pOutResidentBytes != nullptr, "Invalid argument. pOutResidentBytes is null.");
		*pOutResidentBytes = 0;

		if (pOutError)
		{
			pOutError->clear();
		}

		// ------------------------------------------------------------
		// Validate meta
		// ------------------------------------------------------------
		if (meta.SourcePath.empty())
		{
			ASSERT(false, "StaticMeshImporter: meta.SourcePath is empty.");
			setError(pOutError, "StaticMeshImporter: SourcePath is empty.");
			return {};
		}

		// Import settings are stored in meta payload (import/export only).
		const StaticMeshImportSettings* pSettings = meta.TryGetStaticMeshMeta();

		AssimpImportOptions opt = {};

		// ------------------------------------------------------------
		// Translate meta settings -> AssimpImportOptions
		// ------------------------------------------------------------
		// NOTE:
		// If you don't have StaticMeshImportSettings yet, either:
		// - add it to AssetMeta payload, or
		// - keep defaults here.
		if (pSettings)
		{
			// Keep these assignments aligned with your actual settings struct.
			// Rename fields if needed.
			opt.Triangulate = pSettings->bTriangulate;
			opt.JoinIdenticalVertices = pSettings->bJoinIdenticalVertices;
			opt.GenNormals = pSettings->bGenNormals;
			opt.GenSmoothNormals = pSettings->bGenSmoothNormals;
			opt.GenTangents = pSettings->bGenTangents;
			opt.CalcTangentSpace = pSettings->bCalcTangentSpace;

			opt.FlipUVs = pSettings->bFlipUVs;
			opt.ConvertToLeftHanded = pSettings->bConvertToLeftHanded;

			opt.UniformScale = pSettings->UniformScale;

			opt.MergeMeshes = pSettings->bMergeMeshes;

			opt.ImportMaterials = pSettings->bImportMaterials;
			opt.RegisterTextureAssets = pSettings->bRegisterTextureAssets;
		}

		// If we are going to register texture assets, AssetManager must be provided.
		AssetManager* pMgr = nullptr;
		if (opt.RegisterTextureAssets)
		{
			pMgr = &assetManager;
		}

		// ------------------------------------------------------------
		// Import
		// ------------------------------------------------------------
		StaticMeshAsset mesh = {};

		std::string importErr;
		const bool ok = AssimpImporter::LoadStaticMeshAsset(
			meta.SourcePath,
			&mesh,
			opt,
			&importErr,
			pMgr);

		if (!ok)
		{
			ASSERT(false, "StaticMeshImporter: AssimpImporter::LoadStaticMeshAsset failed.");
			if (importErr.empty())
			{
				importErr = "StaticMeshImporter: Import failed.";
			}
			setError(pOutError, importErr);
			return {};
		}

		if (!mesh.IsValid())
		{
			ASSERT(false, "StaticMeshImporter: Imported mesh is invalid.");
			setError(pOutError, "StaticMeshImporter: Imported mesh is invalid.");
			return {};
		}

		// ------------------------------------------------------------
		// Estimate resident bytes (CPU data size)
		// ------------------------------------------------------------
		// NOTE:
		// This is a conservative estimate for budget/GC.
		// If you later strip CPU data after creating GPU buffers, update accordingly.
		{
			uint64 bytes = 0;

			bytes += static_cast<uint64>(mesh.GetPositions().size()) * sizeof(float3);
			bytes += static_cast<uint64>(mesh.GetNormals().size()) * sizeof(float3);
			bytes += static_cast<uint64>(mesh.GetTangents().size()) * sizeof(float3);
			bytes += static_cast<uint64>(mesh.GetTexCoords().size()) * sizeof(float2);

			if (mesh.GetIndexType() == VT_UINT32)
			{
				bytes += static_cast<uint64>(mesh.GetIndicesU32().size()) * sizeof(uint32);
			}
			else
			{
				bytes += static_cast<uint64>(mesh.GetIndicesU16().size()) * sizeof(uint16);
			}

			bytes += static_cast<uint64>(mesh.GetSections().size()) * sizeof(StaticMeshAsset::Section);
			bytes += static_cast<uint64>(mesh.GetMaterialSlots().size()) * sizeof(MaterialAsset);

			*pOutResidentBytes = bytes;
		}

		// ------------------------------------------------------------
		// Return typed asset object
		// ------------------------------------------------------------
		return std::make_unique<TypedAssetObject<StaticMeshAsset>>(static_cast<StaticMeshAsset&&>(mesh));
	}

} // namespace shz
