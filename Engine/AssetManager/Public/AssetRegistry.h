#pragma once
#include <unordered_map>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetManager/Public/AssetID.hpp"
#include "Engine/AssetManager/Public/AssetMeta.h"

namespace shz
{
	class AssetRegistry final
	{
	public:
		void Register(const AssetID& id, const AssetMeta& meta)
		{
			ASSERT(id, "Register: invalid AssetID.");
			ASSERT(meta.TypeID != 0, "Register: invalid TypeID.");
			ASSERT(!meta.SourcePath.empty(), "Register: empty SourcePath.");

			m_Map[id] = meta;
		}

		void Unregister(const AssetID& id)
		{
			ASSERT(id, "Unregister: invalid AssetID.");
			m_Map.erase(id);
		}

		const AssetMeta& Get(const AssetID& id) const
		{
			ASSERT(id, "Get: invalid AssetID.");

			auto it = m_Map.find(id);
			ASSERT(it != m_Map.end(), "Get: asset not registered.");

			return it->second;
		}

		bool Contains(const AssetID& id) const noexcept
		{
			return m_Map.find(id) != m_Map.end();
		}

		void Clear()
		{
			m_Map.clear();
		}

	private:
		std::unordered_map<AssetID, AssetMeta> m_Map = {};
	};
} // namespace shz
