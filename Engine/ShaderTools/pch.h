// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

#include <unordered_set>
#include <cstring>
#include <array>
#include <vector>
#include <sstream>

#include "Engine/Core/Memory/Public/DataBlobImpl.hpp"
#include "Engine/Core/Memory/Public/DefaultRawMemoryAllocator.hpp"
#include "Engine/Core/Common/Public/StringDataBlobImpl.hpp"
#include "Engine/Core/Common/Public/ParsingTools.hpp"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/Core/Common/Public/BasicFileSystem.hpp"
#include "Primitives/DebugUtilities.hpp"

#include "Engine/GraphicsUtils/Public/GraphicsUtils.hpp"

#endif //PCH_H
