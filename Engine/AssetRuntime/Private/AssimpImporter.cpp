// ============================================================================
// Engine/AssetRuntime/Private/AssimpImporter.cpp
// ============================================================================

#include "pch.h"
#include "Engine/AssetRuntime/Public/AssimpImporter.h"

#include <vector>
#include <string>
#include <utility>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <cctype>

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// Engine
#include "Engine/AssetRuntime/Public/AssetManager.h"
#include "Engine/AssetRuntime/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"

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

	static inline bool isSpaceChar(unsigned char c) noexcept
	{
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}

	// Trim spaces + optional wrapping quotes, normalize slashes to '/'
	static std::string sanitizePathString(std::string s)
	{
		while (!s.empty() && isSpaceChar((unsigned char)s.front()))
			s.erase(s.begin());
		while (!s.empty() && isSpaceChar((unsigned char)s.back()))
			s.pop_back();

		if (s.size() >= 2)
		{
			const char a = s.front();
			const char b = s.back();
			if ((a == '"' && b == '"') || (a == '\'' && b == '\''))
			{
				s = s.substr(1, s.size() - 2);

				while (!s.empty() && isSpaceChar((unsigned char)s.front()))
					s.erase(s.begin());
				while (!s.empty() && isSpaceChar((unsigned char)s.back()))
					s.pop_back();
			}
		}

		for (char& c : s)
		{
			if (c == '\\') c = '/';
		}

		return s;
	}

	static std::string getDirectoryOfPath(const std::string& path)
	{
		if (path.empty())
			return {};

		const size_t pos = path.find_last_of("/\\");
		if (pos == std::string::npos)
			return {};

		return path.substr(0, pos + 1);
	}

	// Fix patterns like "c:/c:/dev/..." or "C:\C:\dev\..."
	// This can happen when a path was already absolute but got "joined" again.
	static std::string fixDuplicateDrivePrefix(std::string s)
	{
		// Work on normalized slashes form.
		s = sanitizePathString(static_cast<std::string&&>(s));

		if (s.size() < 6)
			return s;

		auto IsAlpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };

		// Pattern: "<d>:/<d>:/..."
		if (IsAlpha(s[0]) && s[1] == ':' && s[2] == '/' &&
			IsAlpha(s[3]) && s[4] == ':' && s[5] == '/')
		{
			// Keep the first drive, drop the duplicated second drive.
			// "c:/c:/dev/x" -> "c:/dev/x"
			s.erase(0, 3); // drop first "c:/"
			// Now s begins with "c:/dev/x". But we wanted the first drive, not the second.
			// So instead reconstruct with first drive:
			// original: D0 = s0 (before erase) is lost now, so do it differently.
		}

		// Implement properly with original.
		{
			const std::string t = s;
			if (t.size() >= 6 && IsAlpha(t[0]) && t[1] == ':' && t[2] == '/' &&
				IsAlpha(t[3]) && t[4] == ':' && t[5] == '/')
			{
				std::string out;
				out.reserve(t.size());
				out.push_back(t[0]);
				out.push_back(':');
				out.push_back('/');
				out.append(t.substr(6)); // skip "<d>:/"
				return out;
			}
		}

		return s;
	}

	static std::string normalizeResolvedPath(const std::filesystem::path& p)
	{
		// Use lexically_normal (no filesystem access needed).
		std::filesystem::path n = p.lexically_normal();

		// Use generic string with forward slashes (matches your sanitizeFilePath behavior).
		std::string out = n.generic_string();
		out = sanitizePathString(static_cast<std::string&&>(out));
		out = fixDuplicateDrivePrefix(static_cast<std::string&&>(out));
		return out;
	}

	static std::filesystem::path pathFromUtf8(const std::string& s)
	{
		// On Windows, std::filesystem::path accepts UTF-8 in modern MSVC; assume ok.
		return std::filesystem::path(s);
	}

	static inline uint32 makeAssimpFlags(const AssimpImportOptions& opt)
	{
		uint32 flags = 0;

		if (opt.Triangulate)            flags |= aiProcess_Triangulate;
		if (opt.JoinIdenticalVertices)  flags |= aiProcess_JoinIdenticalVertices;

		// Normal generation
		if (opt.GenNormals)
		{
			if (opt.GenSmoothNormals) flags |= aiProcess_GenSmoothNormals;
			else                      flags |= aiProcess_GenNormals;
		}

		// Tangent space (optional)
		if (opt.GenTangents || opt.CalcTangentSpace)
			flags |= aiProcess_CalcTangentSpace;

		// Helpful cleanup / cache locality
		flags |= aiProcess_ImproveCacheLocality;
		flags |= aiProcess_RemoveRedundantMaterials;
		flags |= aiProcess_SortByPType;

		if (opt.FlipUVs) flags |= aiProcess_FlipUVs;

		// D3D-style left-handed conversion (positions + winding)
		if (opt.ConvertToLeftHanded) flags |= aiProcess_MakeLeftHanded;

		return flags;
	}

	// ------------------------------------------------------------
	// Assimp matrix -> math helpers (bake node transforms)
	// ------------------------------------------------------------

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

	// ------------------------------------------------------------
	// Filesystem helpers (portable)
	// ------------------------------------------------------------

	static bool writeBytesToFile(
		const std::string& path,
		const void* pData,
		size_t sizeBytes,
		std::string* outError)
	{
		if (pData == nullptr || sizeBytes == 0)
		{
			if (outError) *outError = "WriteBytesToFile: empty data.";
			return false;
		}

		std::ofstream ofs(path, std::ios::binary);
		if (!ofs)
		{
			if (outError) *outError = "WriteBytesToFile: failed to open output file: " + path;
			return false;
		}

		ofs.write(reinterpret_cast<const char*>(pData), static_cast<std::streamsize>(sizeBytes));
		if (!ofs.good())
		{
			if (outError) *outError = "WriteBytesToFile: write failed: " + path;
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
			if (outError) *outError = "TryDumpEmbeddedTextureToFile: scene is null.";
			return false;
		}

		const std::string key = "*" + std::to_string(embeddedIndex);
		const aiTexture* tex = scene->GetEmbeddedTexture(key.c_str());
		if (tex == nullptr)
		{
			if (embeddedIndex < scene->mNumTextures)
				tex = scene->mTextures[embeddedIndex];
		}

		if (tex == nullptr)
		{
			if (outError) *outError = "TryDumpEmbeddedTextureToFile: embedded texture not found: " + key;
			return false;
		}

		const std::filesystem::path sceneDir = pathFromUtf8(getDirectoryOfPath(sceneFilePath));
		const std::filesystem::path dumpDir = sceneDir / "_embedded_textures";

		std::error_code ec;
		if (!std::filesystem::exists(dumpDir, ec))
		{
			if (!std::filesystem::create_directories(dumpDir, ec))
			{
				if (outError) *outError = "EnsureDirectory: create_directories failed: " + ec.message();
				return false;
			}
		}

		std::string ext = ".bin";
		const char* hint = tex->achFormatHint;
		if (hint != nullptr && hint[0] != '\0')
		{
			ext = ".";
			ext += hint;
		}

		const std::filesystem::path outFilePath =
			dumpDir / (std::string("tex_") + std::to_string(embeddedIndex) + ext);

		const std::string outFile = normalizeResolvedPath(outFilePath);

		// Compressed: mHeight == 0, mWidth == data size in bytes
		if (tex->mHeight == 0)
		{
			const size_t sizeBytes = static_cast<size_t>(tex->mWidth);
			if (!writeBytesToFile(outFile, tex->pcData, sizeBytes, outError))
				return false;

			outPath = outFile;
			return true;
		}

		// Uncompressed: aiTexel[width*height]
		{
			const uint32 w = tex->mWidth;
			const uint32 h = tex->mHeight;

			const size_t texelCount = static_cast<size_t>(w) * static_cast<size_t>(h);
			const size_t sizeBytes = texelCount * sizeof(aiTexel);

			const std::filesystem::path rawFilePath =
				dumpDir / (std::string("tex_") + std::to_string(embeddedIndex) + ".rgba8");

			const std::string rawFile = normalizeResolvedPath(rawFilePath);

			if (!writeBytesToFile(rawFile, tex->pcData, sizeBytes, outError))
				return false;

			outPath = rawFile;
			return true;
		}
	}

	// ------------------------------------------------------------
	// Material import helpers
	// ------------------------------------------------------------

	static bool resolveTexturePath(
		const aiScene* scene,
		const aiMaterial* mat,
		aiTextureType type,
		const std::string& sceneFilePath,
		std::string& outPath,
		std::string* outError)
	{
		outPath.clear();

		if (!mat)
			return false;

		if (mat->GetTextureCount(type) == 0)
			return false;

		aiString path;
		if (mat->GetTexture(type, 0, &path) != AI_SUCCESS)
			return false;

		const char* cstr = path.C_Str();
		if (!cstr || cstr[0] == '\0')
			return false;

		std::string raw = sanitizePathString(cstr);

		// Embedded texture "*0", "*1", ...
		if (!raw.empty() && raw[0] == '*')
		{
			bool isValid = true;
			uint32 embeddedIndex = 0;

			for (uint32 i = 1; i < (uint32)raw.size(); ++i)
			{
				const char ch = raw[i];
				if (ch < '0' || ch > '9')
				{
					isValid = false;
					break;
				}
				embeddedIndex = embeddedIndex * 10u + static_cast<uint32>(ch - '0');
			}

			if (!isValid)
				return false;

			std::string dumped;
			if (tryDumpEmbeddedTextureToFile(scene, sceneFilePath, embeddedIndex, dumped, outError))
			{
				outPath = dumped;
				return true;
			}
			return false;
		}

		// Resolve relative paths against scene directory using std::filesystem.
		const std::filesystem::path sceneDir = pathFromUtf8(getDirectoryOfPath(sceneFilePath));
		const std::filesystem::path p = pathFromUtf8(raw);

		std::filesystem::path resolved;
		if (p.is_absolute() || p.has_root_name())
		{
			// Already absolute path. Just normalize.
			resolved = p;
		}
		else
		{
			// Relative -> resolve against scene directory.
			resolved = sceneDir / p;
		}

		outPath = normalizeResolvedPath(resolved);
		outPath = fixDuplicateDrivePrefix(static_cast<std::string&&>(outPath));

		return !outPath.empty();
	}

	static void importOneMaterial(
		const aiScene* scene,
		const aiMaterial* mat,
		uint32 materialIndex,
		const std::string& sceneFilePath,
		MaterialAsset& outMat,
		AssetManager* pAssetManager,
		const AssimpImportOptions& opt,
		std::string* outError)
	{
		auto PushErrorOnce = [&](const std::string& msg)
		{
			if (!outError) return;
			if (outError->empty()) *outError = msg;
		};

		outMat.Clear();
		outMat.SetSourcePath(sceneFilePath);

		// Name
		{
			aiString n;
			if (mat != nullptr && mat->Get(AI_MATKEY_NAME, n) == AI_SUCCESS)
			{
				outMat.SetName(n.C_Str());
			}
			else
			{
				outMat.SetName(std::string("Material_") + std::to_string(materialIndex));
			}
		}

		// Default template key (Renderer can map this later)
		outMat.SetTemplateKey("DefaultLit");

		// ------------------------------------------------------------
		// Values (store as overrides by shader param names)
		// ------------------------------------------------------------

		// BaseColor
		float baseColor[4] = { 1, 1, 1, 1 };
		{
			aiColor4D c(1, 1, 1, 1);

#if defined(AI_MATKEY_BASE_COLOR)
			if (mat != nullptr && mat->Get(AI_MATKEY_BASE_COLOR, c) == AI_SUCCESS)
			{
				baseColor[0] = c.r; baseColor[1] = c.g; baseColor[2] = c.b; baseColor[3] = c.a;
			}
			else
#endif
				if (mat != nullptr && mat->Get(AI_MATKEY_COLOR_DIFFUSE, c) == AI_SUCCESS)
				{
					baseColor[0] = c.r; baseColor[1] = c.g; baseColor[2] = c.b; baseColor[3] = c.a;
				}
		}
		outMat.SetFloat4("g_BaseColorFactor", baseColor);

		// Emissive
		{
			aiColor3D e(0, 0, 0);
			float emissive[3] = { 0, 0, 0 };

			if (mat != nullptr && mat->Get(AI_MATKEY_COLOR_EMISSIVE, e) == AI_SUCCESS)
			{
				emissive[0] = e.r; emissive[1] = e.g; emissive[2] = e.b;
			}

			outMat.SetFloat3("g_EmissiveFactor", emissive);
			outMat.SetFloat("g_EmissiveIntensity", 1.0f);
		}

		// Metallic / Roughness
		{
			float metallic = 0.0f;
			float roughness = 1.0f;

#if defined(AI_MATKEY_METALLIC_FACTOR)
			if (!(mat != nullptr && mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS))
				metallic = 0.0f;
#endif
#if defined(AI_MATKEY_ROUGHNESS_FACTOR)
			if (!(mat != nullptr && mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS))
				roughness = 1.0f;
#endif
			outMat.SetFloat("g_MetallicFactor", metallic);
			outMat.SetFloat("g_RoughnessFactor", roughness);
		}

		// Occlusion strength
		outMat.SetFloat("g_OcclusionStrength", 1.0f);

		// Opacity / cutoff
		{
			float opacity = 1.0f;
			if (mat != nullptr && mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
			{
				baseColor[3] = opacity;
				outMat.SetFloat4("g_BaseColorFactor", baseColor);
			}

			float alphaCutoff = 0.5f;
#if defined(AI_MATKEY_GLTF_ALPHACUTOFF)
			if (!(mat != nullptr && mat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff) == AI_SUCCESS))
				alphaCutoff = 0.5f;
#endif
			outMat.SetFloat("g_AlphaCutoff", alphaCutoff);
		}

		// Normal scale
		outMat.SetFloat("g_NormalScale", 1.0f);

		// ------------------------------------------------------------
		// Textures (path -> AssetRef via AssetManager)
		// ------------------------------------------------------------

		auto BindTex2D = [&](const char* shaderVar, const std::string& texPath)
		{
			if (!shaderVar || shaderVar[0] == '\0')
				return;

			if (texPath.empty())
				return;

			// If we don't have AssetManager or policy disabled: skip.
			if (!pAssetManager || !opt.RegisterTextureAssets)
				return;

			// Register texture asset by path and store AssetRef.
			// NOTE: RegisterAssetRefByPath is expected to be deterministic.
			const AssetRef<TextureAsset> texRef = pAssetManager->RegisterAssetRefByPath<TextureAsset>(texPath);
			if (!texRef)
			{
				PushErrorOnce(std::string("RegisterAssetRefByPath<TextureAsset> failed. Var=") + shaderVar + " Path=" + texPath);
				return;
			}

			outMat.SetTextureAssetRef(shaderVar, MATERIAL_RESOURCE_TYPE_TEXTURE2D, texRef);
		};

		{
			std::string path;

			// BaseColor
			path.clear();
			if (resolveTexturePath(scene, mat, aiTextureType_BASE_COLOR, sceneFilePath, path, outError) ||
				resolveTexturePath(scene, mat, aiTextureType_DIFFUSE, sceneFilePath, path, outError))
			{
				BindTex2D("g_BaseColorTex", path);
			}

			// Normal
			path.clear();
			if (resolveTexturePath(scene, mat, aiTextureType_NORMALS, sceneFilePath, path, outError) ||
				resolveTexturePath(scene, mat, aiTextureType_NORMAL_CAMERA, sceneFilePath, path, outError))
			{
				BindTex2D("g_NormalTex", path);
			}

			// Metallic/Roughness (packed)
			path.clear();
			if (resolveTexturePath(scene, mat, aiTextureType_METALNESS, sceneFilePath, path, outError) ||
				resolveTexturePath(scene, mat, aiTextureType_DIFFUSE_ROUGHNESS, sceneFilePath, path, outError) ||
				resolveTexturePath(scene, mat, aiTextureType_UNKNOWN, sceneFilePath, path, outError))
			{
				BindTex2D("g_MetallicRoughnessTex", path);
			}

			// AO
			path.clear();
			if (resolveTexturePath(scene, mat, aiTextureType_AMBIENT_OCCLUSION, sceneFilePath, path, outError))
			{
				BindTex2D("g_AOTex", path);
			}

			// Emissive
			path.clear();
			if (resolveTexturePath(scene, mat, aiTextureType_EMISSIVE, sceneFilePath, path, outError))
			{
				BindTex2D("g_EmissiveTex", path);
			}

			// Height
			path.clear();
			if (resolveTexturePath(scene, mat, aiTextureType_HEIGHT, sceneFilePath, path, outError))
			{
				BindTex2D("g_HeightTex", path);
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
		std::string* outError,
		AssetManager* pAssetManager)
	{
		if (pOutMesh == nullptr)
		{
			if (outError) *outError = "AssimpImporter: pOutMesh is null.";
			return false;
		}

		pOutMesh->Clear();
		pOutMesh->SetSourcePath(filePath);

		Assimp::Importer importer;
		const uint32 flags = makeAssimpFlags(options);

		const aiScene* scene = importer.ReadFile(filePath.c_str(), flags);
		if (scene == nullptr)
		{
			if (outError) *outError = makeError("Assimp ReadFile failed", importer.GetErrorString());
			return false;
		}

		if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr)
		{
			if (outError) *outError = makeError("Assimp scene incomplete", importer.GetErrorString());
			return false;
		}

		if (scene->mNumMeshes == 0)
		{
			if (outError) *outError = "Assimp: scene has no meshes.";
			return false;
		}

		// ------------------------------------------------------------
		// Import materials
		// ------------------------------------------------------------
		if (options.ImportMaterials)
		{
			std::vector<MaterialAsset> materials;
			materials.resize(scene->mNumMaterials);

			for (uint32 i = 0; i < scene->mNumMaterials; ++i)
			{
				const aiMaterial* mat = scene->mMaterials[i];
				importOneMaterial(scene, mat, i, filePath, materials[i], pAssetManager, options, outError);
			}

			pOutMesh->SetMaterialSlots(static_cast<std::vector<MaterialAsset>&&>(materials));
		}

		// ------------------------------------------------------------
		// Decide index type (estimate)
		// ------------------------------------------------------------
		uint32 totalVertexCount = 0;

		if (options.MergeMeshes)
		{
			for (uint32 m = 0; m < scene->mNumMeshes; ++m)
			{
				const aiMesh* mesh = scene->mMeshes[m];
				if (mesh != nullptr)
					totalVertexCount += mesh->mNumVertices;
			}
		}
		else
		{
			totalVertexCount = scene->mMeshes[0]->mNumVertices;
		}

		pOutMesh->ReserveVertices(totalVertexCount);

		const VALUE_TYPE indexType = totalVertexCount <= 65535u ? VT_UINT16 : VT_UINT32;

		if (indexType == VT_UINT32) pOutMesh->SetIndicesU32({});
		else                        pOutMesh->SetIndicesU16({});

		auto& idx32 = pOutMesh->GetIndicesU32();
		auto& idx16 = pOutMesh->GetIndicesU16();

		auto pushIndex = [&](uint32 idx)
		{
			if (indexType == VT_UINT32) idx32.push_back(idx);
			else                        idx16.push_back(static_cast<uint16>(idx));
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
				return true;

			if (!mesh->HasPositions())
				return false;

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

				float3 n = hasNormals
					? float3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z)
					: float3(0.0f, 1.0f, 0.0f);
				normals.push_back(transformNormal(normalM, n));

				float3 t = hasTangents
					? float3(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z)
					: float3(1.0f, 0.0f, 0.0f);
				tangents.push_back(transformNormal(normalM, t));

				if (hasUV0)
					texCoords.push_back(float2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y));
				else
					texCoords.push_back(float2(0.0f, 0.0f));
			}

			StaticMeshAsset::Section sec = {};
			sec.BaseVertex = baseVertex;     // IMPORTANT: use baseVertex
			sec.MaterialSlot = mesh->mMaterialIndex;

			const uint32 firstIndex = pOutMesh->GetIndexCount();
			uint32 indexCount = 0;

			for (uint32 f = 0; f < mesh->mNumFaces; ++f)
			{
				const aiFace& face = mesh->mFaces[f];
				if (face.mNumIndices != 3)
					continue;

				// Chosen policy:
				// - Indices are LOCAL [0..vertexCount)
				// - Use BaseVertex at draw time
				const uint32 i0 = face.mIndices[0];
				const uint32 i1 = face.mIndices[1];
				const uint32 i2 = face.mIndices[2];

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
				return true;

			aiMatrix4x4 global = parent * node->mTransformation;

			for (uint32 i = 0; i < node->mNumMeshes; ++i)
			{
				const uint32 meshIndex = node->mMeshes[i];
				if (meshIndex >= scene->mNumMeshes)
					continue;

				const aiMesh* mesh = scene->mMeshes[meshIndex];
				if (!ImportMeshAsSection(mesh, global))
					return false;

				if (!options.MergeMeshes)
					return true;
			}

			for (uint32 c = 0; c < node->mNumChildren; ++c)
			{
				if (!self(self, node->mChildren[c], global))
					return false;

				if (!options.MergeMeshes && !sections.empty())
					return true;
			}

			return true;
		};

		{
			const aiMatrix4x4 identity;
			if (!TraverseNode(TraverseNode, scene->mRootNode, identity))
			{
				if (outError) *outError = "Assimp: failed to import meshes while traversing nodes (bake transforms).";
				return false;
			}
		}

		if (positions.empty() || sections.empty())
		{
			if (outError) *outError = "Assimp: node traversal produced empty mesh.";
			return false;
		}

		// ------------------------------------------------------------
		// Commit to asset (SoA)
		// ------------------------------------------------------------
		pOutMesh->SetPositions(static_cast<std::vector<float3>&&>(positions));
		pOutMesh->SetNormals(static_cast<std::vector<float3>&&>(normals));
		pOutMesh->SetTangents(static_cast<std::vector<float3>&&>(tangents));
		pOutMesh->SetTexCoords(static_cast<std::vector<float2>&&>(texCoords));
		pOutMesh->SetSections(static_cast<std::vector<StaticMeshAsset::Section>&&>(sections));

		pOutMesh->RecomputeBounds();

		if (!pOutMesh->IsValid())
		{
			if (outError) *outError = "Assimp: imported mesh is invalid (empty vertices/indices or inconsistent attributes/sections/material slots).";
			return false;
		}

		return true;
	}

} // namespace shz
