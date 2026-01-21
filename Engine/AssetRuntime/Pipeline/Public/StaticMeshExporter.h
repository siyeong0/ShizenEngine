#pragma once
#include <string>

#include "Engine/AssetRuntime/AssetManager/Public/AssetMeta.h"
#include "Engine/AssetRuntime/Common/AssetObject.h"

namespace shz
{
	class AssetManager;

	class StaticMeshAssetExporter final
	{
	public:
		bool operator()(
			AssetManager& assetManager,
			const AssetMeta& meta,
			const AssetObject* pObject,
			const std::string& outPath,
			std::string* pOutError) const;
	};
}
