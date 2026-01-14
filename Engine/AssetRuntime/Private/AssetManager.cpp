// AssetManager.cpp
#include "pch.h"
#include "AssetManager.h"

#include <algorithm>
#include <cctype>

namespace shz
{
	// ------------------------------------------------------------
	// Small helpers (local)
	// ------------------------------------------------------------
	static inline void ToLowerInPlace(std::string& s)
	{
		for (char& c : s)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	static inline void NormalizeSlashesInPlace(std::string& s)
	{
		for (char& c : s)
		{
			if (c == '\\')
				c = '/';
		}

		// Collapse duplicate slashes: "a//b" -> "a/b"
		std::string out;
		out.reserve(s.size());
		bool prevSlash = false;
		for (char c : s)
		{
			const bool isSlash = (c == '/');
			if (isSlash && prevSlash)
				continue;
			out.push_back(c);
			prevSlash = isSlash;
		}
		s.swap(out);
	}

	static inline void TrimSpacesInPlace(std::string& s)
	{
		auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };

		size_t b = 0;
		while (b < s.size() && isSpace(static_cast<unsigned char>(s[b])))
			++b;

		size_t e = s.size();
		while (e > b && isSpace(static_cast<unsigned char>(s[e - 1])))
			--e;

		s = s.substr(b, e - b);
	}

	// ------------------------------------------------------------
	// AssetManager
	// ------------------------------------------------------------
	std::string AssetManager::MakeKeyFromPath(std::string_view path)
	{
		std::string key(path.begin(), path.end());
		TrimSpacesInPlace(key);
		NormalizeSlashesInPlace(key);

#if defined(_WIN32)
		// On Windows treat paths as case-insensitive for cache key.
		ToLowerInPlace(key);
#endif

		return key;
	}

	// ------------------------------------------------------------
	// Register (by value)
	// ------------------------------------------------------------
	TextureAssetHandle AssetManager::RegisterTexture(const TextureAsset& asset)
	{
		if (!asset.IsValid())
			return {};

		const std::string key = MakeKeyFromPath(asset.GetSourcePath());
		if (!key.empty())
		{
			auto it = m_TextureKeyToHandle.find(key);
			if (it != m_TextureKeyToHandle.end())
				return it->second;
		}

		TextureAssetHandle h = allocateTextureHandle();
		m_Textures.emplace(h, asset);

		if (!key.empty())
			m_TextureKeyToHandle.emplace(key, h);

		return h;
	}

	MaterialAssetHandle AssetManager::RegisterMaterial(const MaterialAsset& asset, uint32 subId) 
	{
		if (!asset.IsValid())
			return {};

		const std::string key = MakeKeyFromPath(asset.GetSourcePath() + std::to_string(subId)); // TODO: 지금은 sub ID 필요. AssetObject마다 Unqique eky를 가지게
		if (!key.empty())
		{
			auto it = m_MaterialKeyToHandle.find(key);
			if (it != m_MaterialKeyToHandle.end())
				return it->second;
		}

		MaterialAssetHandle h = allocateMaterialHandle();
		m_Materials.emplace(h, asset);

		if (!key.empty())
			m_MaterialKeyToHandle.emplace(key, h);

		return h;
	}

	StaticMeshAssetHandle AssetManager::RegisterStaticMesh(const StaticMeshAsset& asset)
	{
		if (!asset.IsValid())
			return {};

		const std::string key = MakeKeyFromPath(asset.GetSourcePath());
		if (!key.empty())
		{
			auto it = m_StaticMeshKeyToHandle.find(key);
			if (it != m_StaticMeshKeyToHandle.end())
				return it->second;
		}

		StaticMeshAssetHandle h = allocateStaticMeshHandle();
		m_StaticMeshes.emplace(h, asset);

		if (!key.empty())
			m_StaticMeshKeyToHandle.emplace(key, h);

		return h;
	}

	// ------------------------------------------------------------
	// Get (by handle) - assert policy (your style)
	// ------------------------------------------------------------
	const TextureAsset& AssetManager::GetTexture(TextureAssetHandle h) const noexcept
	{
		ASSERT(h.IsValid() && m_Textures.find(h) != m_Textures.end(), "Invalid TextureAssetHandle.");
		return m_Textures.at(h);
	}

