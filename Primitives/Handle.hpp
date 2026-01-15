#pragma once
#include <cstdint>
#include <vector>
#include <shared_mutex>
#include <functional>

#include "Primitives/BasicTypes.h"

namespace shz
{
	template<typename T>
	class Handle final
	{
	public:
		using ValueType = uint64;
		using IndexType = uint32;
		using GenType = uint32;

	public:
		constexpr Handle() noexcept = default;

		static constexpr Handle Invalid() noexcept
		{
			return Handle{};
		}

		// ------------------------------------------------------------
		// Type-local pool API
		// ------------------------------------------------------------
		static Handle Create()
		{
			return GetPool().Create();
		}

		static bool Destroy(Handle h)
		{
			return GetPool().Destroy(h);
		}

		static bool IsAlive(Handle h)
		{
			return GetPool().IsAlive(h);
		}

		static void ResetPool()
		{
			GetPool().Reset();
		}

		// ------------------------------------------------------------
		// Value API
		// ------------------------------------------------------------
		constexpr bool IsValid() const noexcept
		{
			return m_Value != 0;
		}

		constexpr ValueType GetValue() const noexcept
		{
			return m_Value;
		}

		constexpr IndexType GetIndex() const noexcept
		{
			return IsValid() ? UnpackIndex(m_Value) : IndexType{ 0 };
		}

		constexpr GenType GetGeneration() const noexcept
		{
			return IsValid() ? UnpackGen(m_Value) : GenType{ 0 };
		}

		friend constexpr bool operator==(Handle a, Handle b) noexcept { return a.m_Value == b.m_Value; }
		friend constexpr bool operator!=(Handle a, Handle b) noexcept { return a.m_Value != b.m_Value; }

	private:
		// ------------------------------------------------------------
		// Pool (one per Handle<T>)
		// ------------------------------------------------------------
		class Pool final
		{
		public:
			Pool()
			{
				// index 0 reserved for invalid
				m_Generations.push_back(GenType{ 0 });
			}

			Handle Create()
			{
				std::unique_lock lock(m_Mutex);

				IndexType index = 0;

				if (!m_FreeList.empty())
				{
					index = m_FreeList.back();
					m_FreeList.pop_back();
				}
				else
				{
					index = static_cast<IndexType>(m_Generations.size());
					m_Generations.push_back(GenType{ 1 }); // start generation at 1
				}

				const GenType gen = m_Generations[index];
				return Handle{ Pack(index, gen) };
			}

			bool Destroy(Handle h)
			{
				if (!h.IsValid())
				{
					return false;
				}

				std::unique_lock lock(m_Mutex);

				const IndexType index = h.GetIndex();
				if (index == 0 || index >= static_cast<IndexType>(m_Generations.size()))
				{
					return false;
				}

				const GenType gen = h.GetGeneration();
				if (m_Generations[index] != gen)
				{
					return false; // stale handle
				}

				// bump generation (avoid 0)
				GenType next = m_Generations[index] + 1;
				if (next == 0)
				{
					next = 1;
				}
				m_Generations[index] = next;

				m_FreeList.push_back(index);
				return true;
			}

			bool IsAlive(Handle h) const
			{
				if (!h.IsValid())
				{
					return false;
				}

				std::shared_lock lock(m_Mutex);

				const IndexType index = h.GetIndex();
				if (index == 0 || index >= static_cast<IndexType>(m_Generations.size()))
				{
					return false;
				}

				return m_Generations[index] == h.GetGeneration();
			}

			void Reset()
			{
				std::unique_lock lock(m_Mutex);

				m_Generations.clear();
				m_Generations.push_back(GenType{ 0 });

				m_FreeList.clear();
			}

		private:
			std::vector<GenType>   m_Generations;
			std::vector<IndexType> m_FreeList;

			mutable std::shared_mutex m_Mutex;
		};

		static Pool& GetPool()
		{
			// function-local static: init order issues minimized
			static Pool s_Pool;
			return s_Pool;
		}

	private:
		constexpr explicit Handle(ValueType v) noexcept : m_Value(v) {}

		static constexpr ValueType Pack(IndexType index, GenType gen) noexcept
		{
			return (ValueType(gen) << 32) | ValueType(index);
		}

		static constexpr IndexType UnpackIndex(ValueType v) noexcept
		{
			return IndexType(v & 0xFFFFFFFFull);
		}

		static constexpr GenType UnpackGen(ValueType v) noexcept
		{
			return GenType((v >> 32) & 0xFFFFFFFFull);
		}

	private:
		ValueType m_Value = 0;
	};
} // namespace shz

namespace std
{
	template<typename T>
	struct hash<shz::Handle<T>>
	{
		size_t operator()(const shz::Handle<T>& h) const noexcept
		{
			return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(h.GetValue()));
		}
	};
} // namespace std
