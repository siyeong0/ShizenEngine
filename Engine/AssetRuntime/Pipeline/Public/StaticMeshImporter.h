#pragma once
#include <string>
#include <memory>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Common/AssetObject.h"
#include "Engine/AssetRuntime/AssetData/Public/StaticMeshAsset.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetMeta.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetManager.h"

namespace shz
{
	class StaticMeshImporter final
	{
	public:
		std::unique_ptr<AssetObject> operator()(
			AssetManager& assetManager,
			const AssetMeta& meta,
			uint64* pOutResidentBytes,
			std::string* pOutError) const;
	};
} // namespace shz