	const MaterialAsset& AssetManager::GetMaterial(MaterialAssetHandle h) const noexcept
	{
		ASSERT(h.IsValid() && m_Materials.find(h) != m_Materials.end(), "Invalid MaterialAssetHandle.");
		return m_Materials.at(h);
	}

	const StaticMeshAsset& AssetManager::GetStaticMesh(StaticMeshAssetHandle h) const noexcept
	{
		ASSERT(h.IsValid() && m_StaticMeshes.find(h) != m_StaticMeshes.end(), "Invalid StaticMeshAssetHandle.");
		return m_StaticMeshes.at(h);
	}

	// ------------------------------------------------------------
	// Find by key
	// ------------------------------------------------------------
	TextureAssetHandle AssetManager::FindTextureByKey(std::string_view key) const noexcept
	{
		if (key.empty())
			return {};

		std::string k = MakeKeyFromPath(key);
		auto it = m_TextureKeyToHandle.find(k);
		return (it != m_TextureKeyToHandle.end()) ? it->second : TextureAssetHandle{};
	}

	MaterialAssetHandle AssetManager::FindMaterialByKey(std::string_view key) const noexcept
	{
		if (key.empty())
			return {};

		std::string k = MakeKeyFromPath(key);
		auto it = m_MaterialKeyToHandle.find(k);
		return (it != m_MaterialKeyToHandle.end()) ? it->second : MaterialAssetHandle{};
	}

	StaticMeshAssetHandle AssetManager::FindStaticMeshByKey(std::string_view key) const noexcept
	{
		if (key.empty())
			return {};

		std::string k = MakeKeyFromPath(key);
		auto it = m_StaticMeshKeyToHandle.find(k);
		return (it != m_StaticMeshKeyToHandle.end()) ? it->second : StaticMeshAssetHandle{};
	}

	// ------------------------------------------------------------
	// Remove
	// ------------------------------------------------------------
	bool AssetManager::RemoveTexture(TextureAssetHandle h)
	{
		if (!h.IsValid())
			return false;

		auto it = m_Textures.find(h);
		if (it == m_Textures.end())
			return false;

		const std::string key = MakeKeyFromPath(it->second.GetSourcePath());
		if (!key.empty())
		{
			auto kit = m_TextureKeyToHandle.find(key);
			if (kit != m_TextureKeyToHandle.end() && kit->second == h)
				m_TextureKeyToHandle.erase(kit);
		}

		m_Textures.erase(it);
		return true;
	}

	bool AssetManager::RemoveMaterial(MaterialAssetHandle h)
	{
		if (!h.IsValid())
			return false;

		auto it = m_Materials.find(h);
		if (it == m_Materials.end())
			return false;

		const std::string key = MakeKeyFromPath(it->second.GetSourcePath());
		if (!key.empty())
		{
			auto kit = m_MaterialKeyToHandle.find(key);
			if (kit != m_MaterialKeyToHandle.end() && kit->second == h)
				m_MaterialKeyToHandle.erase(kit);
		}

		m_Materials.erase(it);
		return true;
	}

	bool AssetManager::RemoveStaticMesh(StaticMeshAssetHandle h)
	{
		if (!h.IsValid())
			return false;

		auto it = m_StaticMeshes.find(h);
		if (it == m_StaticMeshes.end())
			return false;

		const std::string key = MakeKeyFromPath(it->second.GetSourcePath());
		if (!key.empty())
		{
			auto kit = m_StaticMeshKeyToHandle.find(key);
			if (kit != m_StaticMeshKeyToHandle.end() && kit->second == h)
				m_StaticMeshKeyToHandle.erase(kit);
		}

		m_StaticMeshes.erase(it);
		return true;
	}

	// ------------------------------------------------------------
	// Clear
	// ------------------------------------------------------------
	void AssetManager::Clear()
	{
		m_Textures.clear();
		m_Materials.clear();
		m_StaticMeshes.clear();

		m_TextureKeyToHandle.clear();
		m_MaterialKeyToHandle.clear();
		m_StaticMeshKeyToHandle.clear();

		m_NextTextureId = 1;
		m_NextMaterialId = 1;
		m_NextStaticMeshId = 1;
	}

} // namespace shz
