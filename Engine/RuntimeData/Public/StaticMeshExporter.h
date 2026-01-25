#pragma once
#include <string>

#include "Engine/AssetManager/Public/AssetMeta.h"
#include "Engine/AssetManager/Public/AssetObject.h"

namespace shz
{
	class AssetManager;

	class StaticMeshExporter final
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
