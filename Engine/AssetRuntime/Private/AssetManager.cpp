#include "pch.h"
#include "AssetManager.h"

#include <algorithm>
#include <cctype>

namespace shz
{
	// ------------------------------------------------------------
	// Register (by value)
	// ------------------------------------------------------------
	TextureAssetHandle AssetManager::RegisterTexture(const TextureAsset& asset)
	{
		if (!asset.IsValid())
			return {};

		AssetId id = asset.GetId();
		auto it = m_TextureKeyToHandle.find(id);
		if (it != m_TextureKeyToHandle.end())
			return it->second;

		TextureAssetHandle h = allocateTextureHandle();
		m_Textures.emplace(h, asset);

		m_TextureKeyToHandle.emplace(id, h);

		return h;
	}

	MaterialAssetHandle AssetManager::RegisterMaterial(const MaterialAsset& asset)
	{
		if (!asset.IsValid())
			return {};

		AssetId id = asset.GetId();
		auto it = m_MaterialKeyToHandle.find(id);
		if (it != m_MaterialKeyToHandle.end())
			return it->second;

		MaterialAssetHandle h = allocateMaterialHandle();
		m_Materials.emplace(h, asset);

		m_MaterialKeyToHandle.emplace(id, h);

		return h;
	}

	StaticMeshAssetHandle AssetManager::RegisterStaticMesh(const StaticMeshAsset& asset)
	{
		if (!asset.IsValid())
			return {};

		AssetId id = asset.GetId();
		auto it = m_StaticMeshKeyToHandle.find(id);
		if (it != m_StaticMeshKeyToHandle.end())
			return it->second;

		StaticMeshAssetHandle h = allocateStaticMeshHandle();
		m_StaticMeshes.emplace(h, asset);

		m_StaticMeshKeyToHandle.emplace(id, h);

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
	TextureAssetHandle AssetManager::FindTextureById(const AssetId& id) const noexcept
	{
		auto it = m_TextureKeyToHandle.find(id);
		return (it != m_TextureKeyToHandle.end()) ? it->second : TextureAssetHandle{};
	}

	MaterialAssetHandle AssetManager::FindMaterialById(const AssetId& id) const noexcept
	{
		auto it = m_MaterialKeyToHandle.find(id);
		return (it != m_MaterialKeyToHandle.end()) ? it->second : MaterialAssetHandle{};
	}

	StaticMeshAssetHandle AssetManager::FindStaticMeshById(const AssetId& id) const noexcept
	{
		auto it = m_StaticMeshKeyToHandle.find(id);
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

		AssetId id = it->second.GetId();
		auto kit = m_TextureKeyToHandle.find(id);
		if (kit != m_TextureKeyToHandle.end() && kit->second == h)
			m_TextureKeyToHandle.erase(kit);

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

		AssetId id = it->second.GetId();
		auto kit = m_MaterialKeyToHandle.find(id);
		if (kit != m_MaterialKeyToHandle.end() && kit->second == h)
			m_MaterialKeyToHandle.erase(kit);

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

		AssetId id = it->second.GetId();
		auto kit = m_StaticMeshKeyToHandle.find(id);
		if (kit != m_StaticMeshKeyToHandle.end() && kit->second == h)
			m_StaticMeshKeyToHandle.erase(kit);

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
