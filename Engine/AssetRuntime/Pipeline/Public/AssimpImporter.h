#pragma once
#include <string>
#include <memory>

#include "Primitives/BasicTypes.h"

#include "Engine/AssetRuntime/AssetManager/Public/AssetMeta.h" // <- 여기의 AssimpImportSettings 사용
#include "Engine/AssetRuntime/Common/AssetObject.h"

#include "Engine/AssetRuntime/AssetData/Public/AssimpAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/StaticMeshAsset.h"

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
