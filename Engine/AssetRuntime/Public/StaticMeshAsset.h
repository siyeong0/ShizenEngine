#pragma once
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/AssetRuntime/Public/MaterialAsset.h"

namespace shz
{
	// ------------------------------------------------------------
	// StaticMeshAsset
	// - CPU-side static mesh asset data (no GPU/RHI dependency).
	// - Stored as SoA for easy vertex stream split later.
	// - Importers may feed AoS and deinterleave into these streams.
	// ------------------------------------------------------------
	class StaticMeshAsset final
	{
	public:
		struct Section final
		{
			uint32 FirstIndex = 0;
			uint32 IndexCount = 0;
			uint32 BaseVertex = 0;     // Optional for some pipelines
			uint32 MaterialSlot = 0;     // Index into material slots

			Box LocalBounds = {};
		};

	public:
		StaticMeshAsset() = default;
		StaticMeshAsset(const StaticMeshAsset&) = default;
		StaticMeshAsset(StaticMeshAsset&&) noexcept = default;
		StaticMeshAsset& operator=(const StaticMeshAsset&) = default;
		StaticMeshAsset& operator=(StaticMeshAsset&&) noexcept = default;
		~StaticMeshAsset() = default;

		// ------------------------------------------------------------
		// Metadata
		// ------------------------------------------------------------
		void SetName(const std::string& name) { m_Name = name; }
		const std::string& GetName() const noexcept { return m_Name; }

		void SetSourcePath(const std::string& path) { m_SourcePath = path; }
		const std::string& GetSourcePath() const noexcept { return m_SourcePath; }

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
		void SetMaterialSlots(std::vector<MaterialAsset>&& materials) { m_MaterialSlots = std::move(materials); }

		std::vector<MaterialAsset>& GetMaterialSlots() noexcept { return m_MaterialSlots; }
		const std::vector<MaterialAsset>& GetMaterialSlots() const noexcept { return m_MaterialSlots; }

		uint32 GetMaterialSlotCount() const noexcept { return static_cast<uint32>(m_MaterialSlots.size()); }

		MaterialAsset& GetMaterialSlot(uint32 slot) noexcept;
		const MaterialAsset& GetMaterialSlot(uint32 slot) const noexcept;

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
		const Box& GetBounds() const noexcept { return m_Bounds; }

		// ------------------------------------------------------------
		// Memory policy
		// ------------------------------------------------------------
		void StripCPUData();
		void Clear();

	private:
		uint32 GetIndexAt(uint32 i) const noexcept;
		void RecomputeSectionBounds();

	private:
		std::string m_Name;
		std::string m_SourcePath;

		std::vector<float3> m_Positions;
		std::vector<float3> m_Normals;
		std::vector<float3> m_Tangents;
		std::vector<float2> m_TexCoords;

		VALUE_TYPE m_IndexType = VT_UINT32;
		std::vector<uint32> m_IndicesU32;
		std::vector<uint16> m_IndicesU16;

		std::vector<Section> m_Sections;
		std::vector<MaterialAsset> m_MaterialSlots;

		Box m_Bounds = {};
	};
} // namespace shz
