#pragma once
#include <string>
#include <unordered_map>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Public/AssetID.hpp"

namespace shz
{
	// ------------------------------------------------------------
	// AssetRegistry
	// - Minimal in-memory mapping: AssetID -> (TypeID, SourcePath)
	// - Assumption:
	//   - Runtime registry is valid and complete.
	//   - Missing entries / type mismatch are programmer error => ASSERT.
	// ------------------------------------------------------------
	class AssetRegistry final
	{
	public:
		struct AssetMeta final
		{
			AssetTypeID TypeID = 0;
			std::string SourcePath = {};
		};

	public:
		void Register(const AssetID& id, const AssetMeta& meta)
		{
			ASSERT(id, "AssetRegistry::Register: invalid AssetID.");
			ASSERT(meta.TypeID != 0, "AssetRegistry::Register: invalid TypeID.");
			ASSERT(!meta.SourcePath.empty(), "AssetRegistry::Register: empty SourcePath.");

			m_Map[id] = meta;
		}

		void Unregister(const AssetID& id)
		{
			ASSERT(id, "AssetRegistry::Unregister: invalid AssetID.");
			m_Map.erase(id);
		}

		const AssetMeta& Get(const AssetID& id) const
		{
			ASSERT(id, "AssetRegistry::Get: invalid AssetID.");

			auto it = m_Map.find(id);
			ASSERT(it != m_Map.end(), "AssetRegistry::Get: asset not registered.");

			return it->second;
		}

		void Clear()
		{
			m_Map.clear();
		}

	private:
		std::unordered_map<AssetID, AssetMeta> m_Map = {};
	};

} // namespace shz
