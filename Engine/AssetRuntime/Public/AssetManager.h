// AssetManager.h
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>

#include "Primitives/BasicTypes.h"

#include "Engine/AssetRuntime/Public/AssetHandles.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"
#include "Engine/AssetRuntime/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"

namespace shz
{
	// ------------------------------------------------------------
	// AssetManager
	// - CPU-side registry/cache for assets (no GPU/RHI dependency).
	// - Responsibilities (MVP):
	//   * Register assets (by path or by value)
	//   * Deduplicate by "key" (default: normalized path)
	//   * Provide stable AssetHandles
	//   * Query assets back by handle
	//
	// Notes:
	// - This does NOT load GPU resources.
	// - You can later expand:
	//   * async loading, hot-reload, cooked packages, GUIDs, etc.
	// ------------------------------------------------------------
	class AssetManager final
	{
	public:
		AssetManager() = default;
		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;
		~AssetManager() = default;

		// --------------------------------------------------------
		// Register by value
		// - Copies the asset into the registry.
		// - If "key" already exists, returns existing handle.
		// --------------------------------------------------------
		TextureAssetHandle    RegisterTexture(const TextureAsset& asset);
		MaterialAssetHandle   RegisterMaterial(const MaterialAsset& asset, uint32 subId);
		StaticMeshAssetHandle RegisterStaticMesh(const StaticMeshAsset& asset);

		// --------------------------------------------------------
		// Lookup by handle
		// - Returns nullptr if handle invalid or not found.
		//
		// IMPORTANT:
		// - You currently return references and ASSERT on invalid.
		// - Keep that policy for now (matches your engine style).
		// --------------------------------------------------------
		const TextureAsset& GetTexture(TextureAssetHandle h) const noexcept;
		const MaterialAsset& GetMaterial(MaterialAssetHandle h) const noexcept;
		const StaticMeshAsset& GetStaticMesh(StaticMeshAssetHandle h) const noexcept;

		// --------------------------------------------------------
		// Lookup handle by key/path
		// --------------------------------------------------------
		TextureAssetHandle     FindTextureByKey(std::string_view key) const noexcept;
		MaterialAssetHandle    FindMaterialByKey(std::string_view key) const noexcept;
		StaticMeshAssetHandle  FindStaticMeshByKey(std::string_view key) const noexcept;

		// --------------------------------------------------------
		// Remove / Clear
		// --------------------------------------------------------
		bool RemoveTexture(TextureAssetHandle h);
		bool RemoveMaterial(MaterialAssetHandle h);
		bool RemoveStaticMesh(StaticMeshAssetHandle h);

		void Clear();

		// --------------------------------------------------------
		// Stats
		// --------------------------------------------------------
		uint32 GetTextureCount() const noexcept { return static_cast<uint32>(m_Textures.size()); }
		uint32 GetMaterialCount() const noexcept { return static_cast<uint32>(m_Materials.size()); }
		uint32 GetStaticMeshCount() const noexcept { return static_cast<uint32>(m_StaticMeshes.size()); }

		// --------------------------------------------------------
		// Key policy
		// - You can replace this later with GUID or hashed import key.
		// --------------------------------------------------------
		static std::string MakeKeyFromPath(std::string_view path);

	private:
		TextureAssetHandle    allocateTextureHandle() noexcept { return TextureAssetHandle{ m_NextTextureId++ }; }
		MaterialAssetHandle   allocateMaterialHandle() noexcept { return MaterialAssetHandle{ m_NextMaterialId++ }; }
		StaticMeshAssetHandle allocateStaticMeshHandle() noexcept { return StaticMeshAssetHandle{ m_NextStaticMeshId++ }; }

	private:
		// Handle -> asset (registry storage)
		std::unordered_map<TextureAssetHandle, TextureAsset>    m_Textures;
		std::unordered_map<MaterialAssetHandle, MaterialAsset>   m_Materials;
		std::unordered_map<StaticMeshAssetHandle, StaticMeshAsset> m_StaticMeshes;

		// Key -> handle (dedup / lookup)
		std::unordered_map<std::string, TextureAssetHandle>    m_TextureKeyToHandle;
		std::unordered_map<std::string, MaterialAssetHandle>   m_MaterialKeyToHandle;
		std::unordered_map<std::string, StaticMeshAssetHandle> m_StaticMeshKeyToHandle;

		uint32 m_NextTextureId = 1;
		uint32 m_NextMaterialId = 1;
		uint32 m_NextStaticMeshId = 1;
	};

} // namespace shz
