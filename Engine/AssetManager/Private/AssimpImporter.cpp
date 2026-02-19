#include "pch.h"
#include "Engine/AssetManager/Public/AssimpImporter.h"

#include <vector>
#include <string>
#include <utility>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <cctype>
#include <unordered_map>
#include <optional>
#include <algorithm>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/RuntimeData/Public/Material.h"
#include "Engine/RuntimeData/Public/Texture.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

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

	static inline bool isControlChar(unsigned char c) noexcept
	{
		return c < 32u;
	}

	static inline std::string toLowerASCII(std::string s)
	{
		for (char& c : s)
			c = (char)std::tolower((unsigned char)c);
		return s;
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

		// remove trailing \0 and control chars (some exporters embed weird tail bytes)
		while (!s.empty() && (s.back() == '\0' || isControlChar((unsigned char)s.back())))
			s.pop_back();

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

	static std::string getFileNameOnly(const std::string& path)
	{
		if (path.empty())
			return {};
		const size_t pos = path.find_last_of("/\\");
		if (pos == std::string::npos)
			return path;
		return path.substr(pos + 1);
	}

	// Fix patterns like "c:/c:/dev/..." or "C:\C:\dev\..."
	static std::string fixDuplicateDrivePrefix(std::string s)
	{
		s = sanitizePathString(static_cast<std::string&&>(s));
		if (s.size() < 6)
			return s;

		auto IsAlpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };

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

		return s;
	}

	// More FBX-ish sanitize:
	// - strip file:// prefixes
	// - strip leading "./"
	// - normalize slashes
	// - fix duplicate drive
	static std::string sanitizeFbxTexturePath(std::string s)
	{
		s = sanitizePathString(std::move(s));

		const char* k1 = "file:///";
		const char* k2 = "file://";
		if (s.rfind(k1, 0) == 0) s = s.substr(std::strlen(k1));
		else if (s.rfind(k2, 0) == 0) s = s.substr(std::strlen(k2));

		while (s.rfind("./", 0) == 0)
			s = s.substr(2);

		s = fixDuplicateDrivePrefix(std::move(s));
		return s;
	}

	static std::string normalizeResolvedPath(const std::filesystem::path& p)
	{
		std::filesystem::path n = p.lexically_normal();
		std::string out = n.generic_string();
		out = sanitizePathString(static_cast<std::string&&>(out));
		out = fixDuplicateDrivePrefix(static_cast<std::string&&>(out));
		return out;
	}

	// ------------------------------------------------------------
	// Windows path decoding (UTF-8 first, then ANSI codepage fallback)
	// ------------------------------------------------------------
	static std::filesystem::path pathFromAssimpString(const std::string& s)
	{
#if defined(_WIN32)
		auto ToWide = [](const std::string& in, UINT cp, std::wstring& out) -> bool
		{
			out.clear();
			if (in.empty())
				return false;

			const int flags = (cp == CP_UTF8) ? MB_ERR_INVALID_CHARS : 0;
			int len = MultiByteToWideChar(cp, flags, in.c_str(), (int)in.size(), nullptr, 0);
			if (len <= 0)
				return false;

			out.resize((size_t)len);
			int written = MultiByteToWideChar(cp, flags, in.c_str(), (int)in.size(), out.data(), len);
			if (written != len)
				return false;

			return true;
		};

		std::wstring w;
		if (ToWide(s, CP_UTF8, w))
			return std::filesystem::path(w);

		// ANSI fallback (CP_ACP)
		if (ToWide(s, CP_ACP, w))
			return std::filesystem::path(w);

		// last resort: treat as narrow
		return std::filesystem::path(s);
#else
		return std::filesystem::path(s);
#endif
	}

	static inline uint32 makeAssimpFlags(const AssimpImportSettings& s)
	{
		uint32 flags = 0;

		if (s.bTriangulate)           flags |= aiProcess_Triangulate;
		if (s.bJoinIdenticalVertices) flags |= aiProcess_JoinIdenticalVertices;

		// Normal generation
		if (s.bGenNormals)
		{
			if (s.bGenSmoothNormals) flags |= aiProcess_GenSmoothNormals;
			else                     flags |= aiProcess_GenNormals;
		}

		// Tangent space
		if (s.bGenTangents || s.bCalcTangentSpace)
			flags |= aiProcess_CalcTangentSpace;

		// Cleanup
		flags |= aiProcess_ImproveCacheLocality;
		flags |= aiProcess_RemoveRedundantMaterials;
		flags |= aiProcess_SortByPType;

		if (s.bFlipUVs) flags |= aiProcess_FlipUVs;
		if (s.bConvertToLeftHanded) flags |= aiProcess_MakeLeftHanded;

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
		if (n.Length() < 1e-6f)
		{
			return float3(0.0f, 0.0f, 1.0f);
		}
		const float x = nrm.a1 * n.x + nrm.a2 * n.y + nrm.a3 * n.z;
		const float y = nrm.b1 * n.x + nrm.b2 * n.y + nrm.b3 * n.z;
		const float z = nrm.c1 * n.x + nrm.c2 * n.y + nrm.c3 * n.z;
		return Vector3::Normalize(float3(x, y, z));
	}

	// ------------------------------------------------------------
	// Filesystem helpers
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

	static bool ensureDirectory(const std::filesystem::path& p, std::string* outError)
	{
		std::error_code ec;
		if (std::filesystem::exists(p, ec))
			return true;

		if (!std::filesystem::create_directories(p, ec))
		{
			if (outError) *outError = "EnsureDirectory: create_directories failed: " + ec.message();
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

		const std::filesystem::path sceneDir = pathFromAssimpString(getDirectoryOfPath(sceneFilePath));
		const std::filesystem::path dumpDir = sceneDir / "_embedded_textures";

		if (!ensureDirectory(dumpDir, outError))
			return false;

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

	static bool fileExists(const std::filesystem::path& p)
	{
		std::error_code ec;
		return std::filesystem::exists(p, ec) && !ec;
	}

	// Case-insensitive filename match in a directory:
	// - if exact path exists, return it
	// - else scan parent directory entries and match filename lower
	static bool tryResolveCaseInsensitiveInSameDir(const std::filesystem::path& p, std::filesystem::path& out)
	{
		out.clear();

		if (fileExists(p))
		{
			out = p;
			return true;
		}

		const std::filesystem::path dir = p.parent_path();
		const std::filesystem::path want = p.filename();

		if (dir.empty() || want.empty())
			return false;

		std::error_code ec;
		if (!std::filesystem::exists(dir, ec) || ec)
			return false;

		const std::string wantLower = toLowerASCII(want.generic_string());

		for (auto it = std::filesystem::directory_iterator(dir, ec);
			!ec && it != std::filesystem::directory_iterator();
			it.increment(ec))
		{
			if (ec) break;
			if (!it->is_regular_file(ec)) continue;

			const std::string haveLower = toLowerASCII(it->path().filename().generic_string());
			if (haveLower == wantLower)
			{
				out = it->path();
				return true;
			}
		}

		return false;
	}

	// ------------------------------------------------------------
	// Texture search index (build once per import)
	// ------------------------------------------------------------
	struct TexturePathIndex final
	{
		// key: lower(filename.ext) -> list of full paths
		std::unordered_map<std::string, std::vector<std::filesystem::path>> ByBaseLower = {};
	};

	static bool isLikelyTextureExt(const std::filesystem::path& p)
	{
		const std::string ext = toLowerASCII(p.extension().generic_string());
		return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".dds" || ext == ".exr" || ext == ".bmp" || ext == ".tif" || ext == ".tiff";
	}

	static TexturePathIndex buildTextureIndex(const std::vector<std::filesystem::path>& roots)
	{
		TexturePathIndex idx;

		std::error_code ec;
		for (const std::filesystem::path& root : roots)
		{
			if (root.empty())
				continue;

			if (!std::filesystem::exists(root, ec) || ec)
				continue;

			for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
				!ec && it != std::filesystem::recursive_directory_iterator();
				it.increment(ec))
			{
				if (ec) break;

				if (!it->is_regular_file(ec))
					continue;

				const std::filesystem::path p = it->path();
				if (!isLikelyTextureExt(p))
					continue;

				const std::string baseLower = toLowerASCII(p.filename().generic_string());
				idx.ByBaseLower[baseLower].push_back(p);
			}
		}

		return idx;
	}

	static bool findByFileNameIndex(
		const TexturePathIndex& idx,
		const std::string& fileName,
		std::filesystem::path& out)
	{
		out.clear();
		const std::string key = toLowerASCII(fileName);
		auto it = idx.ByBaseLower.find(key);
		if (it == idx.ByBaseLower.end() || it->second.empty())
			return false;

		out = it->second.front();
		return true;
	}

	static std::vector<std::filesystem::path> makeDefaultTextureSearchRoots(const std::filesystem::path& sceneDir)
	{
		std::vector<std::filesystem::path> roots;
		roots.reserve(8);

		if (!sceneDir.empty())
		{
			roots.push_back(sceneDir);
			roots.push_back(sceneDir / "Textures");
			roots.push_back(sceneDir / "textures");
		}

		const std::filesystem::path parent = sceneDir.parent_path();
		if (!parent.empty())
		{
			roots.push_back(parent / "Textures");
			roots.push_back(parent / "textures");
		}

		// de-dup (lexically)
		for (std::filesystem::path& p : roots)
			p = p.lexically_normal();

		std::sort(roots.begin(), roots.end());
		roots.erase(std::unique(roots.begin(), roots.end()), roots.end());

		return roots;
	}

	// ------------------------------------------------------------
	// Material import helpers
	// ------------------------------------------------------------
	static bool resolveTexturePathRobust(
		const aiScene* scene,
		const aiMaterial* mat,
		aiTextureType type,
		const std::string& sceneFilePath,
		const std::filesystem::path& sceneDir,
		const std::vector<std::filesystem::path>& searchRoots,
		const TexturePathIndex* pIndex,
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

		std::string raw = sanitizeFbxTexturePath(cstr);
		if (raw.empty())
			return false;

		// Embedded "*0" ...
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

		const std::filesystem::path pRaw = pathFromAssimpString(raw);

		auto acceptFound = [&](const std::filesystem::path& found) -> bool
		{
			std::filesystem::path resolved = found.lexically_normal();

			std::filesystem::path ci;
			if (tryResolveCaseInsensitiveInSameDir(resolved, ci))
				resolved = ci;

			if (!fileExists(resolved))
				return false;

			outPath = normalizeResolvedPath(resolved);
			outPath = fixDuplicateDrivePrefix(static_cast<std::string&&>(outPath));
			return !outPath.empty();
		};

		// 1) Try as absolute (or rooted) first
		if (pRaw.is_absolute() || pRaw.has_root_name())
		{
			if (acceptFound(pRaw))
				return true;

			// absolute but invalid on this machine -> fall back to filename search
		}

		// 2) Try relative against sceneDir (classic)
		{
			const std::filesystem::path candidate = (sceneDir / pRaw).lexically_normal();
			if (acceptFound(candidate))
				return true;
		}

		// 3) Try search roots with full relative path (sometimes raw contains subdirs)
		for (const std::filesystem::path& root : searchRoots)
		{
			if (root.empty())
				continue;

			const std::filesystem::path candidate = (root / pRaw).lexically_normal();
			if (acceptFound(candidate))
				return true;
		}

		// 4) Try filename-only across common roots
		const std::string fileName = getFileNameOnly(raw);
		if (!fileName.empty())
		{
			for (const std::filesystem::path& root : searchRoots)
			{
				if (root.empty())
					continue;

				const std::filesystem::path candidate = (root / pathFromAssimpString(fileName)).lexically_normal();
				if (acceptFound(candidate))
					return true;
			}

			// 5) Index lookup (recursive search done once)
			if (pIndex != nullptr)
			{
				std::filesystem::path indexed;
				if (findByFileNameIndex(*pIndex, fileName, indexed))
				{
					if (acceptFound(indexed))
						return true;
				}
			}
		}

		if (outError)
		{
			*outError = "Texture path resolve failed. raw='" + raw + "' sceneDir='" + sceneDir.generic_string() + "'";
		}

		return false;
	}

	static bool resolveTexturePathAnyRobust(
		const aiScene* scene,
		const aiMaterial* mat,
		const std::initializer_list<aiTextureType>& types,
		const std::string& sceneFilePath,
		const std::filesystem::path& sceneDir,
		const std::vector<std::filesystem::path>& searchRoots,
		const TexturePathIndex* pIndex,
		std::string& outPath,
		std::string* outError)
	{
		for (aiTextureType t : types)
		{
			outPath.clear();
			if (resolveTexturePathRobust(scene, mat, t, sceneFilePath, sceneDir, searchRoots, pIndex, outPath, outError))
				return true;
		}
		outPath.clear();
		return false;
	}

	// ------------------------------------------------------------
	// Improved material import (glTF alpha/twosided + robust paths)
	// ------------------------------------------------------------
	static MaterialId importOneMaterial(
		const aiScene* scene,
		const aiMaterial* mat,
		uint32 materialIndex,
		const std::string& matTmpl,
		const std::string& sceneFilePath,
		const std::filesystem::path& sceneDir,
		const std::vector<std::filesystem::path>& searchRoots,
		const TexturePathIndex* pIndex,
		AssetManager* pAssetManager,
		const AssimpImportSettings& setting,
		std::string* outError)
	{
		if (mat == nullptr)
			return 0;

		std::string name;
		{
			aiString n;
			if (mat->Get(AI_MATKEY_NAME, n) == AI_SUCCESS && n.length > 0)
			{
				name = n.C_Str();
			}
			else
			{
				name = std::string("Material_") + std::to_string(materialIndex);
			}
		}

		const std::string templateName = matTmpl;

		MaterialManager* pMaterialManager = MaterialManager::GetInstance();
		MaterialId outId = pMaterialManager->CreateMaterial(name, templateName);
		if (outId == 0)
			return 0;

		Material& material = pMaterialManager->GetMaterial(outId);

		auto ieq = [](const std::string& a, const char* b)
		{
			if (!b) return false;
			if (a.size() != std::strlen(b)) return false;
			for (size_t i = 0; i < a.size(); ++i)
			{
				const char ca = (char)std::tolower((unsigned char)a[i]);
				const char cb = (char)std::tolower((unsigned char)b[i]);
				if (ca != cb) return false;
			}
			return true;
		};

		// glTF alpha mode / cutoff / twosided
		std::string alphaMode = "OPAQUE";
#if defined(AI_MATKEY_GLTF_ALPHAMODE)
		{
			aiString am;
			if (mat->Get(AI_MATKEY_GLTF_ALPHAMODE, am) == AI_SUCCESS && am.length > 0)
			{
				alphaMode = am.C_Str();
			}
		}
#endif

		float alphaCutoff = 0.5f;
#if defined(AI_MATKEY_GLTF_ALPHACUTOFF)
		{
			float v = alphaCutoff;
			if (mat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, v) == AI_SUCCESS)
			{
				alphaCutoff = v;
			}
		}
#endif

		bool twoSided = false;
#if defined(AI_MATKEY_TWOSIDED)
		{
			int v = 0;
			if (mat->Get(AI_MATKEY_TWOSIDED, v) == AI_SUCCESS)
			{
				twoSided = (v != 0);
			}
		}
#endif
		bool bFoliage = false;
		if ((name.find("foliage") != std::string::npos) || 
			(name.find("Foliage") != std::string::npos) ||
			(name.find("clusters") != std::string::npos) || 
			(name.find("Clusters") != std::string::npos) ||
			(name.find("billboard") != std::string::npos) ||
			(name.find("Billboard") != std::string::npos))
		{
			twoSided = true;
			bFoliage = true;
		}

		// BaseColor factor
		float baseColor[4] = { 1, 1, 1, 1 };
		{
			aiColor4D c(1, 1, 1, 1);
#if defined(AI_MATKEY_BASE_COLOR)
			if (mat->Get(AI_MATKEY_BASE_COLOR, c) == AI_SUCCESS)
			{
				baseColor[0] = c.r; baseColor[1] = c.g; baseColor[2] = c.b; baseColor[3] = c.a;
			}
			else
#endif
				if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, c) == AI_SUCCESS)
				{
					baseColor[0] = c.r; baseColor[1] = c.g; baseColor[2] = c.b; baseColor[3] = c.a;
				}
		}

		// Opacity multiplier (common non-glTF)
		{
			float opacity = 1.0f;
			if (mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
			{
				baseColor[3] *= opacity;
			}
		}

		material.SetFloat4("g_BaseColorFactor", baseColor);

		// Emissive
		{
			aiColor3D e(0, 0, 0);
			float emissive[3] = { 0, 0, 0 };

			if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, e) == AI_SUCCESS)
			{
				emissive[0] = e.r; emissive[1] = e.g; emissive[2] = e.b;
			}

			material.SetFloat3("g_EmissiveFactor", emissive);
			material.SetFloat("g_EmissiveIntensity", 1.0f);
		}

		// Metallic / Roughness
		{
			float metallic = 0.0f;
			float roughness = 1.0f;

#if defined(AI_MATKEY_METALLIC_FACTOR)
			{
				float v = metallic;
				if (mat->Get(AI_MATKEY_METALLIC_FACTOR, v) == AI_SUCCESS)
				{
					metallic = v;
				}
			}
#endif
#if defined(AI_MATKEY_ROUGHNESS_FACTOR)
			{
				float v = roughness;
				if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, v) == AI_SUCCESS)
				{
					roughness = v;
				}
			}
