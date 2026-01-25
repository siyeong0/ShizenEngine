#pragma once
#include <string>
#include <memory>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetManager/Public/AssetObject.h"
#include "Engine/RuntimeData/Public/Texture.h"
#include "Engine/AssetManager/Public/AssetMeta.h"
#include "Engine/AssetManager/Public/AssetManager.h"

namespace shz
{
	class TextureImporter final
	{
	public:
		std::unique_ptr<AssetObject> operator()(
			AssetManager& assetManager,
			const AssetMeta& meta,
			uint64* pOutResidentBytes,
			std::string* pOutError) const;
	};
} // namespace shz
