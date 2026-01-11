#include "pch.h"
#include "Engine/Core/Memory/Public/PagedMemoryPool.h"

namespace shz
{
    PagedMemoryPool::PagedMemoryPool(PagedMemoryPool&& rhs) noexcept
    {
        *this = std::move(rhs);
    }

    PagedMemoryPool& PagedMemoryPool::operator=(PagedMemoryPool&& rhs) noexcept
    {
        if (this == &rhs)
        {
            return *this;
        }

        Cleanup();

        m_ElementByteSize = rhs.m_ElementByteSize;
        m_Alignment = rhs.m_Alignment;
        m_ElementsPerPage = rhs.m_ElementsPerPage;
        m_HeaderSize = rhs.m_HeaderSize;
        m_SlotStride = rhs.m_SlotStride;
        m_PageBytes = rhs.m_PageBytes;

        m_PageHead = rhs.m_PageHead;
        m_PageCount = rhs.m_PageCount;
        m_FreeList = rhs.m_FreeList;
        m_LiveCount = rhs.m_LiveCount;

        rhs.m_PageHead = nullptr;
        rhs.m_PageCount = 0;
        rhs.m_FreeList = nullptr;
        rhs.m_LiveCount = 0;

        rhs.m_ElementByteSize = 0;
        rhs.m_Alignment = 0;
        rhs.m_ElementsPerPage = 0;
        rhs.m_HeaderSize = 0;
        rhs.m_SlotStride = 0;
        rhs.m_PageBytes = 0;

        return *this;
    }

    bool PagedMemoryPool::Initialize(size_t elementByteSize, size_t alignment, uint32 elementsPerPage)
    {
        ASSERT(m_PageHead == nullptr && m_FreeList == nullptr, "PagedBlockPool already initialized.");
        ASSERT(elementByteSize > 0, "Element size must be > 0.");
        ASSERT(alignment > 0, "Alignment must be > 0.");
        ASSERT(elementsPerPage > 0, "ElementsPerPage must be > 0.");

        m_ElementByteSize = elementByteSize;
        m_Alignment = alignment;
        m_ElementsPerPage = elementsPerPage;

        // Header is aligned so that payload can be aligned.
        m_HeaderSize = alignUp(sizeof(SlotHeader), m_Alignment);

        // IMPORTANT FIX:
        // payload must be aligned in stride, otherwise next slot may break alignment.
        const size_t payloadAligned = alignUp(m_ElementByteSize, m_Alignment);
        m_SlotStride = m_HeaderSize + payloadAligned;

        ASSERT((m_SlotStride % m_Alignment) == 0, "Slot stride must be multiple of alignment.");

        m_PageBytes = m_SlotStride * static_cast<size_t>(m_ElementsPerPage);

        // Create first page eagerly.
        return allocateNewPage();
    }

    void PagedMemoryPool::Cleanup()
    {
        // Optional leak check
        // ASSERT(m_LiveCount == 0, "PagedBlockPool leak detected (liveCount != 0).");

        freeAllPages();

        m_ElementByteSize = 0;
        m_Alignment = 0;
        m_ElementsPerPage = 0;
        m_HeaderSize = 0;
        m_SlotStride = 0;
        m_PageBytes = 0;
        m_FreeList = nullptr;
        m_PageCount = 0;
        m_LiveCount = 0;
    }

    void* PagedMemoryPool::Alloc()
    {
        if (!m_FreeList)
        {
            const bool ok = allocateNewPage();
            ASSERT(ok, "PagedBlockPool failed to allocate a new page.");
            if (!ok)
            {
                ASSERT(false, "PagedBlockPool out of memory.");
                return nullptr;
            }
        }

        SlotHeader* h = m_FreeList;
        m_FreeList = h->NextFree;

#if ENGINE_MEMPOOL_DEBUG
        ASSERT(h->Magic == MAGIC_FREE, "Alloc detected corrupted or double-allocated slot.");
        h->Magic = MAGIC_ALLOC;
#endif

        ++m_LiveCount;

        void* payload = reinterpret_cast<void*>(reinterpret_cast<byte*>(h) + m_HeaderSize);

#if ENGINE_MEMPOOL_DEBUG
        // Fill with known pattern for debugging.
        std::memset(payload, 0xCD, m_ElementByteSize);
#endif

        return payload;
    }

