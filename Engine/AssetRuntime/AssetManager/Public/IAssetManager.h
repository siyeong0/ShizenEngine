#pragma once
#include <string>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Common/AssetID.hpp"
#include "Engine/AssetRuntime/Common/AssetObject.h"
#include "Engine/AssetRuntime/Common/EAssetStatus.h"

namespace shz
{
    using AssetTypeID = uint64;

    struct SHZ_INTERFACE IAssetManager
    {
    public:
        virtual ~IAssetManager() = default;

        virtual void AddStrongRef(const AssetID& id, AssetTypeID typeId) noexcept = 0;
        virtual void ReleaseStrongRef(const AssetID& id, AssetTypeID typeId) noexcept = 0;

        // Acquire/Prefetch will call this (idempotent).
        virtual void RequestLoad(const AssetID& id, AssetTypeID typeId, uint32 flags) = 0;
        virtual void RequestSave(const AssetID& id, AssetTypeID typeId, const std::string& outPath, uint32 flags) = 0;

        virtual EAssetLoadStatus GetLoadStatusByID(const AssetID& id, AssetTypeID typeId) const noexcept = 0;
        virtual EAssetSaveStatus GetSaveStatusByID(const AssetID& id, AssetTypeID typeId) const noexcept = 0;

        virtual AssetObject* TryGetByID(const AssetID& id, AssetTypeID typeId) noexcept = 0;
        virtual const AssetObject* TryGetByID(const AssetID& id, AssetTypeID typeId) const noexcept = 0;

        virtual void WaitLoadByID(const AssetID& id, AssetTypeID typeId) const = 0;
        virtual void WaitSaveByID(const AssetID& id, AssetTypeID typeId) const = 0;
    };
} // namespace shz
