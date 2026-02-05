#pragma once
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IShader.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
    class Renderer;
    class RenderScene;
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

    private:
        static constexpr uint32 INTERACTION_FIELD_SIZE = 4096;
        static constexpr uint32 MAX_NUM_INTERACTION_STAMPS = 1024;
        static constexpr uint32 THREAD_GROUP_SIZE_X = 8;
        static constexpr uint32 THREAD_GROUP_SIZE_Y = 8;

        std::string m_InteractionCS = "InteractionCompute.hlsl";

        RefCntAutoPtr<IPipelineState>         m_pDecayCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pDecaySRB;

        RefCntAutoPtr<IPipelineState>         m_pApplyCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pApplySRB;
    };
} // namespace shz
