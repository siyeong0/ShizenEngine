#pragma once
#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/RuntimeData/Public/Texture.h"
#include "Engine/RuntimeData/Public/Material.h"
#include "Engine/AssetManager/Public/AssimpAsset.h"

namespace shz
{
	static constexpr AssetTypeID ASSET_TYPE_STATIC_MESH = 0x1001;
	static constexpr AssetTypeID ASSET_TYPE_TEXTURE = 0x1002;
	static constexpr AssetTypeID ASSET_TYPE_MATERIAL_INSTANCE = 0x1003;
	static constexpr AssetTypeID ASSET_TYPE_ASSIMP_SCENE = 0x1004;

	template<> struct AssetTypeTraits<StaticMesh> { static constexpr AssetTypeID TypeID = ASSET_TYPE_STATIC_MESH; };
	template<> struct AssetTypeTraits<Texture> { static constexpr AssetTypeID TypeID = ASSET_TYPE_TEXTURE; };
	template<> struct AssetTypeTraits<Material> { static constexpr AssetTypeID TypeID = ASSET_TYPE_MATERIAL_INSTANCE; };
	template<> struct AssetTypeTraits<AssimpAsset> { static constexpr AssetTypeID TypeID = ASSET_TYPE_ASSIMP_SCENE; };
}
