#pragma once
#include <memory>
#include <string>

#include "Engine/AssetRuntime/Common/AssetObject.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetMeta.h"

namespace shz
{
	class AssetManager;

	class MaterialAssetImporter final
	{
	public:
		std::unique_ptr<AssetObject> operator()(
			AssetManager& assetManager,
			const AssetMeta& meta,
			uint64* pOutResidentBytes,
			std::string* pOutError) const;
	};
}
