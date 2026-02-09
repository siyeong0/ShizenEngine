#pragma once
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;
	class TerrainSystem;
	struct RenderPassContext;
	struct RenderPassBuilder;

	class InteractionSystem final
	{
	public:
		InteractionSystem() = default;
		~InteractionSystem() = default;

		InteractionSystem(const InteractionSystem&) = delete;
		InteractionSystem& operator=(const InteractionSystem&) = delete;

		void InstallPasses(Renderer& renderer, TerrainSystem& terrain);

		float2 GetWorldOriginXZ() const;
		float2 GetWorldSizeXZ() const;
		uint2 GetTexelOrigin() const;
		uint32 GetInteractionFieldResolution() const { return INTERACTION_FIELD_SIZE; }

	private:
		static constexpr uint32 INTERACTION_FIELD_SIZE = 4096;
		static constexpr uint32 MAX_NUM_INTERACTION_STAMPS = 1024;
		static constexpr uint32 THREAD_GROUP_SIZE_X = 8;
		static constexpr uint32 THREAD_GROUP_SIZE_Y = 8;

		// World coverage of the interaction field (meters). (tweak)
		static constexpr float  INTERACTION_FIELD_WORLD_SIZE = 256.0f;

		std::string m_InteractionCS = "InteractionCompute.hlsl";

		// Persistent sliding state
		bool  m_bHasPrevOrigin = false;
		float2 m_PrevFieldOriginXZ = { 0.0f, 0.0f };  // snapped origin
		uint32 m_TexelOriginX = 0;
		uint32 m_TexelOriginY = 0;

		// PSOs
		RefCntAutoPtr<IPipelineState>         m_pDecayCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pDecaySRB;

		RefCntAutoPtr<IPipelineState>         m_pRectOpCSO;   // ClearRect / ApplySingleStamp
		RefCntAutoPtr<IShaderResourceBinding> m_pRectOpSRB;
	};
} // namespace shz
