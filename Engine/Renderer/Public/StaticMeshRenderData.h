#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/Core/Math/Math.h"

#include "Engine/RHI/Interface/IBuffer.h"

namespace shz
{
	class StaticMeshRenderData final
	{
	public:
		struct Section final
		{
			uint32 FirstIndex = 0;
			uint32 IndexCount = 0;
			uint32 BaseVertex = 0;
			uint32 MaterialSlot = 0;

			Box LocalBounds = {};
		};

	public:
		StaticMeshRenderData() = default;
		~StaticMeshRenderData() = default;

		bool IsValid() const noexcept
		{
			return (m_pVertexBuffer != nullptr) && (m_pIndexBuffer != nullptr) && (m_VertexCount > 0) && (m_IndexCount > 0);
		}

		IBuffer* GetVertexBuffer() const noexcept { return m_pVertexBuffer; }
		IBuffer* GetIndexBuffer() const noexcept { return m_pIndexBuffer; }

		uint32 GetVertexStride() const noexcept { return m_VertexStride; }
		uint32 GetVertexCount() const noexcept { return m_VertexCount; }
		uint32 GetIndexCount() const noexcept { return m_IndexCount; }
		VALUE_TYPE GetIndexType() const noexcept { return m_IndexType; }

		const Box& GetLocalBounds() const { return m_LocalBounds; }

		uint32 GetSectionCount() const noexcept { return static_cast<uint32>(m_Sections.size()); }
		const Section& GetSection(uint32 i) const noexcept { return m_Sections[i]; }
		const std::vector<Section>& GetSections() const noexcept { return m_Sections; }

		void SetVertexBuffer(IBuffer* pVB) noexcept { m_pVertexBuffer = pVB; }
		void SetIndexBuffer(IBuffer* pIB) noexcept { m_pIndexBuffer = pIB; }
		void SetVertexStride(uint32 stride) noexcept { m_VertexStride = stride; }
		void SetVertexCount(uint32 c) noexcept { m_VertexCount = c; }
		void SetIndexCount(uint32 c) noexcept { m_IndexCount = c; }
		void SetIndexType(VALUE_TYPE t) noexcept { m_IndexType = t; }

		void SetLocalBounds(const Box& b) noexcept { m_LocalBounds = b; }

		void SetSections(std::vector<Section>&& secs) { m_Sections = std::move(secs); }

	private:
		RefCntAutoPtr<IBuffer> m_pVertexBuffer = {};
		RefCntAutoPtr<IBuffer> m_pIndexBuffer = {};

		uint32 m_VertexStride = 0;
		uint32 m_VertexCount = 0;
		uint32 m_IndexCount = 0;
		VALUE_TYPE m_IndexType = VT_UINT32;

		Box m_LocalBounds = {};

		std::vector<Section> m_Sections = {};
	};
}
