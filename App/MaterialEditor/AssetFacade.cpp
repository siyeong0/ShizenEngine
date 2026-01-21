// ============================================================================
// MaterialEditorAssetFacade.cpp
// ============================================================================

#include "AssetFacade.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "Engine/AssetRuntime/Pipeline/Public/AssimpImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/MaterialImporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/TextureImporter.h"

#include "Engine/AssetRuntime/Pipeline/Public/StaticMeshExporter.h"
#include "Engine/AssetRuntime/Pipeline/Public/MaterialExporter.h"

namespace shz
{
	// ------------------------------------------------------------
	// Small helpers
	// ------------------------------------------------------------
	static inline void setErr(std::string* outError, const std::string& s)
	{
		if (outError) *outError = s;
	}

	static inline bool endsWith(const std::string& s, const std::string& suffix)
	{
		if (suffix.size() > s.size()) return false;
		return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
	}

	// ------------------------------------------------------------
	// Register default importers/exporters
	// ------------------------------------------------------------
	void MaterialEditorAssetFacade::RegisterDefaultImporters()
	{
		if (!m_pAM)
			return;

		// Importers
		{
			m_pAM->RegisterImporter(AssetTypeTraits<TextureAsset>::TypeID, TextureImporter{});
			m_pAM->RegisterImporter(AssetTypeTraits<StaticMeshAsset>::TypeID, StaticMeshAssetImporter{});
			m_pAM->RegisterImporter(AssetTypeTraits<MaterialAsset>::TypeID, MaterialAssetImporter{});
			m_pAM->RegisterImporter(AssetTypeTraits<AssimpAsset>::TypeID, AssimpImporter{});
		}

		// Exporters (필요한 것만)
		{
			m_pAM->RegisterExporter(AssetTypeTraits<StaticMeshAsset>::TypeID, StaticMeshAssetExporter{});
			m_pAM->RegisterExporter(AssetTypeTraits<MaterialAsset>::TypeID, MaterialAssetExporter{});
		}
	}

	// ------------------------------------------------------------
	// Path utilities
	// ------------------------------------------------------------
	std::string MaterialEditorAssetFacade::SanitizeFilePath(std::string s)
	{
		// trim quotes
		if (!s.empty() && (s.front() == '"' || s.front() == '\''))
			s.erase(s.begin());
		if (!s.empty() && (s.back() == '"' || s.back() == '\''))
			s.pop_back();

		// normalize slashes
		for (char& c : s)
		{
			if (c == '\\')
				c = '/';
		}

		// trim spaces
		auto ltrim = [](std::string& str)
		{
			while (!str.empty() && std::isspace((unsigned char)str.front()))
				str.erase(str.begin());
		};
		auto rtrim = [](std::string& str)
		{
			while (!str.empty() && std::isspace((unsigned char)str.back()))
				str.pop_back();
		};

		ltrim(s);
		rtrim(s);

		return s;
	}

	std::string MaterialEditorAssetFacade::GetLowerExt(const std::string& path)
	{
		std::filesystem::path p(path);
		std::string ext = p.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return ext;
	}

	EEditorAssetKind MaterialEditorAssetFacade::ClassifyPathByExtension(const std::string& path)
	{
		const std::string ext = GetLowerExt(path);

		// Assimp mesh formats
		if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb")
			return EEditorAssetKind::AssimpMesh;

		// Engine-native cpu mesh
		if (ext == ".staticmesh" || endsWith(path, ".staticmesh.json"))
			return EEditorAssetKind::StaticMesh;

		// Material
		if (ext == ".material" || ext == ".mat" || endsWith(path, ".material.json") || endsWith(path, ".mat.json"))
			return EEditorAssetKind::Material;

		// Texture (대충)
		if (ext == ".png" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".hdr" || ext == ".exr")
			return EEditorAssetKind::Texture;

		return EEditorAssetKind::Unknown;
	}

