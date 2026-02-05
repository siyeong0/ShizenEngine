#ifndef HLSL_COMMON_HLSLI
#define HLSL_COMMON_HLSLI

// Returns pseudo-random in [0,1)
float Hash2D(float2 p)
{
    float v = 1.0e4 * sin(17.0 * p.x + 0.1 * p.y) * 0.1 + abs(sin(13.0 * p.y + p.x));
    return frac(v);
}

float DitherThreshold(float4 svPos)
{
    float2 ip = floor(svPos.xy);
    return Hash2D(ip);
}

void AlphaDitherTest(float alpha, float4 svPos)
{
    float r = DitherThreshold(svPos);
    if (alpha < r)
    {
        discard;
    }
}

//// 4x4 Bayer (0..15) -> [0,1)
//float Bayer4x4(uint2 p)
//{
//    // p mod 4
//    uint x = p.x & 3;
//    uint y = p.y & 3;

//    // 4x4 matrix in a compact form
//    static const uint M[16] =
//    {
//        0, 8, 2, 10,
//        12, 4, 14, 6,
//         3, 11, 1, 9,
//        15, 7, 13, 5
//    };
//    return (M[y * 4 + x] + 0.5) / 16.0;
//}

//void AlphaDitherTest(float alpha, float4 svPos)
//{
//    uint2 ip = uint2(svPos.xy); // already integer-ish in pixel shader
//    float t = Bayer4x4(ip);
//    clip(alpha - t); // discard if negative
//}


#endif // HLSL_COMMON_HLSLI
