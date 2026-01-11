#include "pch.h"
#include "Memory/Public/ArenaAllocator.h"

namespace shz
{
	ArenaAllocator::~ArenaAllocator()
	{
		Shutdown();
	}

	ArenaAllocator::ArenaAllocator(ArenaAllocator&& other) noexcept
	{
		*this = std::move(other);
	}

	ArenaAllocator& ArenaAllocator::operator=(ArenaAllocator&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		Shutdown();

		m_Mode = other.m_Mode;

		m_Base = other.m_Base;
		m_Capacity = other.m_Capacity;
		m_Offset = other.m_Offset;

		m_Head = other.m_Head;
		m_Tail = other.m_Tail;
		m_FirstChunkBytes = other.m_FirstChunkBytes;
		m_NextChunkBytes = other.m_NextChunkBytes;

		other.m_Mode = eMode::Uninitialized;
		other.m_Base = nullptr;
		other.m_Capacity = 0;
		other.m_Offset = 0;
		other.m_Head = nullptr;
		other.m_Tail = nullptr;
		other.m_FirstChunkBytes = 0;
		other.m_NextChunkBytes = 0;

		return *this;
	}

	void ArenaAllocator::Initialize(void* buffer, size_t bytes)
	{
		Shutdown();

		ASSERT(buffer != nullptr, "ArenaAllocator::Initialize: buffer is null");
		ASSERT(bytes > 0, "ArenaAllocator::Initialize: bytes is zero");

		m_Mode = eMode::FixedBuffer;
		m_Base = reinterpret_cast<byte*>(buffer);
		m_Capacity = bytes;
		m_Offset = 0;
	}

	void ArenaAllocator::InitializeGrowable(size_t firstChunkBytes, size_t nextChunkBytes)
	{
		Shutdown();

		ASSERT(firstChunkBytes > 0, "ArenaAllocator::InitializeGrowable: firstChunkBytes is zero");
		if (nextChunkBytes == 0)
		{
			nextChunkBytes = firstChunkBytes;
		}

		m_Mode = eMode::Growable;
		m_FirstChunkBytes = firstChunkBytes;
		m_NextChunkBytes = nextChunkBytes;

		m_Head = allocateChunk(firstChunkBytes);
		m_Tail = m_Head;
	}

	void ArenaAllocator::Shutdown()
	{
		if (m_Mode == eMode::Growable)
		{
			Chunk* c = m_Head;
			while (c)
			{
				Chunk* next = c->Next;
				AlignedFree(c);
				c = next;
			}
		}

		m_Mode = eMode::Uninitialized;

		m_Base = nullptr;
		m_Capacity = 0;
		m_Offset = 0;

		m_Head = nullptr;
		m_Tail = nullptr;
		m_FirstChunkBytes = 0;
		m_NextChunkBytes = 0;
	}

	void* ArenaAllocator::Allocate(size_t bytes, size_t alignment)
	{
		ASSERT(m_Mode != eMode::Uninitialized, "ArenaAllocator::Allocate: allocator is uninitialized");
		ASSERT(isPowerOfTwo(alignment), "ArenaAllocator::Allocate: alignment is not power-of-two");
		if (bytes == 0) bytes = 1;

		if (m_Mode == eMode::FixedBuffer)
		{
			return allocateFixed(bytes, alignment);
		}
		else
		{
			return allocateGrowable(bytes, alignment);
		}
	}

	void* ArenaAllocator::AllocateZero(size_t bytes, size_t alignment)
	{
		void* p = Allocate(bytes, alignment);
		if (p)
		{
			std::memset(p, 0, bytes);
		}
		return p;
	}

	void* ArenaAllocator::allocateFixed(size_t bytes, size_t alignment)
	{
		const size_t aligned = alignUp(m_Offset, alignment);
		if (aligned + bytes > m_Capacity)
		{
			return nullptr;
		}

		void* p = m_Base + aligned;
		m_Offset = aligned + bytes;
		return p;
	}

	ArenaAllocator::Chunk* ArenaAllocator::allocateChunk(size_t payloadBytes)
	{
		constexpr size_t kChunkAlignment = 64;

		const size_t headerSize = sizeof(Chunk);

		// We want BeginAddress to be kChunkAlignment-aligned.
		// Allocate extra slack so we can align (raw + header) up to kChunkAlignment.
		const size_t totalBytes = headerSize + payloadBytes + (kChunkAlignment - 1);

		void* raw = AlignedAlloc(kChunkAlignment, totalBytes);
		ASSERT(raw != nullptr, "ArenaAllocator::allocateChunk: allocation failed");

		Chunk* c = reinterpret_cast<Chunk*>(raw);

		byte* rawDataBegin = reinterpret_cast<byte*>(raw) + headerSize;
		byte* alignedDataBegin = reinterpret_cast<byte*>((reinterpret_cast<uintptr_t>(rawDataBegin) + (kChunkAlignment - 1)) & ~(uintptr_t)(kChunkAlignment - 1));

		c->Next = nullptr;
		c->Capacity = payloadBytes;   // payload capacity (not counting header/slack)
		c->Offset = 0;
		c->BeginAddress = alignedDataBegin;

		return c;
	}