#endif
			material.SetFloat("g_MetallicFactor", metallic);
			material.SetFloat("g_RoughnessFactor", roughness);
		}

		material.SetFloat("g_OcclusionStrength", 1.0f);
		material.SetFloat("g_AlphaCutoff", alphaCutoff);
		material.SetFloat("g_NormalScale", 1.0f);

		// Decide blend/cull/depth
		MATERIAL_BLEND_MODE blendMode = MATERIAL_BLEND_MODE_OPAQUE;
		if (ieq(alphaMode, "BLEND"))
		{
			blendMode = MATERIAL_BLEND_MODE_TRANSPARENT;
		}
		else if (ieq(alphaMode, "MASK"))
		{
			twoSided = true;
			blendMode = MATERIAL_BLEND_MODE_MASKED;
		}
		else
		{
			// exporter didn't emit alphaMode but alpha<1
			if (baseColor[3] < 0.999f)
			{
				blendMode = MATERIAL_BLEND_MODE_TRANSPARENT;
			}
		}

		material.SetBlendMode(blendMode);
		// material.SetCullMode(twoSided ? CULL_MODE_NONE : CULL_MODE_BACK);

		if (blendMode == MATERIAL_BLEND_MODE_TRANSPARENT)
		{
			material.SetDepthEnable(true);
			material.SetDepthWriteEnable(false);
			material.SetDepthFunc(COMPARISON_FUNC_LESS_EQUAL);
		}
		else
		{
			material.SetDepthEnable(true);
			material.SetDepthWriteEnable(true);
			material.SetDepthFunc(COMPARISON_FUNC_LESS_EQUAL);
		}

		// Bind textures
		uint32 materialFlag = 0;

		auto bindTexture = [&](const char* shaderVar, const std::string& texPath) -> bool
		{
			if (!setting.bRegisterTextureAssets)
				return false;
			if (pAssetManager == nullptr)
				return false;
			if (shaderVar == nullptr || shaderVar[0] == '\0')
				return false;
			if (texPath.empty())
				return false;

			const AssetRef<Texture> texRef = pAssetManager->RegisterAsset<Texture>(texPath);
			if (!texRef)
				return false;

			material.SetTextureAssetRef(shaderVar, texRef);
			return true;
		};

		// BaseColor
		{
			std::string path;
			if (resolveTexturePathAnyRobust(scene, mat,
				{ aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE },
				sceneFilePath, sceneDir, searchRoots, pIndex, path, outError))
			{
				if (bindTexture("g_BaseColorTex", path))
					materialFlag |= hlsl::MAT_HAS_BASECOLOR;
			}
		}

		// Normal
		{
			std::string path;
			if (resolveTexturePathAnyRobust(scene, mat,
				{ aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT },
				sceneFilePath, sceneDir, searchRoots, pIndex, path, outError))
			{
				if (bindTexture("g_NormalTex", path))
					materialFlag |= hlsl::MAT_HAS_NORMAL;
			}
		}

		// Metallic/Roughness (glTF: metallicRoughnessTexture)
		{
			std::string path;
			if (resolveTexturePathAnyRobust(scene, mat,
				{ aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN },
				sceneFilePath, sceneDir, searchRoots, pIndex, path, outError))
			{
				if (bindTexture("g_MetallicRoughnessTex", path))
					materialFlag |= hlsl::MAT_HAS_MR;
			}
		}

		// AO (glTF: occlusionTexture)
		{
			std::string path;
			if (resolveTexturePathAnyRobust(scene, mat,
				{ aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP, aiTextureType_AMBIENT },
				sceneFilePath, sceneDir, searchRoots, pIndex, path, outError))
			{
				if (bindTexture("g_AOTex", path))
					materialFlag |= hlsl::MAT_HAS_AO;
			}
		}

		// Emissive
		{
			std::string path;
			if (resolveTexturePathAnyRobust(scene, mat,
				{ aiTextureType_EMISSIVE },
				sceneFilePath, sceneDir, searchRoots, pIndex, path, outError))
			{
				if (bindTexture("g_EmissiveTex", path))
					materialFlag |= hlsl::MAT_HAS_EMISSIVE;
			}
		}

		// Height/Displacement
		{
			std::string path;
			if (resolveTexturePathAnyRobust(scene, mat,
				{ aiTextureType_HEIGHT, aiTextureType_DISPLACEMENT },
				sceneFilePath, sceneDir, searchRoots, pIndex, path, outError))
			{
				if (bindTexture("g_HeightTex", path))
					materialFlag |= hlsl::MAT_HAS_HEIGHT;
			}
		}

		material.SetUint("g_MaterialFlags", materialFlag);
		if (bFoliage)
		{
			material.SetUint("g_ShadingMode", hlsl::FOLIAGE);
		}
		else
		{
			material.SetUint("g_ShadingMode", hlsl::DEFAULT_LIT);
		}
		return outId;
	}

	// ------------------------------------------------------------
	// AssimpImporter::operator()
	// ------------------------------------------------------------
	std::unique_ptr<AssetObject> AssimpImporter::operator()(
		AssetManager& assetManager,
		const AssetMeta& meta,
		uint64* pOutResidentBytes,
		std::string* pOutError) const
	{
		if (pOutResidentBytes)
			*pOutResidentBytes = 0;
		if (pOutError)
			pOutError->clear();

		if (meta.SourcePath.empty())
		{
			if (pOutError) *pOutError = "AssimpImporter: meta.SourcePath is empty.";
			return {};
		}

		const AssimpImportSettings s = meta.TryGetAssimpMeta() ? *meta.TryGetAssimpMeta() : AssimpImportSettings{};
		const uint32 flags = makeAssimpFlags(s);

		AssimpAsset out = {};
		out.SourcePath = meta.SourcePath;
		out.Importer = std::make_shared<Assimp::Importer>();
		out.Scene = out.Importer->ReadFile(out.SourcePath.c_str(), flags);

		if (out.Scene == nullptr)
		{
			if (pOutError) *pOutError = makeError("Assimp ReadFile failed", out.Importer->GetErrorString());
			out.Clear();
			return {};
		}

		if ((out.Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || out.Scene->mRootNode == nullptr)
		{
			if (pOutError) *pOutError = makeError("Assimp scene incomplete", out.Importer->GetErrorString());
			out.Clear();
			return {};
		}

		if (pOutResidentBytes)
			*pOutResidentBytes = static_cast<uint64>(out.SourcePath.size());

		return std::make_unique<TypedAssetObject<AssimpAsset>>(static_cast<AssimpAsset&&>(out));
	}

	// ------------------------------------------------------------
	// BuildStaticMeshAsset (AssimpAsset -> StaticMeshAsset)
	// ------------------------------------------------------------
	bool BuildStaticMeshAsset(
		const AssimpAsset& assimpAsset,
		StaticMeshLevel* pOutMesh,
		const AssimpImportSettings& setting,
		const std::string& materialTemplateName,
		std::string* outError,
		AssetManager* pAssetManager)
	{
		ASSERT(pOutMesh, "pOutMesh is null.");
		ASSERT(assimpAsset.IsValid(), "assimpAsset is invalid.");
		pOutMesh->Clear();

		const aiScene* scene = assimpAsset.Scene;
		const std::string& filePath = assimpAsset.SourcePath;

		if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr)
		{
			if (outError) *outError = "scene incomplete or missing root node.";
			return false;
		}

		if (scene->mNumMeshes == 0)
		{
			if (outError) *outError = "BuildStaticMeshAsset: scene has no meshes.";
			return false;
		}

		// Scene directory + search roots + index
		const std::filesystem::path sceneDir = pathFromAssimpString(getDirectoryOfPath(filePath)).lexically_normal();
		const std::vector<std::filesystem::path> searchRoots = makeDefaultTextureSearchRoots(sceneDir);

		// Build index once (only if materials and you expect path issues)
		TexturePathIndex texIndex = {};
		const TexturePathIndex* pTexIndex = nullptr;

		if (setting.bImportMaterials)
		{
			texIndex = buildTextureIndex(searchRoots);
			pTexIndex = &texIndex;
		}

		// ------------------------------------------------------------
		// Import materials (optional) - robust
		// ------------------------------------------------------------
		if (setting.bImportMaterials)
		{
			std::vector<MaterialId> materials;
			materials.resize(scene->mNumMaterials, 0);

			// fallback material
			MaterialId fallbackId = 0;
			{
				MaterialManager* pMM = MaterialManager::GetInstance();
				fallbackId = pMM->CreateMaterial(assimpAsset.SourcePath, materialTemplateName);
				Material& m = pMM->GetMaterial(fallbackId);
				const float base[4] = { 1,1,1,1 };
				m.SetFloat4("g_BaseColorFactor", base);
				m.SetFloat("g_MetallicFactor", 0.0f);
				m.SetFloat("g_RoughnessFactor", 1.0f);
				m.SetUint("g_MaterialFlags", 0);
			}

			for (uint32 i = 0; i < scene->mNumMaterials; ++i)
			{
				const aiMaterial* mat = scene->mMaterials[i];

				MaterialId mid = importOneMaterial(
					scene,
					mat,
					i,
					materialTemplateName,
					filePath,
					sceneDir,
					searchRoots,
					pTexIndex,
					pAssetManager,
					setting,
					outError);

				if (mid == 0)
					mid = fallbackId;

				materials[i] = mid;
			}

			pOutMesh->SetMaterialSlots(std::move(materials));
		}

		// ------------------------------------------------------------
		// Decide index type (estimate)
		// ------------------------------------------------------------
		uint32 totalVertexCount = 0;

		if (setting.bMergeMeshes)
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
			else idx16.push_back(static_cast<uint16>(idx));
		};

		// ------------------------------------------------------------
		// Import meshes by traversing nodes (BAKE transforms)
		// ------------------------------------------------------------
		std::vector<float3> positions;
		std::vector<float3> normals;
		std::vector<float3> tangents;
		std::vector<float2> texCoords;

		positions.reserve(totalVertexCount);
		normals.reserve(totalVertexCount);
		tangents.reserve(totalVertexCount);
		texCoords.reserve(totalVertexCount);

		std::vector<StaticMeshLevel::Section> sections;
		sections.reserve(setting.bMergeMeshes ? scene->mNumMeshes : 1);

		auto importMeshAsSection = [&](const aiMesh* mesh, const aiMatrix4x4& global)
		{
			ASSERT(mesh, "mesh is null.");
			ASSERT(mesh->HasPositions(), "mesh has no positions.");

			const uint32 baseVertex = static_cast<uint32>(positions.size());
			const uint32 vertexCount = mesh->mNumVertices;

			const bool hasNormals = mesh->HasNormals();
			const bool hasTangents = (mesh->mTangents != nullptr) && (mesh->mBitangents != nullptr);
			const bool hasUV0 = mesh->HasTextureCoords(0);

			const aiMatrix3x3 normalM = makeNormalMatrix(global);

			for (uint32 v = 0; v < vertexCount; ++v)
			{
				const aiVector3D& pA = mesh->mVertices[v];

				float3 p = float3(pA.x, pA.y, pA.z) * setting.UniformScale;
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

			StaticMeshLevel::Section sec = {};
			sec.BaseVertex = baseVertex;         // 인덱스는 로컬이므로 draw에서 BaseVertex 사용
			sec.MaterialSlot = mesh->mMaterialIndex;

			const uint32 firstIndex = pOutMesh->GetIndexCount();
			uint32 indexCount = 0;

			for (uint32 f = 0; f < mesh->mNumFaces; ++f)
			{
				const aiFace& face = mesh->mFaces[f];
				ASSERT(face.mNumIndices == 3, "Only triangles are supported.");

				pushIndex(face.mIndices[0]);
				pushIndex(face.mIndices[1]);
				pushIndex(face.mIndices[2]);
				indexCount += 3;
			}

			sec.FirstIndex = firstIndex;
			sec.IndexCount = indexCount;

			sections.push_back(sec);
		};

		auto traverseNode = [&](auto&& self, const aiNode* node, const aiMatrix4x4& parent) -> bool
		{
			ASSERT(node, "node is null.");

			aiMatrix4x4 global = parent * node->mTransformation;

			for (uint32 i = 0; i < node->mNumMeshes; ++i)
			{
				const uint32 meshIndex = node->mMeshes[i];
				ASSERT(meshIndex < scene->mNumMeshes, "meshIndex out of range.");

				const aiMesh* pMesh = scene->mMeshes[meshIndex];
				importMeshAsSection(pMesh, global);

				if (!setting.bMergeMeshes)
					return true;
			}

			for (uint32 c = 0; c < node->mNumChildren; ++c)
			{
				if (!self(self, node->mChildren[c], global))
					return false;

				if (!setting.bMergeMeshes && !sections.empty())
					return true;
			}

			return true;
		};

		{
			const aiMatrix4x4 identity;
			if (!traverseNode(traverseNode, scene->mRootNode, identity))
			{
				if (outError) *outError = "BuildStaticMeshAsset: node traversal failed.";
				return false;
			}
		}

		if (positions.empty() || sections.empty())
		{
			if (outError) *outError = "BuildStaticMeshAsset: produced empty mesh.";
			return false;
		}

		// Commit SoA
		pOutMesh->SetPositions(static_cast<std::vector<float3>&&>(positions));
		pOutMesh->SetNormals(static_cast<std::vector<float3>&&>(normals));
		pOutMesh->SetTangents(static_cast<std::vector<float3>&&>(tangents));
		pOutMesh->SetTexCoords(static_cast<std::vector<float2>&&>(texCoords));
		pOutMesh->SetSections(static_cast<std::vector<StaticMeshLevel::Section>&&>(sections));

		pOutMesh->RecomputeBounds();

		if (!pOutMesh->IsValid())
		{
			if (outError) *outError = "BuildStaticMeshAsset: StaticMeshAsset validation failed.";
			return false;
		}

		return true;
	}

} // namespace shz
