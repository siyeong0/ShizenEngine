#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

#include "Engine/AssetManager/Public/AssetRef.hpp"
#include "Engine/RuntimeData/Public/StaticMesh.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Physics/Public/Physics.h"

#include <vector>

#define COMPONENT struct

namespace shz
{
	//
	// Common
	//
	COMPONENT CName final
	{
		std::string Value = {};
	};

	// World transform (Euler rotation in radians: {Pitch, Yaw, Roll} or your convention)
	COMPONENT CTransform final
	{
		float3 Position = { 0, 0, 0 };
		float3 Rotation = { 0, 0, 0 };
		float3 Scale = { 1, 1, 1 };
	};

	//
	// Physics 
	//

	COMPONENT CRigidbody final
	{
		ERigidbodyType BodyType = ERigidbodyType::Static;
		uint8 Layer = 0; // 0: NonMoving, 1: Moving

		float Mass = 1.0f;
		float LinearDamping = 0.0f;
		float AngularDamping = 0.0f;
		bool bAllowSleeping = true;
		bool bEnableGravity = true;
		bool bStartActive = true;

		// Runtime (owned by PhysicsSystem)
		uint32 BodyHandle = 0; // PhysicsBodyHandle::Value
	};

	COMPONENT CBoxCollider final
	{
		Box Box;
		bool bIsSensor = false;

		uint64 ShapeHandle = 0;
	};

	COMPONENT CSphereCollider final
	{
		float Radius = 0.5f;
		float3 Center = { 0, 0, 0 };
		bool bIsSensor = false;

		uint64 ShapeHandle = 0;
	};

	COMPONENT CHeightFieldCollider final
	{
		uint32 Width = 0;
		uint32 Height = 0;
		float CellSizeX = 1.0f;
		float CellSizeZ = 1.0f;
		float HeightScale = 1.0f;
		float HeightOffset = 0.0f;
		std::vector<float> Heights = {};

		bool bIsSensor = false;

		uint64 ShapeHandle = 0;
	};

	//
	// Render
	//
	COMPONENT CMeshRenderer final
	{
		AssetRef<StaticMesh> MeshRef = {};
		Handle<RenderScene::SceneObject> RenderObjectHandle = {};

		bool bCastShadow = true;
	};
} // namespace shz