	void* ArenaAllocator::allocateGrowable(size_t bytes, size_t alignment)
	{
		ASSERT(m_Tail != nullptr, "ArenaAllocator::AllocateGrowable: m_Tail is null");

		// Try current tail
		{
			const size_t aligned = alignUp(m_Tail->Offset, alignment);
			if (aligned + bytes <= m_Tail->Capacity)
			{
				void* p = m_Tail->BeginAddress + aligned;
				m_Tail->Offset = aligned + bytes;
				return p;
			}
		}

		// Need a new chunk
		const size_t minChunk = bytes + alignment; // slack for alignment
		size_t newChunkBytes = m_NextChunkBytes;
		if (newChunkBytes < minChunk)
			newChunkBytes = minChunk;

		Chunk* c = allocateChunk(newChunkBytes);
		m_Tail->Next = c;
		m_Tail = c;

		const size_t aligned = alignUp(m_Tail->Offset, alignment);
		void* p = m_Tail->BeginAddress + aligned; // <-- use BeginAddress
		m_Tail->Offset = aligned + bytes;
		return p;
	}


	void ArenaAllocator::freeAllChunksExceptFirst()
	{
		if (!m_Head)
		{
			return;
		}

		Chunk* first = m_Head;
		Chunk* c = first->Next;
		first->Next = nullptr;

		while (c)
		{
			Chunk* next = c->Next;
			AlignedFree(c);
			c = next;
		}

		m_Tail = first;
	}

	void ArenaAllocator::Reset()
	{
		ASSERT(m_Mode != eMode::Uninitialized, "ArenaAllocator::Reset: allocator is uninitialized");

		if (m_Mode == eMode::FixedBuffer)
		{
			m_Offset = 0;
		}
		else
		{
			// Keep first chunk allocated (good for frame alloc style reuse).
			freeAllChunksExceptFirst();
			m_Head->Offset = 0;
		}
	}

	ArenaAllocator::Marker ArenaAllocator::Save() const
	{
		ASSERT(m_Mode != eMode::Uninitialized, "ArenaAllocator::Save: allocator is uninitialized");

		Marker m{};
		if (m_Mode == eMode::FixedBuffer)
		{
			m.Chunk = nullptr;
			m.Offset = m_Offset;
		}
		else
		{
			m.Chunk = m_Tail;
			m.Offset = m_Tail ? m_Tail->Offset : 0;
		}
		return m;
	}

	void ArenaAllocator::Restore(Marker marker)
	{
		ASSERT(m_Mode != eMode::Uninitialized, "ArenaAllocator::Restore: allocator is uninitialized");

		if (m_Mode == eMode::FixedBuffer)
		{
			ASSERT(marker.Chunk == nullptr);
			ASSERT(marker.Offset <= m_Capacity);
			m_Offset = marker.Offset;
			return;
		}

		// Growable: free chunks after marker.chunk and restore offset.
		Chunk* target = reinterpret_cast<Chunk*>(const_cast<void*>(marker.Chunk));
		ASSERT(target != nullptr, "ArenaAllocator::Restore: invalid marker chunk");

		// Find target in list (debug safety)
		bool found = false;
		for (Chunk* c = m_Head; c; c = c->Next)
		{
			if (c == target) { found = true; break; }
		}
		ASSERT(found, "ArenaAllocator::Restore: marker chunk not found in allocator");

		// Free all chunks after target
		Chunk* c = target->Next;
		target->Next = nullptr;

		while (c)
		{
			Chunk* next = c->Next;
			AlignedFree(c);
			c = next;
		}

		m_Tail = target;
		ASSERT(marker.Offset <= m_Tail->Capacity, "ArenaAllocator::Restore: invalid marker offset");
		m_Tail->Offset = marker.Offset;
	}

	size_t ArenaAllocator::GetUsedBytes() const
	{
		if (m_Mode == eMode::FixedBuffer)
		{
			return m_Offset;
		}
		else if (m_Mode == eMode::Growable)
		{
			size_t used = 0;
			for (Chunk* c = m_Head; c; c = c->Next) used += c->Offset;
			return used;
		}
		return 0;
	}

	size_t ArenaAllocator::GetCapacityBytes() const
	{
		if (m_Mode == eMode::FixedBuffer)
		{
			return m_Capacity;
		}
		else if (m_Mode == eMode::Growable)
		{
			size_t cap = 0;
			for (Chunk* c = m_Head; c; c = c->Next) cap += c->Capacity;
			return cap;
		}
		return 0;
	}

	size_t ArenaAllocator::GetRemainingBytes() const
	{
		const size_t cap = GetCapacityBytes();
		const size_t used = GetUsedBytes();
		return (used <= cap) ? (cap - used) : 0;
	}
} // namespace shz
