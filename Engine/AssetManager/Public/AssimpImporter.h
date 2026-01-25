#pragma once
#include <string>
#include <memory>

#include "Primitives/BasicTypes.h"

#include "Engine/AssetManager/Public/AssetMeta.h"
#include "Engine/AssetManager/Public/AssetObject.h"

#include "Engine/AssetManager/Public/AssimpAsset.h"
#include "Engine/RuntimeData/Public/StaticMeshAsset.h"

namespace shz
{
	class AssetManager;

	class AssimpImporter final
	{
	public:
		std::unique_ptr<AssetObject> operator()(
			AssetManager& assetManager,
			const AssetMeta& meta,
			uint64* pOutResidentBytes,
			std::string* pOutError) const;
	};

	// ------------------------------------------------------------
	// AssimpAsset -> StaticMeshAsset 
	// ------------------------------------------------------------
	bool BuildStaticMeshAsset(
		const AssimpAsset& assimpAsset,
		StaticMeshAsset* pOutMesh,
		const AssimpImportSettings& settings,
		std::string* outError = nullptr,
		AssetManager* pAssetManager = nullptr);
} // namespace shz