    void PagedMemoryPool::Free(void* ptr)
    {
        ASSERT(ptr != nullptr, "PagedBlockPool::Free called with nullptr.");
        if (!ptr)
        {
            return;
        }

        SlotHeader* h = reinterpret_cast<SlotHeader*>(reinterpret_cast<byte*>(ptr) - m_HeaderSize);

#if ENGINE_MEMPOOL_DEBUG
        ASSERT(h->Magic == MAGIC_ALLOC, "Free detected double free or memory corruption.");
        h->Magic = MAGIC_FREE;

        // Overwrite payload with freed pattern.
        std::memset(ptr, 0xDD, m_ElementByteSize);
#endif

        // O(1): no page search needed.
        ASSERT(h->OwnerPage != nullptr, "Slot header has no owner page. Corruption?");
        h->NextFree = m_FreeList;
        m_FreeList = h;

        ASSERT(m_LiveCount > 0, "LiveCount underflow. Double free?");
        --m_LiveCount;
    }

    bool PagedMemoryPool::Owns(const void* ptr) const
    {
        if (!ptr)
        {
            return false;
        }

        // We can do a safe-ish check via header owner page,
        // but pointer must be at least m_HeaderSize bytes after a valid header.
        // If ptr is random, PayloadToHeader might still be invalid.
        // For safety, we do a conservative linear check in debug only.
#if ENGINE_MEMPOOL_DEBUG
        for (Page* p = m_PageHead; p; p = p->Next)
        {
            const byte* begin = p->Begin();
            const byte* end = p->End(m_PageBytes);
            const byte* u = reinterpret_cast<const byte*>(ptr);

            if (u >= begin + m_HeaderSize && u < end)
            {
                // Also verify alignment to slot boundary.
                const size_t offset = static_cast<size_t>(u - begin);
                const size_t slotOffset = offset - m_HeaderSize;
                if ((slotOffset % m_SlotStride) == 0) return true;
            }
        }
        return false;
#else
    // In release, assume caller uses it correctly.
    // If you want strict Owns() in release, keep the linear scan.
        return true;
#endif
    }

    bool PagedMemoryPool::allocateNewPage()
    {
        // Allocate Page node
        Page* pageNode = new Page();
        pageNode->SlotCount = m_ElementsPerPage;

        // Allocate aligned memory block for the page
        // Using aligned operator new (C++17+).
        void* mem = ::operator new(m_PageBytes, std::align_val_t(m_Alignment));
        pageNode->Buffer = reinterpret_cast<byte*>(mem);

        // Link page
        pageNode->Next = m_PageHead;
        m_PageHead = pageNode;
        ++m_PageCount;

        // Build free list for new page
        byte* cursor = pageNode->Buffer;
        for (uint32 i = 0; i < m_ElementsPerPage; ++i)
        {
            SlotHeader* h = reinterpret_cast<SlotHeader*>(cursor);
            h->OwnerPage = pageNode;

#if ENGINE_MEMPOOL_DEBUG
            h->Magic = MAGIC_FREE;
            h->Reserved = 0;
#endif

            h->NextFree = m_FreeList;
            m_FreeList = h;

            cursor += m_SlotStride;
        }

        return true;
    }

    void PagedMemoryPool::freeAllPages()
    {
        // Free pages
        Page* p = m_PageHead;
        while (p)
        {
            Page* next = p->Next;

            if (p->Buffer)
            {
                ::operator delete(p->Buffer, std::align_val_t(m_Alignment));
                p->Buffer = nullptr;
            }

            delete p;
            p = next;
        }

        m_PageHead = nullptr;
        m_FreeList = nullptr;
    }
} // namespace shz