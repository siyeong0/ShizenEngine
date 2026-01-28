#pragma once
#include "Engine/ECS/Public/Common.h"

#include "Engine/AssetManager/Public/AssetRef.hpp"
#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/Renderer/Public/RenderScene.h"

namespace shz
{
	COMPONENT CMeshRenderer final
	{
		AssetRef<StaticMesh> MeshRef = {};
		Handle<RenderScene::RenderObject> RenderObjectHandle = {};

		bool bCastShadow = true;
	};
} // namespace shz
