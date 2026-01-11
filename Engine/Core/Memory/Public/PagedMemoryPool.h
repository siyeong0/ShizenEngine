#pragma once
#include "Primitives/Common.h"
#include "Primitives/BasicTypes.h"

namespace shz
{
	// ------------------------------------------------------------
	// PagedBlockPool
	// - Fixed-size block allocator (pool) with page growth.
	// - O(1) Alloc / Free using an intrusive free-list.
	// - Each allocation returns a pointer aligned to m_Alignment.
	// - No per-block size tracking (all blocks same size).
	//
	// Debug features (optional):
	//   - Double free detection via magic values
	//   - Pattern fill on alloc/free
	// Enable by defining ENGINE_MEMPOOL_DEBUG = 1
	// ------------------------------------------------------------

#ifndef ENGINE_MEMPOOL_DEBUG
#define ENGINE_MEMPOOL_DEBUG 0
#endif

	class PagedMemoryPool
	{
	public:
		PagedMemoryPool() = default;
		~PagedMemoryPool() { Cleanup(); }

		// Non-copyable
		PagedMemoryPool(const PagedMemoryPool&) = delete;
		PagedMemoryPool& operator=(const PagedMemoryPool&) = delete;

		// Movable (optional)
		PagedMemoryPool(PagedMemoryPool&& rhs) noexcept;
		PagedMemoryPool& operator=(PagedMemoryPool&& rhs) noexcept;

		// --------------------------------------------------------
		// Initialize
		// elementByteSize : payload size returned by Alloc()
		// alignment       : payload alignment (power-of-two recommended)
		// elementsPerPage : number of slots per page
		// --------------------------------------------------------
		bool Initialize(size_t elementByteSize, size_t alignment, uint32 elementsPerPage);
		void Cleanup();

		void* Alloc();
		void  Free(void* ptr);

		// Debug / stats
		bool Owns(const void* ptr) const;
		size_t GetElementSize() const { return m_ElementByteSize; }
		size_t GetAlignment()   const { return m_Alignment; }
		uint32 GetElementsPerPage() const { return m_ElementsPerPage; }
		uint32 GetPageCount() const { return m_PageCount; }
		uint32 GetLiveCount() const { return m_LiveCount; }

	private:
		struct Page;

		// Slot header lives in front of payload.
		struct alignas(16) SlotHeader
		{
			// Owner page pointer enables O(1) Free/Owns without searching pages.
			Page* OwnerPage;

			// Intrusive free list pointer (valid only when slot is FREE).
			SlotHeader* NextFree;

#if ENGINE_MEMPOOL_DEBUG
			uint32 Magic;
			uint32 Reserved;
#endif
		};

		struct Page
		{
			uint8* Buffer = nullptr;   // raw page memory
			uint32 SlotCount = 0;
			Page* Next = nullptr;

			// Optional: for fast range checks / debugging
			uint8* Begin() const { return Buffer; }
			uint8* End(size_t pageBytes) const { return Buffer + pageBytes; }
		};

	private:
		static inline size_t alignUp(size_t value, size_t alignment)
		{
			// Works for any alignment > 0 (not necessarily power-of-two).
			ASSERT(alignment > 0, "Alignment must be > 0");
			const size_t mod = value % alignment;
			return (mod == 0) ? value : (value + (alignment - mod));
		}

		bool allocateNewPage();
		void freeAllPages();

	private:
		// Config
		size_t m_ElementByteSize = 0;
		size_t m_Alignment = 0;
		uint32 m_ElementsPerPage = 0;

		// Derived
		size_t m_HeaderSize = 0;     // aligned header size
		size_t m_SlotStride = 0;     // aligned stride between slots (header + payloadAligned)
		size_t m_PageBytes = 0;

		// State
		Page* m_PageHead = nullptr;
		uint32 m_PageCount = 0;

		SlotHeader* m_FreeList = nullptr;
		uint32 m_LiveCount = 0;

#if ENGINE_MEMPOOL_DEBUG
		static constexpr uint32 MAGIC_FREE = 0xDEADF00D;
		static constexpr uint32 MAGIC_ALLOC = 0xC0FFEE01;
#endif
	};
} // namespace shz