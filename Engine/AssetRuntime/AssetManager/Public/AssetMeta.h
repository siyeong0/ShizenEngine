#pragma once
#include <string>
#include <variant>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Common/AssetID.hpp"

#include "Tools/Image/Public/TextureLoader.h"

#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Engine/Material/Public/MaterialTypes.h"

namespace shz
{
	struct TextureImportSettings final
	{
		bool bSRGB = false;
		bool bGenerateMips = true;
		bool bFlipVertically = false;
		bool bPremultiplyAlpha = false;

		TEXTURE_LOAD_MIP_FILTER     MipFilter = TEXTURE_LOAD_MIP_FILTER_DEFAULT;
		TEXTURE_LOAD_COMPRESS_MODE  CompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;

		uint32 UniformImageClipDim = 0;

		TextureComponentMapping Swizzle = TextureComponentMapping::Identity();
	};

	struct MaterialImportSettings final
	{
		std::string TemplateKey = {};
	};

	struct StaticMeshImportSettings final
	{
		bool bTriangulate = true;
		bool bJoinIdenticalVertices = true;
		bool bGenNormals = true;
		bool bGenSmoothNormals = true;
		bool bGenTangents = false;
		bool bCalcTangentSpace = false;

		bool bFlipUVs = false;
		bool bConvertToLeftHanded = true;
		
		float32 UniformScale = 1.0f;

		bool bMergeMeshes = true;

		bool bImportMaterials = true;
		bool bRegisterTextureAssets = true;
	};

	using AssetImportSetting = std::variant<std::monostate, TextureImportSettings, MaterialImportSettings, StaticMeshImportSettings>;


	struct AssetMeta final
	{
		AssetTypeID TypeID = 0;
		std::string SourcePath = {};
		std::string Name = {};

		AssetImportSetting Payload = {};

		// Convenience
		const TextureImportSettings* TryGetTextureMeta() const noexcept { return std::get_if<TextureImportSettings>(&Payload); }
		TextureImportSettings* TryGetTextureMeta() noexcept { return std::get_if<TextureImportSettings>(&Payload); }

		const MaterialImportSettings* TryGetMaterialMeta() const noexcept { return std::get_if<MaterialImportSettings>(&Payload); }
		MaterialImportSettings* TryGetMaterialMeta() noexcept { return std::get_if<MaterialImportSettings>(&Payload); }

		const StaticMeshImportSettings* TryGetStaticMeshMeta() const noexcept { return std::get_if<StaticMeshImportSettings>(&Payload); }
		StaticMeshImportSettings* TryGetStaticMeshMeta() noexcept { return std::get_if<StaticMeshImportSettings>(&Payload); }
	};
} // namespace shz
