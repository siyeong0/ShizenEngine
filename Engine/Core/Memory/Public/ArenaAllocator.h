#pragma once
#include "Primitives/Common.h"

namespace shz
{
    // ------------------------------------------------------------
    // ArenaAllocator
    // - Linear/Bump allocator.
    // - Fast allocations, bulk free via Reset().
    // - Supports markers (Save/Restore) for temporary scopes.
    // - Two modes:
    //   1) Fixed buffer (Initialize(buffer, bytes)).
    //   2) Growable chunks (InitializeGrowable(...)).
    // Notes:
    // - Not thread-safe by default.
    // - Individual Free is not supported.
    // ------------------------------------------------------------
    class ArenaAllocator
    {
    public:
        struct Marker
        {
            const void* Chunk = nullptr; // internal: chunk pointer (for growable)
            size_t Offset = 0;      // offset in that chunk
        };

    public:
        ArenaAllocator() = default;
        ~ArenaAllocator();

        ArenaAllocator(const ArenaAllocator&) = delete;
        ArenaAllocator& operator=(const ArenaAllocator&) = delete;

        ArenaAllocator(ArenaAllocator&& other) noexcept;
        ArenaAllocator& operator=(ArenaAllocator&& other) noexcept;

        // --------------------------------------------------------
        // Fixed buffer mode
        // - Arena does not own memory.
        // --------------------------------------------------------
        void Initialize(void* buffer, size_t bytes);
        void Shutdown(); // safe to call multiple times

        // --------------------------------------------------------
        // Growable mode
        // - Arena owns memory, allocates chunks from heap.
        // - firstChunkBytes: initial chunk size
        // - nextChunkBytes : minimum size for subsequent chunks
        //   (can still grow bigger if a single allocation needs it)
        // --------------------------------------------------------
        void InitializeGrowable(size_t firstChunkBytes, size_t nextChunkBytes = 0);
        bool IsGrowable() const { return m_Mode == eMode::Growable; }

        // --------------------------------------------------------
        // Allocate
        // - alignment must be power-of-two.
        // - returns nullptr if fixed arena runs out of memory.
        // --------------------------------------------------------
        void* Allocate(size_t bytes, size_t alignment = alignof(std::max_align_t));
        void* AllocateZero(size_t bytes, size_t alignment = alignof(std::max_align_t));

        template<typename T>
        T* AllocateArray(size_t count, size_t alignment = alignof(T))
        {
            static_assert(!std::is_void_v<T>);
            const size_t bytes = sizeof(T) * count;
            return reinterpret_cast<T*>(Allocate(bytes, alignment));
        }

        // Placement construction helpers
        template<typename T, typename... Args>
        T* New(Args&&... args)
        {
            void* mem = Allocate(sizeof(T), alignof(T));
            if (!mem)
            {
                return nullptr;
            }
            return ::new (mem) T(std::forward<Args>(args)...);
        }

        template<typename T>
        void Delete(T* /*ptr*/)
        {
            // Intentionally no-op:
            // Arena frees in bulk; destructors are user's responsibility.
            // If you need dtors, use ArenaScope + manual destructor calls,
            // or a separate "DestructorList" utility.
        }

        // --------------------------------------------------------
        // Reset / Markers
        // --------------------------------------------------------
        void Reset();                 // frees all allocations (keeps first chunk in growable)
        Marker Save() const;          // capture current position
        void Restore(Marker marker);  // roll back to marker

        // RAII scope: restores to marker on destruction
        class Scope
        {
        public:
            explicit Scope(ArenaAllocator& arena)
                : m_Arena(arena), m_Marker(arena.Save()) {
            }
            ~Scope() { m_Arena.Restore(m_Marker); }

            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;

        private:
            ArenaAllocator& m_Arena;
            Marker          m_Marker;
        };

        // --------------------------------------------------------
        // Stats
        // --------------------------------------------------------
        size_t GetUsedBytes() const;
        size_t GetCapacityBytes() const;
        size_t GetRemainingBytes() const;

    private:
        enum class eMode : std::uint8_t
        {
            Uninitialized = 0,
            FixedBuffer,
            Growable,
        };

        struct Chunk
        {
            Chunk* Next = nullptr;
            size_t Capacity = 0;
            size_t Offset = 0;
            byte* BeginAddress = nullptr;
            // followed by byte data[capacity]
            byte* Data() { return reinterpret_cast<byte*>(this + 1); }
            const byte* Data() const { return reinterpret_cast<const byte*>(this + 1); }
        };

    private:
        static bool isPowerOfTwo(size_t x) { return x && ((x & (x - 1)) == 0); }
        static size_t alignUp(size_t v, size_t alignment) { return (v + (alignment - 1)) & ~(alignment - 1); }

        void* allocateFixed(size_t bytes, size_t alignment);
        void* allocateGrowable(size_t bytes, size_t alignment);

        Chunk* allocateChunk(size_t payloadBytes);
        void freeAllChunksExceptFirst();

    private:
        eMode m_Mode = eMode::Uninitialized;

        // Fixed buffer state
        byte* m_Base = nullptr;
        size_t m_Capacity = 0;
        size_t m_Offset = 0;

        // Growable state
        Chunk* m_Head = nullptr;
        Chunk* m_Tail = nullptr;
        size_t m_FirstChunkBytes = 0;
        size_t m_NextChunkBytes = 0;
    };

} // namespace shz