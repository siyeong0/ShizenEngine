#pragma once
#include <string>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"

namespace shz
{
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
		bool FlipUVs = false;           // enable only when needed
		bool ConvertToLeftHanded = true; // D3D-style LH is often convenient

		// Scaling
		float32 UniformScale = 1.0f;

		// Mesh merging policy
		bool MergeMeshes = true;
	};

	class AssimpImporter final
	{
	public:
		static bool LoadStaticMeshAsset(
			const std::string& filePath,
			StaticMeshAsset* pOutMesh,
			const AssimpImportOptions& options = {},
			std::string* outError = nullptr);
	};
} // namespace shz
