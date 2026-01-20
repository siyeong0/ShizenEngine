#include "pch.h"
#include "Engine/AssetRuntime/Pipeline/Public/TextureImporter.h"
#include "Engine/AssetRuntime/Common/AssetTypeTraits.h"

namespace shz
{
	// -------------------------------------------------------------------------
	// Loader implementation
	// -------------------------------------------------------------------------
	bool TextureImporter::operator()(
		const AssetRegistry::AssetMeta& meta,
		std::unique_ptr<AssetObject>& outObject,
		uint64& outResidentBytes,
		std::string& outError) const
	{
		outObject.reset();
		outResidentBytes = 0;
		outError.clear();

		ASSERT(!meta.SourcePath.empty(), "Source path is empty.");

		TextureAsset asset = {};
		asset.SetSourcePath(meta.SourcePath);
		asset.SetName(std::filesystem::path(meta.SourcePath).filename().string().c_str());

		uint64 diskBytes = 0;
		tryGetFileSize(meta.SourcePath, diskBytes);

		ASSERT(asset.IsValid() && asset.ValidateOptions(), "Invalid asset/options.");

		outObject = std::make_unique<TypedAssetObject<TextureAsset>>(static_cast<TextureAsset&&>(asset));

		outResidentBytes = diskBytes;
		return true;
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	bool TextureImporter::tryGetFileSize(const std::string& path, uint64& outBytes) noexcept
	{
		namespace fs = std::filesystem;

		outBytes = 0;

		std::error_code ec;
		const fs::path p = fs::path(path);

		if (!fs::exists(p, ec) || ec)
			return false;

		const auto sz = fs::file_size(p, ec);
		if (ec)
			return false;

		outBytes = static_cast<uint64>(sz);
		return true;
	}
} // namespace shz
