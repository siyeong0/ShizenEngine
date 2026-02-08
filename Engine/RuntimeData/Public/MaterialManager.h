#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/RuntimeData/Public/Material.h"

namespace shz
{
	class MaterialManager final
	{
	public:
		MaterialManager() = default;
		MaterialManager(const MaterialManager&) = delete;
		MaterialManager(const MaterialManager&&) = delete;
		MaterialManager& operator=(const MaterialManager&) = delete;
		~MaterialManager() = default;

		static MaterialManager* GetInstance();

		MaterialId CreateMaterial(const std::string& name, const std::string& templateName);
		void DestryMaterial(MaterialId id);

		bool HasMaterial(MaterialId id) const;
		Material& GetMaterial(MaterialId id);
		const Material& GetMaterial(MaterialId id) const;

	private:
		std::unordered_map<MaterialId, Material> m_MaterialTable;
		std::atomic<uint64> m_Counter = 1;
	};
} // namespace shz