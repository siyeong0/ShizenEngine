#pragma once
#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
namespace shz
{
    struct StaticMeshAssetHandleTag {};
    struct MaterialAssetHandleTag {};
    struct TextureAssetHandleTag {};

    using StaticMeshAssetHandle = Handle<StaticMeshAssetHandleTag>;
    using MaterialAssetHandle = Handle<MaterialAssetHandleTag>;
    using TextureAssetHandle = Handle<TextureAssetHandleTag>;
}