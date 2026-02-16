#pragma once
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/RuntimeData/Public/Material.h"

namespace shz
{
	struct StaticMeshBounds
	{
		float3 Center;
		float3 Extents;
		float Radius;

		Box GetBox() const { return Box(Center - Extents, Center + Extents); }
		Sphere GetSphere() const { return Sphere(Center, Radius); }
	};

	class StaticMeshLevel final
	{
	public:
		struct Section final
		{
			uint32 FirstIndex = 0;
			uint32 IndexCount = 0;
			uint32 BaseVertex = 0;     // Optional for some pipelines
			uint32 MaterialSlot = 0;     // Index into material slots

			StaticMeshBounds LocalBounds = {};
		};

	public:
		StaticMeshLevel() = default;
		StaticMeshLevel(const StaticMeshLevel&) = default;
		StaticMeshLevel(StaticMeshLevel&&) noexcept = default;
		StaticMeshLevel& operator=(const StaticMeshLevel&) = default;
		StaticMeshLevel& operator=(StaticMeshLevel&&) noexcept = default;
		~StaticMeshLevel() = default;

		// ------------------------------------------------------------
		// Geometry setters
		// ------------------------------------------------------------
		void ReserveVertices(uint32 count);

		void SetPositions(std::vector<float3>&& positions) { m_Positions = std::move(positions); }
		void SetNormals(std::vector<float3>&& normals) { m_Normals = std::move(normals); }
		void SetTangents(std::vector<float3>&& tangents) { m_Tangents = std::move(tangents); }
		void SetTexCoords(std::vector<float2>&& texCoords) { m_TexCoords = std::move(texCoords); }

		void SetIndicesU32(std::vector<uint32>&& indices);
		void SetIndicesU16(std::vector<uint16>&& indices);

		void ApplyUniformScale(float s);
		void MoveBottomToOrigin(bool centerXZ);

		// ------------------------------------------------------------
		// Sections (submeshes)
		// ------------------------------------------------------------
		void SetSections(std::vector<Section>&& sections) { m_Sections = std::move(sections); }

		std::vector<Section>& GetSections() noexcept { return m_Sections; }
		const std::vector<Section>& GetSections() const noexcept { return m_Sections; }

		// ------------------------------------------------------------
		// Materials (slots)
		// ------------------------------------------------------------
		bool HasMaterial() const { return !m_MaterialSlots.empty(); }
		void SetMaterialSlots(std::vector<MaterialId>&& materials) { m_MaterialSlots = std::move(materials); }

		std::vector<MaterialId>& GetMaterialSlots() noexcept { return m_MaterialSlots; }
		const std::vector<MaterialId>& GetMaterialSlots() const noexcept { return m_MaterialSlots; }

		uint32 GetMaterialSlotCount() const noexcept { return static_cast<uint32>(m_MaterialSlots.size()); }

		MaterialId& GetMaterialSlot(uint32 slot) noexcept;
		const MaterialId& GetMaterialSlot(uint32 slot) const noexcept;

		// ------------------------------------------------------------
		// Geometry getters (SoA)
		// ------------------------------------------------------------
		const std::vector<float3>& GetPositions() const noexcept { return m_Positions; }
		const std::vector<float3>& GetNormals() const noexcept { return m_Normals; }
		const std::vector<float3>& GetTangents() const noexcept { return m_Tangents; }
		const std::vector<float2>& GetTexCoords() const noexcept { return m_TexCoords; }

		VALUE_TYPE GetIndexType() const noexcept { return m_IndexType; }

		std::vector<uint32>& GetIndicesU32() noexcept { return m_IndicesU32; }
		std::vector<uint16>& GetIndicesU16() noexcept { return m_IndicesU16; }
		const std::vector<uint32>& GetIndicesU32() const noexcept { return m_IndicesU32; }
		const std::vector<uint16>& GetIndicesU16() const noexcept { return m_IndicesU16; }

		const void* GetIndexData() const noexcept;
		uint32 GetIndexDataSizeBytes() const noexcept;

		uint32 GetVertexCount() const noexcept { return static_cast<uint32>(m_Positions.size()); }
		uint32 GetIndexCount() const noexcept;

		// ------------------------------------------------------------
		// Validation / bounds
		// ------------------------------------------------------------
		bool IsValid() const noexcept;
		bool HasCPUData() const noexcept;

		void RecomputeBounds();
		const StaticMeshBounds& GetBounds() const noexcept { return m_Bounds; }
		Box GetBoxBounds() const noexcept { return m_Bounds.GetBox(); }
		Sphere GetSphereBounds() const noexcept { return m_Bounds.GetSphere(); }

		// ------------------------------------------------------------
		// Memory policy
		// ------------------------------------------------------------
		void StripCPUData();
		void Clear();

		// ------------------------------------------------------------
		// Geometry helpers
		// ------------------------------------------------------------
		static StaticMeshLevel CreateBillboard(AssetRef<Texture> texureRef, const std::string& templateName, MATERIAL_BLEND_MODE blendMode, float2 scale = { 1.0f, 1.0f }, float2 pivot = { 0.5f, 0.5f });

	private:
		uint32 GetIndexAt(uint32 i) const noexcept;
		void RecomputeSectionBounds();

	private:
		std::vector<float3> m_Positions;
		std::vector<float3> m_Normals;
		std::vector<float3> m_Tangents;
		std::vector<float2> m_TexCoords;

		VALUE_TYPE m_IndexType = VT_UINT32;
		std::vector<uint32> m_IndicesU32;
		std::vector<uint16> m_IndicesU16;

		std::vector<Section> m_Sections;
		std::vector<MaterialId> m_MaterialSlots;

		StaticMeshBounds m_Bounds = {};
	};

	class StaticMesh
	{
	public:
		void AddLevel(StaticMeshLevel&& level, float screenSizeLOD)
		{
			m_Levels.push_back(std::move(level));
			m_LodScreenSizes.push_back(screenSizeLOD);
		}

		const StaticMeshLevel& GetLevel(uint32 lod) const noexcept { return m_Levels[lod]; }
		const float GetLodScreenSize(uint32 lod) const noexcept { return m_LodScreenSizes[lod]; }

		uint32 GetLevelCount() const noexcept { return static_cast<uint32>(m_Levels.size()); }

		const std::vector<StaticMeshLevel>& GetLevels() const noexcept { return m_Levels; }
		const std::vector<float>& GetLodScreenSizes() const noexcept { return m_LodScreenSizes; }

		const StaticMeshLevel& operator[](size_t idx) const { return m_Levels[idx]; }

	private:
		std::vector<StaticMeshLevel> m_Levels;
		std::vector<float> m_LodScreenSizes;
	};
} // namespace shz