#ifndef HLSL_STRUCTURES_HLSLI
#define HLSL_STRUCTURES_HLSLI

// ----------------------------------------------
// Constant buffers
// ----------------------------------------------
struct FrameConstants
{
    float4x4 ViewProj;
};


struct ObjectConstants
{
    float4x4 World;
};

#endif // HLSL_STRUCTURES_HLSLI
