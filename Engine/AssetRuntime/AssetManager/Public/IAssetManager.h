#pragma once
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

		virtual EAssetStatus GetStatusByID(const AssetID& id, AssetTypeID typeId) const noexcept = 0;

		virtual AssetObject* TryGetByID(const AssetID& id, AssetTypeID typeId) noexcept = 0;
		virtual const AssetObject* TryGetByIDConst(const AssetID& id, AssetTypeID typeId) const noexcept = 0;

		virtual void WaitByID(const AssetID& id, AssetTypeID typeId) const = 0;
	};
} // namespace shz
