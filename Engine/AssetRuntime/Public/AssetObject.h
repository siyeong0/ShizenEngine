#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Public/AssetId.h"

namespace shz
{
	class AssetObject
	{
	public:
		AssetObject() : m_Id() {};
		virtual ~AssetObject() = default;

		AssetId GetId() const { return m_Id; }

	private:
		const AssetId m_Id;
	};
} // namespace shz