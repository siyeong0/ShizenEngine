#include "pch.h"
#include "AssimpImporter.h"

#include <vector>
#include <string>
#include <utility>

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace shz
{
	// ------------------------------------------------------------
	// Small helpers
	// ------------------------------------------------------------

	static inline float3 ToFloat3(const aiVector3D& v) noexcept
	{
		return float3(v.x, v.y, v.z);
	}

	static inline float2 ToFloat2(const aiVector3D& v) noexcept
	{
		return float2(v.x, v.y);
	}

	static inline std::string MakeError(const char* prefix, const char* details)
	{
		std::string s;
		s += (prefix != nullptr) ? prefix : "Error";
		s += ": ";
		s += (details != nullptr) ? details : "(null)";
		return s;
	}

	static std::string GetDirectoryOfPath(const std::string& path)
	{
		if (path.empty())
			return {};

		const size_t pos = path.find_last_of("/\\");
		if (pos == std::string::npos)
			return {};

		return path.substr(0, pos + 1);
	}

	static std::string JoinPath(const std::string& dir, const std::string& rel)
	{
		if (dir.empty())
			return rel;

		if (rel.empty())
			return dir;

		// Absolute paths (Windows drive, UNC, unix-like root)
		if ((rel.size() >= 2 && rel[1] == ':') ||
			(rel.size() >= 2 && rel[0] == '\\' && rel[1] == '\\') ||
			(rel[0] == '/' || rel[0] == '\\'))
		{
			return rel;
		}

		return dir + rel;
	}

	static inline uint32 MakeAssimpFlags(const AssimpImportOptions& opt)
	{
		uint32 flags = 0;

		if (opt.Triangulate)
			flags |= aiProcess_Triangulate;

		if (opt.JoinIdenticalVertices)
			flags |= aiProcess_JoinIdenticalVertices;

		// Normal generation
		if (opt.GenNormals)
		{
			if (opt.GenSmoothNormals)
				flags |= aiProcess_GenSmoothNormals;
			else
				flags |= aiProcess_GenNormals;
		}

		// Tangent space (optional)
		if (opt.GenTangents || opt.CalcTangentSpace)
			flags |= aiProcess_CalcTangentSpace;

		// Helpful cleanup / cache locality
		flags |= aiProcess_ImproveCacheLocality;
		flags |= aiProcess_RemoveRedundantMaterials;
		flags |= aiProcess_SortByPType;

		if (opt.FlipUVs)
			flags |= aiProcess_FlipUVs;

		// D3D-style left-handed conversion (positions + winding)
		if (opt.ConvertToLeftHanded)
		{
			flags |= aiProcess_MakeLeftHanded;
			flags |= aiProcess_FlipWindingOrder;
		}

		return flags;
	}

	static inline bool CanUseU16Indices(uint32 vertexCount) noexcept
	{
		return vertexCount <= 65535u;
	}

	// ------------------------------------------------------------
	// Material import
	// ------------------------------------------------------------

	static bool TryGetTexturePath(
		const aiMaterial* mat,
		aiTextureType type,
		const std::string& sceneFilePath,
		std::string& outPath)
	{
		outPath.clear();

		if (mat == nullptr)
			return false;

		if (mat->GetTextureCount(type) == 0)
			return false;

		aiString path;
		if (mat->GetTexture(type, 0, &path) != AI_SUCCESS)
			return false;

		const char* cstr = path.C_Str();
		if (cstr == nullptr || cstr[0] == '\0')
			return false;

		// Assimp commonly returns relative texture paths (especially for glTF).
		const std::string rel = cstr;
		const std::string dir = GetDirectoryOfPath(sceneFilePath);
		outPath = JoinPath(dir, rel);
		return !outPath.empty();
	}

	static void ImportOneMaterial(
		const aiMaterial* mat,
		uint32 materialIndex,
		const std::string& sceneFilePath,
		MaterialAsset& outMat)
	{
		(void)materialIndex;

		outMat.Clear();
		outMat.SetSourcePath(sceneFilePath);

		// Name
		{
			aiString n;
			if (mat != nullptr && mat->Get(AI_MATKEY_NAME, n) == AI_SUCCESS)
			{
				outMat.SetName(n.C_Str());
			}
		}

		// Default template key (Renderer can map this later)
		outMat.SetShaderKey("DefaultLit");

		// BaseColor (Diffuse / BaseColor)
		{
			aiColor4D c(1, 1, 1, 1);

#if defined(AI_MATKEY_BASE_COLOR)
			if (mat != nullptr && mat->Get(AI_MATKEY_BASE_COLOR, c) == AI_SUCCESS)
			{
				outMat.GetParams().BaseColor = float4(c.r, c.g, c.b, c.a);
			}
			else
#endif
				if (mat != nullptr && mat->Get(AI_MATKEY_COLOR_DIFFUSE, c) == AI_SUCCESS)
				{
					outMat.GetParams().BaseColor = float4(c.r, c.g, c.b, c.a);
				}
		}

		// Emissive color
		{
			aiColor3D e(0, 0, 0);
			if (mat != nullptr && mat->Get(AI_MATKEY_COLOR_EMISSIVE, e) == AI_SUCCESS)
			{
				outMat.GetParams().EmissiveColor = float3(e.r, e.g, e.b);
			}
		}

		// Metallic / Roughness (best effort)
		{
#if defined(AI_MATKEY_METALLIC_FACTOR)
			float metallic = outMat.GetParams().Metallic;
			if (mat != nullptr && mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
				outMat.GetParams().Metallic = metallic;
#endif

#if defined(AI_MATKEY_ROUGHNESS_FACTOR)
			float roughness = outMat.GetParams().Roughness;
			if (mat != nullptr && mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
				outMat.GetParams().Roughness = roughness;
#endif
		}

		// Opacity / alpha cutoff / blend mode (best effort)
		{
			float opacity = 1.0f;
			if (mat != nullptr && mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
			{
				float4 bc = outMat.GetParams().BaseColor;
				bc.w = opacity;
				outMat.GetParams().BaseColor = bc;

				// Heuristic: if opacity is not 1, treat as translucent
				if (opacity < 0.999f)
					outMat.GetOptions().BlendMode = MaterialAsset::BLEND_TRANSLUCENT;
			}

#if defined(AI_MATKEY_GLTF_ALPHACUTOFF)
			float cutoff = outMat.GetParams().AlphaCutoff;
			if (mat != nullptr && mat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff) == AI_SUCCESS)
			{
				outMat.GetParams().AlphaCutoff = cutoff;
			}
#endif
		}

		// Two-sided (best effort)
		{
#if defined(AI_MATKEY_TWOSIDED)
			int twoSided = 0;
			if (mat != nullptr && mat->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS)
			{
				outMat.GetOptions().TwoSided = (twoSided != 0);
			}
#endif
		}

		// Textures (best effort with fallbacks)
		{
			std::string path;

			// Albedo / BaseColor
			path.clear();
			if (TryGetTexturePath(mat, aiTextureType_BASE_COLOR, sceneFilePath, path) ||
				TryGetTexturePath(mat, aiTextureType_DIFFUSE, sceneFilePath, path))
			{
				outMat.SetTexture(MaterialAsset::TEX_ALBEDO, path, true);
			}

			// Normal
			path.clear();
			if (TryGetTexturePath(mat, aiTextureType_NORMALS, sceneFilePath, path) ||
				TryGetTexturePath(mat, aiTextureType_HEIGHT, sceneFilePath, path)) // exporter misuse fallback
			{
				outMat.SetTexture(MaterialAsset::TEX_NORMAL, path, false);
			}

			// ORM (Occlusion/Roughness/Metallic)
			// NOTE:
			// Different exporters store these textures differently. We keep only the path here.
			path.clear();
			if (TryGetTexturePath(mat, aiTextureType_METALNESS, sceneFilePath, path) ||
				TryGetTexturePath(mat, aiTextureType_DIFFUSE_ROUGHNESS, sceneFilePath, path) ||
				TryGetTexturePath(mat, aiTextureType_AMBIENT_OCCLUSION, sceneFilePath, path) ||
				TryGetTexturePath(mat, aiTextureType_UNKNOWN, sceneFilePath, path)) // conservative fallback
			{
				outMat.SetTexture(MaterialAsset::TEX_ORM, path, false);
			}

			// Emissive
			path.clear();
			if (TryGetTexturePath(mat, aiTextureType_EMISSIVE, sceneFilePath, path))
			{
				outMat.SetTexture(MaterialAsset::TEX_EMISSIVE, path, true);
			}
		}
	}

	// ------------------------------------------------------------
	// AssimpImporter
	// ------------------------------------------------------------
	bool AssimpImporter::LoadStaticMeshAsset(
		const std::string& filePath,
		StaticMeshAsset* pOutMesh,
		const AssimpImportOptions& options,
		std::string* outError)
	{
		if (pOutMesh == nullptr)
		{
			if (outError)
				*outError = "AssimpImporter: pOutMesh is null.";
			return false;
		}

		pOutMesh->Clear();
		pOutMesh->SetSourcePath(filePath);

		Assimp::Importer importer;
		const uint32 flags = MakeAssimpFlags(options);

		const aiScene* scene = importer.ReadFile(filePath.c_str(), flags);
		if (scene == nullptr)
		{
			if (outError) *outError = MakeError("Assimp ReadFile failed", importer.GetErrorString());
			return false;
		}

		if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr)
		{
			if (outError) *outError = MakeError("Assimp scene incomplete", importer.GetErrorString());
			return false;
		}

		if (scene->mNumMeshes == 0)
		{
			if (outError) *outError = "Assimp: scene has no meshes.";
			return false;
		}

		// ------------------------------------------------------------
		// Import materials (slots)
		// ------------------------------------------------------------

		{
			std::vector<MaterialAsset> materials;
			materials.resize(scene->mNumMaterials);

			for (uint32 i = 0; i < scene->mNumMaterials; ++i)
			{
				const aiMaterial* mat = scene->mMaterials[i];
				ImportOneMaterial(mat, i, filePath, materials[i]);
			}

			pOutMesh->SetMaterialSlots(std::move(materials));
		}

		// ------------------------------------------------------------
		// Decide index type
		// ------------------------------------------------------------

		uint32 totalVertexCount = 0;
		if (options.MergeMeshes)
		{
			for (uint32 m = 0; m < scene->mNumMeshes; ++m)
			{
				const aiMesh* mesh = scene->mMeshes[m];
				if (mesh != nullptr)
				{
					totalVertexCount += mesh->mNumVertices;
				}
			}
		}
		else
		{
			totalVertexCount = scene->mMeshes[0]->mNumVertices;
		}

		pOutMesh->ReserveVertices(totalVertexCount);

		const VALUE_TYPE indexType = CanUseU16Indices(totalVertexCount) ? VT_UINT16 : VT_UINT32;

		// Initialize index storage (keeps your existing pattern)
		if (indexType == VT_UINT32)
		{
			pOutMesh->SetIndicesU32({});
		}
		else
		{
			pOutMesh->SetIndicesU16({});
		}

		// Indices accessors (non-const)
		auto& idx32 = pOutMesh->GetIndicesU32();
		auto& idx16 = pOutMesh->GetIndicesU16();

		auto pushIndex = [&](uint32 idx)
		{
			if (indexType == VT_UINT32)
			{
				idx32.push_back(idx);
			}
			else
			{
				idx16.push_back(static_cast<uint16>(idx));
			}
		};

		// ------------------------------------------------------------
		// Import meshes (SoA)
		// ------------------------------------------------------------

		std::vector<float3> positions;
		std::vector<float3> normals;
		std::vector<float3> tangents;
		std::vector<float2> texCoords;

		positions.reserve(totalVertexCount);
		normals.reserve(totalVertexCount);
		tangents.reserve(totalVertexCount);
		texCoords.reserve(totalVertexCount);

		std::vector<StaticMeshAsset::Section> sections;
		sections.reserve(options.MergeMeshes ? scene->mNumMeshes : 1);

		auto ImportMeshAsSection = [&](const aiMesh* mesh) -> bool
		{
			if (mesh == nullptr)
			{
				return true;
			}

			if (!mesh->HasPositions())
			{
				return false;
			}

			const uint32 baseVertex = static_cast<uint32>(positions.size());
			const uint32 vertexCount = mesh->mNumVertices;

			// Assimp tangents require aiProcess_CalcTangentSpace in flags.
			const bool hasNormals = mesh->HasNormals();
			const bool hasTangents = (mesh->mTangents != nullptr) && (mesh->mBitangents != nullptr);
			const bool hasUV0 = mesh->HasTextureCoords(0);

			// Vertices (SoA push in lockstep to keep sizes identical)
			for (uint32 v = 0; v < vertexCount; ++v)
			{
				const aiVector3D& p = mesh->mVertices[v];
				positions.push_back(float3(p.x, p.y, p.z) * options.UniformScale);

				if (hasNormals)
					normals.push_back(ToFloat3(mesh->mNormals[v]));
				else
					normals.push_back(float3(0.0f, 1.0f, 0.0f));

				if (hasTangents)
					tangents.push_back(ToFloat3(mesh->mTangents[v]));
				else
					tangents.push_back(float3(0.0f, 0.0f, 1.0f));

				if (hasUV0)
					texCoords.push_back(ToFloat2(mesh->mTextureCoords[0][v]));
				else
					texCoords.push_back(float2(0.0f, 0.0f));
			}

			// Section
			StaticMeshAsset::Section sec{};
			sec.BaseVertex = baseVertex;
			sec.MaterialSlot = mesh->mMaterialIndex;

			const uint32 firstIndex = pOutMesh->GetIndexCount();
			uint32 indexCount = 0;

			// Faces (triangulated by flags)
			for (uint32 f = 0; f < mesh->mNumFaces; ++f)
			{
				const aiFace& face = mesh->mFaces[f];
				if (face.mNumIndices != 3)
					continue;

				const uint32 i0 = baseVertex + face.mIndices[0];
				const uint32 i1 = baseVertex + face.mIndices[1];
				const uint32 i2 = baseVertex + face.mIndices[2];

				pushIndex(i0);
				pushIndex(i1);
				pushIndex(i2);
				indexCount += 3;
			}

			sec.FirstIndex = firstIndex;
			sec.IndexCount = indexCount;

			sections.push_back(sec);
			return true;
		};

		if (options.MergeMeshes)
		{
			for (uint32 m = 0; m < scene->mNumMeshes; ++m)
			{
				if (!ImportMeshAsSection(scene->mMeshes[m]))
				{
					if (outError)
						*outError = "Assimp: failed to import one of the meshes.";
					return false;
				}
			}
		}
		else
		{
			if (!ImportMeshAsSection(scene->mMeshes[0]))
			{
				if (outError)
					*outError = "Assimp: failed to import the first mesh.";
				return false;
			}
		}

		// ------------------------------------------------------------
		// Commit to asset (SoA)
		// ------------------------------------------------------------

		pOutMesh->SetPositions(std::move(positions));
		pOutMesh->SetNormals(std::move(normals));
		pOutMesh->SetTangents(std::move(tangents));
		pOutMesh->SetTexCoords(std::move(texCoords));
		pOutMesh->SetSections(std::move(sections));

		// Bounds (mesh + sections)
		pOutMesh->RecomputeBounds();

		if (!pOutMesh->IsValid())
		{
			if (outError)
				*outError = "Assimp: imported mesh is invalid (empty vertices/indices or inconsistent attributes/sections/material slots).";
			return false;
		}

		return true;
	}

} // namespace shz
