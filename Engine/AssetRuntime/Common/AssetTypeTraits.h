#pragma once
#include "Engine/AssetRuntime/AssetData/Public/StaticMeshAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/TextureAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/AssimpAsset.h"

namespace shz
{
	static constexpr AssetTypeID ASSET_TYPE_STATIC_MESH = 0x1001;
	static constexpr AssetTypeID ASSET_TYPE_TEXTURE = 0x1002;
	static constexpr AssetTypeID ASSET_TYPE_MATERIAL_INSTANCE = 0x1003;
	static constexpr AssetTypeID ASSET_TYPE_ASSIMP_SCENE = 0x1004;

	template<> struct AssetTypeTraits<StaticMeshAsset> { static constexpr AssetTypeID TypeID = ASSET_TYPE_STATIC_MESH; };
	template<> struct AssetTypeTraits<TextureAsset> { static constexpr AssetTypeID TypeID = ASSET_TYPE_TEXTURE; };
	template<> struct AssetTypeTraits<MaterialAsset> { static constexpr AssetTypeID TypeID = ASSET_TYPE_MATERIAL_INSTANCE; };
	template<> struct AssetTypeTraits<AssimpAsset> { static constexpr AssetTypeID TypeID = ASSET_TYPE_ASSIMP_SCENE; };
}
