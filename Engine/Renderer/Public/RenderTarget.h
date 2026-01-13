#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/ISwapChain.h"

namespace shz
{
    enum LOAD_ACTION
    {
        LOAD_ACTION_LOAD,
        LOAD_ACTION_CLEAR,
        LOAD_ACTION_DISCARD
    };

    struct RenderTarget
    {
        bool bUseSwapChainBackBuffer = true;

        ITextureView* pColorRTV = nullptr;
        ITextureView* pDepthDSV = nullptr;

        uint32 Width = 0;
        uint32 Height = 0;

        LOAD_ACTION ColorLoad = LOAD_ACTION_CLEAR;
        LOAD_ACTION DepthLoad = LOAD_ACTION_CLEAR;

        float ClearColor[4] = { 0.35f, 0.35f, 0.35f, 1.0f };
        float ClearDepth = 1.0f;
        uint8 ClearStencil = 0;
    };
} // namespace shz
