#pragma once
#include "Primitives/BasicTypes.h"

namespace shz
{
	static const uint64 kRes_FrameCB = STRING_HASH("FrameCB");
	static const uint64 kRes_DrawCB = STRING_HASH("DrawCB");
	static const uint64 kRes_ShadowCB = STRING_HASH("ShadowCB");

	static const uint64 kRes_ObjectTable_GB = STRING_HASH("ObjectTableSB.GBuffer");
	static const uint64 kRes_ObjectTable_GR = STRING_HASH("ObjectTableSB.Grass");
	static const uint64 kRes_ObjectTable_SH = STRING_HASH("ObjectTableSB.Shadow");
	static const uint64 kRes_ObjectIndexVB = STRING_HASH("ObjectIndexInstanceVB");

	static const uint64 kRes_EnvTex = STRING_HASH("EnvTex");
	static const uint64 kRes_EnvDiffuseTex = STRING_HASH("EnvDiffuseTex");
	static const uint64 kRes_EnvSpecularTex = STRING_HASH("EnvSpecularTex");
	static const uint64 kRes_EnvBrdfTex = STRING_HASH("EnvBrdfTex");

	const uint64 kRes_ErrorTex = STRING_HASH("ErrorTex");

	// Pass outputs (external views)
	static const uint64 kRes_ShadowMapSRV = STRING_HASH("Out.ShadowMapSRV");
	static const uint64 kRes_DepthSRV = STRING_HASH("Out.DepthSRV");
	static const uint64 kRes_DepthDSV = STRING_HASH("Out.DepthDSV");
	static const uint64 kRes_LightingSRV = STRING_HASH("Out.LightingSRV");
	static const uint64 kRes_LightingRTV = STRING_HASH("Out.LightingRTV");

	static const uint64 kRes_GBufferSRV0 = STRING_HASH("Out.GBufferSRV0");
	static const uint64 kRes_GBufferSRV1 = STRING_HASH("Out.GBufferSRV1");
	static const uint64 kRes_GBufferSRV2 = STRING_HASH("Out.GBufferSRV2");
	static const uint64 kRes_GBufferSRV3 = STRING_HASH("Out.GBufferSRV3");
} // namespace shz
