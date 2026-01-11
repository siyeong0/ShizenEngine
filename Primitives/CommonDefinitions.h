/*
 *  Copyright 2019-2023 Diligent Graphics LLC
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

// \file
// Common definitions

#include <stdint.h>

#ifdef _MSC_VER
// Note that MSVC x86 compiler by default uses __this call for class member functions
#    define SHZ_CALL_TYPE __cdecl
#else
#    define SHZ_CALL_TYPE
#endif

#if UINTPTR_MAX == UINT64_MAX
#    define SHZ_PLATFORM_64 1
#elif UINTPTR_MAX == UINT32_MAX
#    define SHZ_PLATFORM_32 1
#else
#    pragma error Unexpected value of UINTPTR_MAX
#endif

#define SHZ_INTERFACE __declspec(novtable)


// Compiler-specific definition
#ifdef _MSC_VER
#    if _MSC_VER >= 1917
#        define NODISCARD [[nodiscard]]
#    else
#        define NODISCARD
#    endif
#endif // _MSC_VER

#ifdef __clang__
#    if __has_feature(cxx_attributes)
#        define NODISCARD [[nodiscard]]
#    else
#        define NODISCARD
#    endif
#endif // __clang__

#ifdef __GNUC__
#    if __has_cpp_attribute(nodiscard)
#        define NODISCARD [[nodiscard]]
#    else
#        define NODISCARD
#    endif
#endif // __GNUC__
