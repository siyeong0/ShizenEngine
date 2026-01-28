#pragma once
#include <unordered_map>
#include <mutex>

#include "Primitives/BasicTypes.h"

namespace shz
{
	template<class TRenderData>
	class RenderResourceCache final
	{
	public:
		using KeyType = uint64;

		RenderResourceCache() = default;
		RenderResourceCache(const RenderResourceCache&) = delete;
		RenderResourceCache& operator=(const RenderResourceCache&) = delete;
		~RenderResourceCache() = default;

		void Clear()
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Table.clear();
		}

		// ------------------------------------------------------------
		// Acquire
		// ------------------------------------------------------------
		const TRenderData* Acquire(KeyType key) const noexcept
		{
			if (key == 0)
				return nullptr;

			std::lock_guard<std::mutex> lock(m_Mutex);

			auto it = m_Table.find(key);
			if (it == m_Table.end())
				return nullptr;

			return &it->second;
		}

		TRenderData* Acquire(KeyType key) noexcept
		{
			if (key == 0)
				return nullptr;

			std::lock_guard<std::mutex> lock(m_Mutex);

			auto it = m_Table.find(key);
			if (it == m_Table.end())
				return nullptr;

			return &it->second;
		}

		// ------------------------------------------------------------
		// Store
		// ------------------------------------------------------------
		// Returns true if inserted new, false if replaced existing.
		bool Store(KeyType key, const TRenderData&& rd)
		{
			if (key == 0)
				return false;

			std::lock_guard<std::mutex> lock(m_Mutex);

			auto it = m_Table.find(key);
			if (it == m_Table.end())
			{
				m_Table.emplace(key, rd);
				return true;
			}

			it->second = std::move(rd);
			return false;
		}

		bool Store(KeyType key, TRenderData&& rd)
		{
			if (key == 0)
				return false;

			std::lock_guard<std::mutex> lock(m_Mutex);

			auto it = m_Table.find(key);
			if (it == m_Table.end())
			{
				m_Table.emplace(key, std::move(rd));
				return true;
			}

			it->second = std::move(rd);
			return false;
		}

		// ------------------------------------------------------------
		// Erase / Query
		// ------------------------------------------------------------
		bool Erase(KeyType key)
		{
			if (key == 0)
				return false;

			std::lock_guard<std::mutex> lock(m_Mutex);
			return (m_Table.erase(key) > 0);
		}

		bool Contains(KeyType key) const
		{
			if (key == 0)
				return false;

			std::lock_guard<std::mutex> lock(m_Mutex);
			return (m_Table.find(key) != m_Table.end());
		}

		uint32 Size() const
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			return static_cast<uint32>(m_Table.size());
		}

	private:
		mutable std::mutex m_Mutex;
		std::unordered_map<KeyType, TRenderData> m_Table;
	};

} // namespace shz
