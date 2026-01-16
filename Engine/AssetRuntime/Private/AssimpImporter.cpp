#include "pch.h"
#include "AssimpImporter.h"

#include <vector>
#include <string>
#include <utility>
#include <filesystem>
#include <system_error>
#include <fstream>

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace shz
{
	// ------------------------------------------------------------
	// Small helpers
	// ------------------------------------------------------------

	static inline std::string makeError(const char* prefix, const char* details)
	{
		std::string s;
		s += (prefix != nullptr) ? prefix : "Error";
		s += ": ";
		s += (details != nullptr) ? details : "(null)";
		return s;
	}

	static std::string getDirectoryOfPath(const std::string& path)
	{
		if (path.empty())
		{
			return {};
		}

		const size_t pos = path.find_last_of("/\\");
		if (pos == std::string::npos)
		{
			return {};
		}

		return path.substr(0, pos + 1);
	}

	static std::string joinPath(const std::string& dir, const std::string& rel)
	{
		if (dir.empty())
		{
			return rel;
		}

		if (rel.empty())
		{
			return dir;
		}

		// Absolute paths (Windows drive, UNC, unix-like root)
		if ((rel.size() >= 2 && rel[1] == ':') ||
			(rel.size() >= 2 && rel[0] == '\\' && rel[1] == '\\') ||
			(rel[0] == '/' || rel[0] == '\\'))
		{
			return rel;
		}

		return dir + rel;
	}

	static inline uint32 makeAssimpFlags(const AssimpImportOptions& opt)
	{
		uint32 flags = 0;

		if (opt.Triangulate)
		{
			flags |= aiProcess_Triangulate;
		}

		if (opt.JoinIdenticalVertices)
		{
			flags |= aiProcess_JoinIdenticalVertices;
		}

		// Normal generation
		if (opt.GenNormals)
		{
			if (opt.GenSmoothNormals)
			{
				flags |= aiProcess_GenSmoothNormals;
			}
			else
			{
				flags |= aiProcess_GenNormals;
			}
		}

		// Tangent space (optional)
		if (opt.GenTangents || opt.CalcTangentSpace)
		{
			flags |= aiProcess_CalcTangentSpace;
		}

		// Helpful cleanup / cache locality
		flags |= aiProcess_ImproveCacheLocality;
		flags |= aiProcess_RemoveRedundantMaterials;
		flags |= aiProcess_SortByPType;

		if (opt.FlipUVs)
		{
			flags |= aiProcess_FlipUVs;
		}

		// D3D-style left-handed conversion (positions + winding)
		if (opt.ConvertToLeftHanded)
		{
			flags |= aiProcess_MakeLeftHanded;
		}

		return flags;
	}

	// Assimp matrix -> math helpers (bake node transforms)

	static inline float3 transformPoint(const aiMatrix4x4& m, const float3& p) noexcept
	{
		const float x = m.a1 * p.x + m.a2 * p.y + m.a3 * p.z + m.a4;
		const float y = m.b1 * p.x + m.b2 * p.y + m.b3 * p.z + m.b4;
		const float z = m.c1 * p.x + m.c2 * p.y + m.c3 * p.z + m.c4;
		return float3(x, y, z);
	}

	static inline aiMatrix3x3 makeNormalMatrix(const aiMatrix4x4& m) noexcept
	{
		aiMatrix3x3 m3(m);
		m3.Inverse();
		m3.Transpose();
		return m3;
	}

	static inline float3 transformNormal(const aiMatrix3x3& nrm, const float3& n) noexcept
	{
		const float x = nrm.a1 * n.x + nrm.a2 * n.y + nrm.a3 * n.z;
		const float y = nrm.b1 * n.x + nrm.b2 * n.y + nrm.b3 * n.z;
		const float z = nrm.c1 * n.x + nrm.c2 * n.y + nrm.c3 * n.z;
		return Vector3::Normalize(float3(x, y, z));
	}

	// Filesystem helpers (portable)

	static bool writeBytesToFile(
		const std::string& path,
		const void* pData,
		size_t sizeBytes,
		std::string* outError)
	{
		if (pData == nullptr || sizeBytes == 0)
		{
			if (outError)
			{
				*outError = "WriteBytesToFile: empty data.";
			}
			return false;
		}

		std::ofstream ofs(path, std::ios::binary);
		if (!ofs)
		{
			if (outError)
			{
				*outError = "WriteBytesToFile: failed to open output file: " + path;
			}
			return false;
		}

		ofs.write(reinterpret_cast<const char*>(pData), static_cast<std::streamsize>(sizeBytes));
		if (!ofs.good())
		{
			if (outError)
			{
				*outError = "WriteBytesToFile: write failed: " + path;
			}
			return false;
		}

		return true;
	}

	static bool tryDumpEmbeddedTextureToFile(
		const aiScene* scene,
		const std::string& sceneFilePath,
		uint32 embeddedIndex,
		std::string& outPath,
		std::string* outError)
	{
		outPath.clear();

		if (scene == nullptr)
		{
			if (outError)
			{
				*outError = "TryDumpEmbeddedTextureToFile: scene is null.";
			}
			return false;
		}

		// Assimp provides a helper: scene->GetEmbeddedTexture("*0")
		const std::string key = "*" + std::to_string(embeddedIndex);
		const aiTexture* tex = scene->GetEmbeddedTexture(key.c_str());
		if (tex == nullptr)
		{
			if (embeddedIndex < scene->mNumTextures)
			{
				tex = scene->mTextures[embeddedIndex];
			}
		}

		if (tex == nullptr)
		{
			if (outError)
			{
				*outError = "TryDumpEmbeddedTextureToFile: embedded texture not found: " + key;
			}
			return false;
		}

		const std::string sceneDir = getDirectoryOfPath(sceneFilePath);
		const std::string dumpDir = sceneDir + "_embedded_textures/";

		std::error_code ec;
		if (!std::filesystem::exists(dumpDir, ec)) // Ensure directory
		{

			if (!std::filesystem::create_directories(dumpDir, ec))
			{
				if (outError)
				{
					*outError = "EnsureDirectory: create_directories failed: " + ec.message();
				}
				return false;
			}
		}

		const std::string ext = ".bin";
		const char* hint = tex->achFormatHint;
		if (hint != nullptr && hint[0] != '\0')
		{
			std::string ext = ".";
			ext += hint;
		}

		const std::string outFile = dumpDir + "tex_" + std::to_string(embeddedIndex) + ext;

		// Compressed (common for glTF): mHeight == 0, mWidth == data size in bytes
		if (tex->mHeight == 0)
		{
			const size_t sizeBytes = static_cast<size_t>(tex->mWidth);
			const void* pBytes = tex->pcData;

			if (!writeBytesToFile(outFile, pBytes, sizeBytes, outError))
			{
				return false;
			}

			outPath = outFile;
			return true;
		}

		// Uncompressed: aiTexel[width*height]
		{
			const uint32 w = tex->mWidth;
			const uint32 h = tex->mHeight;

			const size_t texelCount = static_cast<size_t>(w) * static_cast<size_t>(h);
			const size_t sizeBytes = texelCount * sizeof(aiTexel);

			const std::string rawFile = dumpDir + "tex_" + std::to_string(embeddedIndex) + ".rgba8";

			if (!writeBytesToFile(rawFile, tex->pcData, sizeBytes, outError))
			{
				return false;
			}

			outPath = rawFile;
			return true;
		}
	}

	// Material import

	static bool tryGetTexturePath(
		const aiScene* scene,
		const aiMaterial* mat,
		aiTextureType type,
		const std::string& sceneFilePath,
		std::string& outPath,
		std::string* outError)
	{
		outPath.clear();

		if (mat == nullptr)
		{
			return false;
		}

		if (mat->GetTextureCount(type) == 0)
		{
			return false;
		}

		aiString path;
		if (mat->GetTexture(type, 0, &path) != AI_SUCCESS)
		{
			return false;
		}

		const char* cstr = path.C_Str();
		if (cstr == nullptr || cstr[0] == '\0')
		{
			return false;
		}

		// Embedded texture "*0", "*1", ...
		if (cstr != nullptr && cstr[0] == '*')
		{
			bool isValid = true;
			uint32 embeddedIndex = 0;

			for (uint32 i = 1; cstr[i] != '\0'; ++i)
			{
				const char ch = cstr[i];
				if (ch < '0' || ch > '9')
				{
					isValid = false;
					break;
				}

				embeddedIndex = embeddedIndex * 10u + static_cast<uint32>(ch - '0');
			}

			if (isValid)
			{
				std::string dumped;
				if (tryDumpEmbeddedTextureToFile(scene, sceneFilePath, embeddedIndex, dumped, outError))
				{
					outPath = dumped;
					return true;
				}

				// Dump failed -> treat as missing texture (fallback)
				return false;
			}

			// "*" but not a valid numeric index ¡æ treat as missing texture
			return false;
		}


		// Assimp commonly returns relative texture paths (especially for glTF).
		const std::string rel = cstr;
		const std::string dir = getDirectoryOfPath(sceneFilePath);
		outPath = joinPath(dir, rel);
		return !outPath.empty();
	}

	static void importOneMaterial(
		const aiScene* scene,
		const aiMaterial* mat,
		uint32 materialIndex,
		const std::string& sceneFilePath,
		MaterialAsset& outMat,
		std::string* outError)
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
			{
				if (mat != nullptr && mat->Get(AI_MATKEY_COLOR_DIFFUSE, c) == AI_SUCCESS)
				{
					outMat.GetParams().BaseColor = float4(c.r, c.g, c.b, c.a);
				}
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
			{
				outMat.GetParams().Metallic = metallic;
			}
#endif

#if defined(AI_MATKEY_ROUGHNESS_FACTOR)
			float roughness = outMat.GetParams().Roughness;
			if (mat != nullptr && mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
			{
				outMat.GetParams().Roughness = roughness;
			}
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
				{
					outMat.GetOptions().BlendMode = MATERIAL_BLEND_TRANSLUCENT;
				}
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
			if (tryGetTexturePath(scene, mat, aiTextureType_BASE_COLOR, sceneFilePath, path, outError) ||
				tryGetTexturePath(scene, mat, aiTextureType_DIFFUSE, sceneFilePath, path, outError))
			{
				outMat.SetTexture(MATERIAL_TEX_ALBEDO, path, true);
			}

			// Normal
			path.clear();
			if (tryGetTexturePath(scene, mat, aiTextureType_NORMALS, sceneFilePath, path, outError) ||
				tryGetTexturePath(scene, mat, aiTextureType_NORMAL_CAMERA, sceneFilePath, path, outError))
			{
				outMat.SetTexture(MATERIAL_TEX_NORMAL, path, false);
			}

			// ORM (Occlusion/Roughness/Metallic)
			path.clear();
			if (tryGetTexturePath(scene, mat, aiTextureType_METALNESS, sceneFilePath, path, outError) ||
				tryGetTexturePath(scene, mat, aiTextureType_DIFFUSE_ROUGHNESS, sceneFilePath, path, outError) ||
				tryGetTexturePath(scene, mat, aiTextureType_AMBIENT_OCCLUSION, sceneFilePath, path, outError) ||
				tryGetTexturePath(scene, mat, aiTextureType_UNKNOWN, sceneFilePath, path, outError))
			{
				outMat.SetTexture(MATERIAL_TEX_ORM, path, false);
			}

			// Emissive
			path.clear();
			if (tryGetTexturePath(scene, mat, aiTextureType_EMISSIVE, sceneFilePath, path, outError))
			{
				outMat.SetTexture(MATERIAL_TEX_EMISSIVE, path, true);
			}

			// Height / Displacement
			path.clear();
			if (tryGetTexturePath(scene, mat, aiTextureType_HEIGHT, sceneFilePath, path, outError))
			{
				outMat.SetTexture(MATERIAL_TEX_HEIGHT, path, false);
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
			{
				*outError = "AssimpImporter: pOutMesh is null.";
			}
			return false;
		}

		pOutMesh->Clear();
		pOutMesh->SetSourcePath(filePath);

		Assimp::Importer importer;
		const uint32 flags = makeAssimpFlags(options);

		const aiScene* scene = importer.ReadFile(filePath.c_str(), flags);
		if (scene == nullptr)
		{
			if (outError)
			{
				*outError = makeError("Assimp ReadFile failed", importer.GetErrorString());
			}
			return false;
		}

		if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr)
		{
			if (outError)
			{
				*outError = makeError("Assimp scene incomplete", importer.GetErrorString());
			}
			return false;
		}

		if (scene->mNumMeshes == 0)
		{
			if (outError)
			{
				*outError = "Assimp: scene has no meshes.";
			}
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
				importOneMaterial(scene, mat, i, filePath, materials[i], outError);
			}

			pOutMesh->SetMaterialSlots(std::move(materials));
		}

		// ------------------------------------------------------------
		// Decide index type (estimate)
		// NOTE:
		// With node traversal, the same aiMesh can be referenced multiple times.
		// For now we keep it simple: sum unique mesh vertex counts as a lower bound.
		// If your glTF heavily instances, this may underestimate, but still safe for U32.
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

		const VALUE_TYPE indexType = totalVertexCount <= 65535u ? VT_UINT16 : VT_UINT32;

		if (indexType == VT_UINT32)
		{
			pOutMesh->SetIndicesU32({});
		}
		else
		{
			pOutMesh->SetIndicesU16({});
		}

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
		// Import meshes by traversing nodes (BAKE node transforms)
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

		auto ImportMeshAsSection = [&](const aiMesh* mesh, const aiMatrix4x4& global) -> bool
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

			const bool hasNormals = mesh->HasNormals();
			const bool hasTangents = (mesh->mTangents != nullptr) && (mesh->mBitangents != nullptr);
			const bool hasUV0 = mesh->HasTextureCoords(0);

			const aiMatrix3x3 normalM = makeNormalMatrix(global);

			for (uint32 v = 0; v < vertexCount; ++v)
			{
				const aiVector3D& pA = mesh->mVertices[v];
				float3 p = float3(pA.x, pA.y, pA.z) * options.UniformScale;
				p = transformPoint(global, p);
				positions.push_back(p);

				float3 n = hasNormals ? float3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z) : float3(0.0f, 1.0f, 0.0f);
				normals.push_back(transformNormal(normalM, n));

				float3 t = hasTangents ? float3(mesh->mTangents[v].z, mesh->mTangents[v].y, mesh->mTangents[v].z) : float3(1.0f, 0.0f, 0.0f);
				tangents.push_back(transformNormal(normalM, t));

				if (hasUV0)
				{
					texCoords.push_back(float2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y));
				}
				else
				{
					texCoords.push_back(float2(0.0f, 0.0f));
				}
			}

			StaticMeshAsset::Section sec{};
			sec.BaseVertex = baseVertex;
			sec.MaterialSlot = mesh->mMaterialIndex;

			const uint32 firstIndex = pOutMesh->GetIndexCount();
			uint32 indexCount = 0;

			for (uint32 f = 0; f < mesh->mNumFaces; ++f)
			{
				const aiFace& face = mesh->mFaces[f];
				if (face.mNumIndices != 3)
				{
					continue;
				}

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

		auto TraverseNode = [&](auto&& self, const aiNode* node, const aiMatrix4x4& parent) -> bool
		{
			if (node == nullptr)
			{
				return true;
			}

			aiMatrix4x4 global = parent * node->mTransformation;

			for (uint32 i = 0; i < node->mNumMeshes; ++i)
			{
				const uint32 meshIndex = node->mMeshes[i];
				if (meshIndex >= scene->mNumMeshes)
				{
					continue;
				}

				if (!options.MergeMeshes)
				{
					const aiMesh* mesh0 = scene->mMeshes[meshIndex];
					return ImportMeshAsSection(mesh0, global);
				}

				const aiMesh* mesh = scene->mMeshes[meshIndex];
				if (!ImportMeshAsSection(mesh, global))
				{
					return false;
				}
			}

			for (uint32 c = 0; c < node->mNumChildren; ++c)
			{
				if (!self(self, node->mChildren[c], global))
				{
					return false;
				}

				if (!options.MergeMeshes && !sections.empty())
				{
					return true;
				}
			}

			return true;
		};

		{
			const aiMatrix4x4 identity;
			if (!TraverseNode(TraverseNode, scene->mRootNode, identity))
			{
				if (outError)
				{
					*outError = "Assimp: failed to import meshes while traversing nodes (bake transforms).";
				}
				return false;
			}
		}

		if (positions.empty() || sections.empty())
		{
			if (outError)
			{
				*outError = "Assimp: node traversal produced empty mesh.";
			}
			return false;
		}

		// ------------------------------------------------------------
		// Commit to asset (SoA)
		// ------------------------------------------------------------
		pOutMesh->SetPositions(std::move(positions));
		pOutMesh->SetNormals(std::move(normals));
		pOutMesh->SetTangents(std::move(tangents));
		pOutMesh->SetTexCoords(std::move(texCoords));
		pOutMesh->SetSections(std::move(sections));

		pOutMesh->RecomputeBounds();

		if (!pOutMesh->IsValid())
		{
			if (outError)
			{
				*outError = "Assimp: imported mesh is invalid (empty vertices/indices or inconsistent attributes/sections/material slots).";
			}
			return false;
		}

		return true;
	}

} // namespace shz
