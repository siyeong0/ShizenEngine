// Engine/RenderSystem/Public/ShadowSystem.h

#pragma once
#include <array>
#include <vector>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/Core/Math/Math.h"

#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IBuffer.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	class Renderer;
	struct View;

	class ShadowSystem final
	{
	public:
		struct CreateInfo final
		{
			uint32 ShadowMapResolution = 4096;
			uint32 NumCascades = 4;

			bool  SnapCascades = true;
			bool  StabilizeExtents = true;
			bool  EqualizeExtents = true;
			float PartitioningFactor = 0.95f;

			float CascadeTransitionRegion = 0.1f;

			float ReceiverPlaneDepthBiasClamp = 0.02f;
			float FixedDepthBias = 0.0005f;

			int   FixedFilterSize = 5;
		};

		ShadowSystem() = default;
		ShadowSystem(const ShadowSystem&) = delete;
		ShadowSystem& operator=(const ShadowSystem&) = delete;
		~ShadowSystem() = default;

		void Initialize(Renderer& renderer, const CreateInfo& ci);
		void Shutdown();

		void UpdateShadowMatrices(Renderer& renderer, const View& mainView, const float3& lightDirWs);
		void InstallPasses(Renderer& renderer);

		uint32 GetNumCascades() const { return m_CI.NumCascades; }
		uint32 GetResolution()  const { return m_CI.ShadowMapResolution; }

	private:
		static constexpr uint32 kMaxCascades = 8;

		// (kept for compatibility; no longer used to build WorldToLightView)
		static void BuildLightViewBasis(const float3& lightDirWs, float3& X, float3& Y, float3& Z);

		static void GetFrustumMinimumBoundingSphere(
			float proj11, float proj22,
			float nearZ, float farZ,
			float3& outCenterView,
			float& outRadius);

		void DistributeCascades(
			const Matrix4x4& cameraView,
			const Matrix4x4& cameraProj,
			const Matrix4x4& cameraWorld,
			const float3& lightDirWs);

		static hlsl::CascadeAttribs& GetCascadeRef(hlsl::ShadowMapAttribs& A, uint32 idx);
		static void SetCascadeCamSpaceZEnd(hlsl::ShadowMapAttribs& A, uint32 idx, float z);

	private:
		CreateInfo m_CI = {};

		uint64 m_ShadowMapTexId = 0;
		uint64 m_ShadowMapSRVId = 0;
		std::array<uint64, kMaxCascades> m_ShadowMapCascadeDSVIds = {};

		uint64 m_ShadowAttribsCBId = 0;

		// HLSL struct 그대로 CPU에서도 사용
		hlsl::ShadowMapAttribs m_ShadowAttribs = {};
	};
} // namespace shz
