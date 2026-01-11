// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

#define QUERY_H_RESTRICTION_PERMISSIVE 1

#include <vector>
#include <exception>
#include <algorithm>

#include "Platforms/Win64/Public/WinHPreface.h"

#include <d3d12.h>
#include <atlbase.h>

#if USE_D3D12_LOADER
// On Win32 we manually load d3d12.dll and get entry points,
// but UWP does not support this, so we link with d3d12.lib
#    include "D3D12Loader.hpp"
#endif

#include "Platforms/Win64/Public/WinHPostface.h"

#ifndef NTDDI_WIN10_FE // First defined in Win SDK 10.0.20348.0
constexpr D3D_FEATURE_LEVEL D3D_FEATURE_LEVEL_12_2 = static_cast<D3D_FEATURE_LEVEL>(0xc200);
#endif

#ifndef NTDDI_WIN10_VB // First defined in Win SDK 10.0.19041.0
#    define D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS static_cast<D3D12_INDIRECT_ARGUMENT_TYPE>(D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW + 1)
#    define D3D12_RAYTRACING_TIER_1_1                  static_cast<D3D12_RAYTRACING_TIER>(11)
#    define D3D12_HEAP_FLAG_CREATE_NOT_ZEROED          D3D12_HEAP_FLAG_NONE
#endif

#ifndef NTDDI_WIN10_19H1 // First defined in Win SDK 10.0.18362.0
enum D3D12_SHADING_RATE
{
};
enum D3D12_SHADING_RATE_COMBINER
{
};

constexpr D3D12_RESOURCE_STATES D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE = static_cast<D3D12_RESOURCE_STATES>(0x1000000);
#endif

#include "Primitives/BasicTypes.h"
#include "Primitives/FlagEnum.h"
#include "Platforms/Common/PlatformDefinitions.h"
#include "Primitives/Errors.hpp"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Primitives/DebugUtilities.hpp"
#include "Engine/RHI_D3DBase/Public/D3DErrors.hpp"
#include "Engine/Core/Common/Public/Cast.hpp"
#include "Engine/Core/Memory/Public/STDAllocator.hpp"


#endif //PCH_H