	// ------------------------------------------------------------
	// Mesh load (extension-driven)
	// ------------------------------------------------------------
	bool MaterialEditorAssetFacade::LoadCpuMeshBlocking(
		const std::string& path,
		LoadedCpuMesh& outMesh,
		std::string* outError,
		EAssetLoadFlags flags)
	{
		outMesh = {};

		if (!m_pAM)
		{
			setErr(outError, "AssetManager is null.");
			return false;
		}

		const std::string p = SanitizeFilePath(path);
		if (p.empty())
		{
			setErr(outError, "Path is empty.");
			return false;
		}

		const EEditorAssetKind kind = ClassifyPathByExtension(p);

		// 1) Native StaticMesh asset (load directly)
		if (kind == EEditorAssetKind::StaticMesh)
		{
			AssetRef<StaticMeshAsset> ref = m_pAM->RegisterAsset<StaticMeshAsset>(p);
			AssetPtr<StaticMeshAsset> meshPtr = m_pAM->LoadBlocking(ref, flags);

			if (!meshPtr)
			{
				setErr(outError, "Failed to load StaticMeshAsset.");
				return false;
			}

			outMesh.StaticMeshPtr = std::move(meshPtr);
			return true;
		}

		// 2) Assimp mesh (load AssimpAsset then build a CPU StaticMeshAsset)
		if (kind == EEditorAssetKind::AssimpMesh)
		{
			AssetRef<AssimpAsset> ref = m_pAM->RegisterAsset<AssimpAsset>(p);
			AssetPtr<AssimpAsset> assimpPtr = m_pAM->LoadBlocking(ref, flags);

			if (!assimpPtr)
			{
				setErr(outError, "Failed to load AssimpAsset.");
				return false;
			}

			// Build CPU mesh (editor-owned)
			auto built = std::make_unique<StaticMeshAsset>();

			AssimpImportSettings settings = {}; // AssetMeta에 들어있던 설정을 여기서 기본값 사용
			std::string err;
			const bool ok = BuildStaticMeshAsset(*assimpPtr.Get(), built.get(), settings, &err, m_pAM);

			if (!ok)
			{
				setErr(outError, err.empty() ? "BuildStaticMeshAsset failed." : err);
				return false;
			}

			outMesh.AssimpPtr = std::move(assimpPtr);
			outMesh.BuiltMesh = std::move(built);
			return true;
		}

		setErr(outError, "Unsupported mesh file type.");
		return false;
	}

	// ------------------------------------------------------------
	// Material load (JSON or whatever importer supports)
	// ------------------------------------------------------------
	bool MaterialEditorAssetFacade::LoadMaterialBlocking(
		const std::string& path,
		AssetPtr<MaterialAsset>& outMat,
		std::string* outError,
		EAssetLoadFlags flags)
	{
		outMat = {};

		if (!m_pAM)
		{
			setErr(outError, "AssetManager is null.");
			return false;
		}

		const std::string p = SanitizeFilePath(path);
		if (p.empty())
		{
			setErr(outError, "Path is empty.");
			return false;
		}

		AssetRef<MaterialAsset> ref = m_pAM->RegisterAsset<MaterialAsset>(p);
		outMat = m_pAM->LoadBlocking(ref, flags);

		if (!outMat)
		{
			setErr(outError, "Failed to load MaterialAsset.");
			return false;
		}

		return true;
	}

	// ------------------------------------------------------------
	// Texture load
	// ------------------------------------------------------------
	bool MaterialEditorAssetFacade::LoadTextureBlocking(
		const std::string& path,
		AssetPtr<TextureAsset>& outTex,
		std::string* outError,
		EAssetLoadFlags flags)
	{
		outTex = {};

		if (!m_pAM)
		{
			setErr(outError, "AssetManager is null.");
			return false;
		}

		const std::string p = SanitizeFilePath(path);
		if (p.empty())
		{
			setErr(outError, "Path is empty.");
			return false;
		}

		AssetRef<TextureAsset> ref = m_pAM->RegisterAsset<TextureAsset>(p);
		outTex = m_pAM->LoadBlocking(ref, flags);

		if (!outTex)
		{
			setErr(outError, "Failed to load TextureAsset.");
			return false;
		}

		return true;
	}

} // namespace shz
