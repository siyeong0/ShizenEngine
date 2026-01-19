/*
 *  Copyright 2019-2022 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

#include "Primitives/FormatString.hpp"

namespace shz
{
#if defined(_WIN32)
#define COREASSERT_DEBUG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define COREASSERT_DEBUG_BREAK() __builtin_trap()
#else
#define COREASSERT_DEBUG_BREAK() std::abort()
#endif

#ifdef SHZ_DEBUG

#define ASSERTION_FAILED(Message, ...)                                      \
    do {                                                                    \
        auto msg = shz::FormatString((Message) __VA_OPT__(,) __VA_ARGS__);  \
        std::fprintf(stderr,                                                \
          "[ASSERT] %s:%d in %s\n  expr: %s\n",                             \
          __FILE__, __LINE__, __func__, msg.c_str());                       \
        std::fflush(stderr);                                                \
        COREASSERT_DEBUG_BREAK();                                           \
    } while (false)

#define ASSERT(expr, Message, ...)                                          \
    do {                                                                    \
        if (!(expr)) {                                                      \
            ASSERTION_FAILED((Message) __VA_OPT__(,) __VA_ARGS__);          \
        }                                                                   \
    } while (false)
#else
#define ASSERT(...)do{}while(false)
#endif

#define ASSERT_EXPR(expr) ASSERT((expr), "Debug expression failed:\n", #expr)

#ifdef SHZ_DEBUG
	template <typename DstType, typename SrcType>
	void CheckDynamicType(SrcType* pSrcPtr)
	{
		ASSERT(pSrcPtr == nullptr || dynamic_cast<DstType*>(pSrcPtr) != nullptr, "Dynamic type cast failed. Src typeid: \'", typeid(*pSrcPtr).name(), "\' Dst typeid: \'", typeid(DstType).name(), '\'');
	}
# define CHECK_DYNAMIC_TYPE(DstType, pSrcPtr) \
    do                                        \
    {                                         \
        CheckDynamicType<DstType>(pSrcPtr);   \
    } while (false)


#else

#define CHECK_DYNAMIC_TYPE(...) do{}while(false)

#endif

} // namespace shz