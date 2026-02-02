#include "pch.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

namespace shz
{
	static MaterialManager* g_Instance = nullptr;

	MaterialManager* MaterialManager::GetInstance()
	{
		if (g_Instance)
		{
			return g_Instance;
		}
		else
		{
			g_Instance = new MaterialManager();
			return g_Instance;
		}
	}

	MaterialId MaterialManager::CreateMaterial(const std::string& name, const std::string& templateName)
	{
		Material material(name, templateName);
		uint64 id = m_Counter++;
		ASSERT(!HasMaterial(id), "Duplicated ID.");
		m_MaterialTable.emplace(id, std::move(material));
		return id;
	}

	void MaterialManager::DestryMaterial(MaterialId id)
	{
		ASSERT(HasMaterial(id), "Buffer id not found.");
		m_MaterialTable.erase(id);
	}

	bool MaterialManager::HasMaterial(MaterialId id) const
	{
		auto it = m_MaterialTable.find(id);
		return it != m_MaterialTable.end();
	}

	Material& MaterialManager::GetMaterial(MaterialId id)
	{
		auto it = m_MaterialTable.find(id);
		ASSERT(it != m_MaterialTable.end(), "Material is not found.");
		return it->second;
	}

	const Material& MaterialManager::GetMaterial(MaterialId id) const
	{
		auto it = m_MaterialTable.find(id);
		ASSERT(it != m_MaterialTable.end(), "Material is not found.");
		return it->second;
	}
} // namespace shz