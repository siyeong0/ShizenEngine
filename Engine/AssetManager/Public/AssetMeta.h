#pragma once
#include <string>
#include <variant>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"

#include "Engine/Image/Public/TextureLoader.h"
#include "Engine/AssetManager/Public/AssetID.hpp"
#include "Engine/RuntimeData/Public/MaterialTypes.h"
#include "Engine/RuntimeData/Public/TerrainHeightField.h"

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

    struct AssimpImportSettings final
    {
        bool bTriangulate = true;
        bool bJoinIdenticalVertices = true;
        bool bGenNormals = true;
        bool bGenSmoothNormals = true;
        bool bGenTangents = false;
        bool bCalcTangentSpace = false;

        bool bFlipUVs = true;
        bool bConvertToLeftHanded = true;

        float32 UniformScale = 1.0f;

        bool bMergeMeshes = true;

        bool bImportMaterials = true;
        bool bRegisterTextureAssets = true;

        std::string OutputName = {};
        std::string OutputDirectory = {};
    };

    struct StaticMeshLoadSettings final
    {
        // reserved
    };

    struct MaterialLoadSettings final
    {
        // reserved
    };

    struct TerrainHeightFieldImportSetting final
    {
        float WorldSpacingX = 1.f;
        float WorldSpacingZ = 1.f;

        float HeightScale = 10000.0f;
        float HeightOffset = 0.f;

        HEIGHT_FIELD_SAMPLE_FORMAT ForceSampleFormat = HEIGHT_FIELD_SAMPLE_FORMAT_UNKNOWN;
    };

    using AssetImportSetting = std::variant<
        std::monostate,
        TextureImportSettings,
        MaterialImportSettings,
        AssimpImportSettings,
        StaticMeshLoadSettings,
        MaterialLoadSettings,
        TerrainHeightFieldImportSetting>;

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

        const AssimpImportSettings* TryGetAssimpMeta() const noexcept { return std::get_if<AssimpImportSettings>(&Payload); }
        AssimpImportSettings* TryGetAssimpMeta() noexcept { return std::get_if<AssimpImportSettings>(&Payload); }

        const StaticMeshLoadSettings* TryGetStaticMeshLoadMeta() const noexcept { return std::get_if<StaticMeshLoadSettings>(&Payload); }
        StaticMeshLoadSettings* TryGetStaticMeshLoadMeta() noexcept { return std::get_if<StaticMeshLoadSettings>(&Payload); }

        const MaterialLoadSettings* TryGetMaterialLoadMeta() const noexcept { return std::get_if<MaterialLoadSettings>(&Payload); }
        MaterialLoadSettings* TryGetMaterialLoadMeta() noexcept { return std::get_if<MaterialLoadSettings>(&Payload); }

		const TerrainHeightFieldImportSetting* TryGetTerrainHeightFieldMeta() const noexcept { return std::get_if<TerrainHeightFieldImportSetting>(&Payload); }
		TerrainHeightFieldImportSetting* TryGetTerrainHeightFieldMeta() noexcept { return std::get_if<TerrainHeightFieldImportSetting>(&Payload); }
    };
} // namespace shz
