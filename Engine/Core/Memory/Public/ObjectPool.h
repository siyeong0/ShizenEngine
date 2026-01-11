#pragma once
#include "Primitives/Common.h"
#include "Engine/Core/Memory/Public/PagedMemoryPool.h"

namespace shz
{
    // ------------------------------------------------------------
    // ObjectPool<T>
    // - Uses PagedBlockPool as backing storage.
    // - O(1) Create/Destroy (plus ctor/dtor cost).
    // Notes:
    // - In debug, pool memory is pattern-filled by PagedBlockPool.
    // - If exceptions are enabled and T ctor throws, slot is returned.
    // ------------------------------------------------------------

    template<typename T>
    class ObjectPool
    {
    public:
        ObjectPool() = default;
        ~ObjectPool() { Cleanup(); }

        ObjectPool(const ObjectPool&) = delete;
        ObjectPool& operator=(const ObjectPool&) = delete;

        bool Initialize(uint32 objectsPerPage)
        {
            // element size/alignment based on T
            return m_Pool.Initialize(sizeof(T), alignof(T), objectsPerPage);
        }

        void Cleanup()
        {
            m_Pool.Cleanup();
        }

        template<typename... Args>
        T* Create(Args&&... args)
        {
            void* mem = m_Pool.Alloc();
            ASSERT(mem != nullptr, "ObjectPool::Create failed to allocate.");

#if defined(__cpp_exceptions)
            try
            {
                return new (mem) T(std::forward<Args>(args)...);
            }
            catch (...)
            {
                m_Pool.Free(mem);
                throw;
            }
#else
            return new (mem) T(std::forward<Args>(args)...);
#endif
        }

        void Destroy(T* obj)
        {
            ASSERT(obj != nullptr, "ObjectPool::Destroy called with nullptr.");
            if (!obj) return;

            obj->~T();
            m_Pool.Free(obj);
        }

        bool Owns(const T* obj) const
        {
            return m_Pool.Owns(obj);
        }

        uint32 GetLiveCount() const { return static_cast<uint32>(m_Pool.GetLiveCount()); }

    private:
        PagedMemoryPool m_Pool;
    };
} // namespace shz