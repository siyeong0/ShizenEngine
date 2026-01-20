// ============================================================================
// Engine/AssetRuntime/Public/AssimpImporter.h
// ============================================================================

#pragma once
#include <string>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/AssetData/Public/StaticMeshAsset.h"

namespace shz
{
	class AssetManager;
	class MaterialAsset;

	struct AssimpImportOptions final
	{
		// Geometry processing
		bool Triangulate = true;
		bool JoinIdenticalVertices = true;
		bool GenNormals = true;
		bool GenSmoothNormals = true;
		bool GenTangents = false;       // Vertex has no tangent yet -> default false
		bool CalcTangentSpace = false;  // recommended true when using tangents

		// UV / Winding / Handedness
		bool FlipUVs = false;            // enable only when needed
		bool ConvertToLeftHanded = true; // D3D-style LH is often convenient

		// Scaling
		float32 UniformScale = 1.0f;

		// Mesh merging policy
		bool MergeMeshes = true;

		// Material import policy
		bool ImportMaterials = true;

		// If AssetManager provided:
		// - Register texture assets from resolved paths
		// - Save bindings into MaterialAsset
		bool RegisterTextureAssets = true;
	};

	class AssimpImporter final
	{
	public:
		static bool LoadStaticMeshAsset(
			const std::string& filePath,
			StaticMeshAsset* pOutMesh,
			const AssimpImportOptions& options = {},
			std::string* outError = nullptr,
			AssetManager* pAssetManager = nullptr);
	};
} // namespace shz
