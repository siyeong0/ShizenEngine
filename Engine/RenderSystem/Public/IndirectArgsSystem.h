#pragma once
#include <array>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	class Renderer;

	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	class IndirectArgsSystem final
	{
	public:
		IndirectArgsSystem() = default;
		~IndirectArgsSystem() = default;

		IndirectArgsSystem(const IndirectArgsSystem&) = delete;
		IndirectArgsSystem& operator=(const IndirectArgsSystem&) = delete;

		void InstallPasses(Renderer& renderer);

		uint32 AllocateSlot(const std::string& debugName);
		void ResetAllSlots();

		void SetTemplate(uint32 slot, const hlsl::IndirectArgsTemplate& t);
		uint32 GetNumSlots() const { return m_NumSlots; }
		static constexpr uint32 GetArgsOffsetBytes(uint32 slot) { return slot * 20u; }
		void SetWriteArgsShader(const char* filePath) { m_WriteArgsCS = filePath ? filePath : ""; }

	private:
		uint32 m_NumSlots = 0;
		std::array<uint8, MAX_NUM_INDIRECTS> m_SlotUsed = {};
		std::array<hlsl::IndirectArgsTemplate, MAX_NUM_INDIRECTS> m_Templates = {};

		RefCntAutoPtr<IPipelineState> m_pWriteArgsCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pWriteArgsSRB;

		std::string m_WriteArgsCS = "WriteIndirectArgs.hlsl";
	};
} // namespace shz
