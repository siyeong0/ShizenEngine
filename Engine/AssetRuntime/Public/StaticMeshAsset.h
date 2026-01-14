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
	// - Filled by importers (e.g., Assimp).
	// - Consumed by Renderer to create StaticMeshRenderData.
	// ------------------------------------------------------------
	class StaticMeshAsset final // TODO: Inherit IAssetObject?
	{
	public:
		struct Vertex final
		{
			float3 Position = float3(0.0f, 0.0f, 0.0f);
			float3 Normal = float3(0.0f, 1.0f, 0.0f);
			float2 TexCoords = float2(0.0f, 0.0f);
		};

		struct Section final
		{
			uint32 FirstIndex = 0;
			uint32 IndexCount = 0;
			uint32 BaseVertex = 0; // optional for some pipelines
			uint32 MaterialSlot = 0; // index into material slots

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

		void SetVertices(std::vector<Vertex>&& vertices) { m_Vertices = std::move(vertices); }
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

		// Material slots are owned by this mesh asset.
		// Section.MaterialSlot indexes into this array.
		void SetMaterialSlots(std::vector<MaterialAsset>&& materials) { m_MaterialSlots = std::move(materials); }

		std::vector<MaterialAsset>& GetMaterialSlots() noexcept { return m_MaterialSlots; }
		const std::vector<MaterialAsset>& GetMaterialSlots() const noexcept { return m_MaterialSlots; }

		uint32 GetMaterialSlotCount() const noexcept { return static_cast<uint32>(m_MaterialSlots.size()); }

		// Returns nullptr if slot index is out of range.
		MaterialAsset* GetMaterialSlot(uint32 slot) noexcept;
		const MaterialAsset* GetMaterialSlot(uint32 slot) const noexcept;

		// ------------------------------------------------------------
		// Geometry getters
		// ------------------------------------------------------------

		const std::vector<Vertex>& GetVertices() const noexcept { return m_Vertices; }

		VALUE_TYPE GetIndexType() const noexcept { return m_IndexType; }

		std::vector<uint32>& GetIndicesU32() noexcept { return m_IndicesU32; }
		std::vector<uint16>& GetIndicesU16() noexcept { return m_IndicesU16; }
		const std::vector<uint32>& GetIndicesU32() const noexcept { return m_IndicesU32; }
		const std::vector<uint16>& GetIndicesU16() const noexcept { return m_IndicesU16; }

		// Raw index buffer pointer + size in bytes (for upload).
		// Returns nullptr if indices are not present.
		const void* GetIndexData() const noexcept;
		uint32 GetIndexDataSizeBytes() const noexcept;

		uint32 GetVertexCount() const noexcept { return static_cast<uint32>(m_Vertices.size()); }
		uint32 GetIndexCount() const noexcept;

		// ------------------------------------------------------------
		// Validation / bounds
		// ------------------------------------------------------------

		bool IsValid() const noexcept;
		bool HasCPUData() const noexcept;

		// Computes mesh bounds from vertex positions.
		// Also fills section bounds if sections exist.
		void RecomputeBounds();

		const Box& GetBounds() const noexcept { return m_Bounds; }

		// ------------------------------------------------------------
		// Memory policy
		// ------------------------------------------------------------

		// Strip CPU geometry after GPU upload, but keep:
		// - sections
		// - bounds
		// - metadata
		// - material slots
		void StripCPUData();

		// Clears all data, including sections, bounds, and material slots.
		void Clear();

	private:
		void recomputeSectionBounds();

	private:
		std::string m_Name;
		std::string m_SourcePath;

		std::vector<Vertex> m_Vertices;

		VALUE_TYPE m_IndexType = VT_UINT32;
		std::vector<uint32> m_IndicesU32;
		std::vector<uint16> m_IndicesU16;

		std::vector<Section> m_Sections;

		// Owned material slots for this mesh.
		std::vector<MaterialAsset> m_MaterialSlots;

		Box m_Bounds = {};
	};
} // namespace shz
