#pragma once
#include <memory>
#include <string>

#include "Engine/AssetManager/Public/AssetObject.h"
#include "Engine/AssetManager/Public/AssetMeta.h"

namespace shz
{
	class AssetManager;

	class StaticMeshAssetImporter final
	{
	public:
		std::unique_ptr<AssetObject> operator()(
			AssetManager& assetManager,
			const AssetMeta& meta,
			uint64* pOutResidentBytes,
			std::string* pOutError) const;
	};
} // namespace shz
