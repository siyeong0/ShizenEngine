#pragma once
#include <array>
#include <string>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
    class Renderer;
    struct RenderPassBuilder;
    struct RenderPassContext;

    namespace hlsl
    {
#include "Shaders/HLSL_Structures.hlsli"
    }

    class IndirectArgsSystem final
    {
    public:
        struct MeshHandle final
        {
            uint32 MeshId = 0;
            uint32 BaseSlot = 0;
            uint32 NumSlots = 0;
        };

        IndirectArgsSystem() = default;
        ~IndirectArgsSystem() = default;
        IndirectArgsSystem(const IndirectArgsSystem&) = delete;
        IndirectArgsSystem& operator=(const IndirectArgsSystem&) = delete;

        void Initialize(Renderer& renderer);

        void InstallPasses(Renderer& renderer);

        MeshHandle AllocateMesh(const std::string& debugName, uint32 numSlots);
        uint32 AllocateSlot(const std::string& debugName);

        void ResetAllSlots();

        void SetTemplate(uint32 slot, const hlsl::IndirectArgsTemplate& t);

    private:
        bool allocateContiguousSlots(uint32 numSlots, uint32& outBaseSlot);

    private:
        // CPU-side mirrors for upload
        std::array<hlsl::IndirectArgsTemplate, MAX_NUM_INDIRECTS> m_Templates = {};
        std::array<uint32, MAX_NUM_INDIRECTS> m_SlotMeshId = {};

        std::array<uint8, MAX_NUM_INDIRECTS> m_SlotUsed = {};
        std::array<uint8, MAX_NUM_INDIRECT_MESHES> m_MeshUsed = {};

        uint32 m_NumSlots = 0;
        uint32 m_NumMeshes = 0;

        // Compute PSOs/SRBs
        RefCntAutoPtr<IPipelineState> m_pWriteArgsCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pWriteArgsSRB;

        RefCntAutoPtr<IPipelineState> m_pResetMeshCountsCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pResetMeshCountsSRB;

        // Shader path
        std::string m_WriteArgsCS = "WriteIndirectArgs.hlsl";
    };
} // namespace shz
