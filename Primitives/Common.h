#pragma once
#include <iostream>
#include <cstdlib>
#include <cstdint>
#include "Primitives/FormatString.hpp"
#include "Primitives/DebugUtilities.hpp"

// ------------------------------------------------
// SAFE_CLEANUP and related macros
// ------------------------------------------------

#define SAFE_CLEANUP(ptr, deleter)                                           \
    do {                                                                     \
        auto& _p = (ptr);                                                    \
        if (_p) {                                                            \
            deleter(_p);                                                     \
            _p = nullptr;                                                    \
        }                                                                    \
    } while (0)

#define SAFE_RELEASE(p)         SAFE_CLEANUP(p, [](auto* x){ x->Release(); })
#define SAFE_FREE(p)            SAFE_CLEANUP(p, std::free)
#define SAFE_FREE_LIBRARY(h)    SAFE_CLEANUP(h, FreeLibrary)
#define SAFE_DELETE(p)          SAFE_CLEANUP(p, [](auto* x){ delete x; })
#define SAFE_DELETE_ARRAY(p)    SAFE_CLEANUP(p, [](auto* x){ delete[] x; })
#define SAFE_CLOSE_HANDLE(h)    SAFE_CLEANUP(h, [](auto x){ if (x != INVALID_HANDLE_VALUE) CloseHandle(x); })

// ------------------------------------------------
// Aligned Alloc/Free
// -------------------------------------------------
inline void* AlignedAlloc(std::size_t alignment, std::size_t size)
{
#if defined(__cpp_aligned_alloc)
	// C++17 aligned_alloc is available
	// NOTE: size must be multiple of alignment (caller responsibility)
	return std::aligned_alloc(alignment, size);

#elif defined(_MSC_VER)
	// MSVC fallback
	return _aligned_malloc(size, alignment);

#else
	// POSIX fallback
	void* p = nullptr;
	if (posix_memalign(&p, alignment, size) != 0)
		return nullptr;
	return p;
#endif
}

inline void AlignedFree(void* p)
{
#if defined(__cpp_aligned_alloc)
	std::free(p);

#elif defined(_MSC_VER)
	_aligned_free(p);

#else
	std::free(p);
#endif
}
