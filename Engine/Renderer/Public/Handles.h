#pragma once
#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"

namespace shz
{
	struct MeshHandleTag {};
	struct MaterialHandleTag {};
	struct TextureHandleTag {};
	struct RenderObjectIdTag {};
	struct LightIdTag {};

	using MeshHandle = Handle<MeshHandleTag>;
	using MaterialHandle = Handle<MaterialHandleTag>;
	using TextureHandle = Handle<TextureHandleTag>;
	using RenderObjectId = Handle<TextureHandleTag>;
	using LightId = Handle<LightIdTag>;
}
