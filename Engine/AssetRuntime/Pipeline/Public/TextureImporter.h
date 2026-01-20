#pragma once
#include <string>
#include <memory>
#include <functional>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Common/AssetObject.h"
#include "Engine/AssetRuntime/AssetData/Public/TextureAsset.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetRegistry.h"

namespace shz
{
	class TextureImporter final
	{
	public:
		bool operator()(
			const AssetRegistry::AssetMeta& meta,
			std::unique_ptr<AssetObject>& outObject,
			uint64& outResidentBytes,
			std::string& outError) const;

	private:
		static bool tryGetFileSize(const std::string& path, uint64& outBytes) noexcept;
	};
} // namespace shz