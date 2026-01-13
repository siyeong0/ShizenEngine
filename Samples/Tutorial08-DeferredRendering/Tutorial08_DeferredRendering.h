#pragma once

#include <unordered_map>
#include <vector>

#include "Engine/Core/Runtime/Public/SampleBase.h"
#include "Engine/Core/Math/Math.h"

namespace shz
{
	namespace
	{
		namespace HLSL
		{
#include "Assets/HLSL_Structures.hlsli"
		}
	} // namespace

	class Tutorial08_DeferredRendering final : public SampleBase
	{
	public:
		Tutorial08_DeferredRendering() = default;
		~Tutorial08_DeferredRendering() override = default;

		void ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& attribs) override;
		void Initialize(const SampleInitInfo& InitInfo) override;
		void Render() override;
		void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override;

		void ReleaseSwapChainBuffers() override;
		void WindowResize(uint32 Width, uint32 Height) override;

	protected:
		void UpdateUI() override;

	private:
		// ---------------------------------------------------------------------
		// Render targets / textures
		// ---------------------------------------------------------------------
		struct GBufferTargets
		{
			RefCntAutoPtr<ITexture> pAlbedo;   // RGBA8
			RefCntAutoPtr<ITexture> pNormal;   // RGBA16F
			RefCntAutoPtr<ITexture> pMaterial; // RGBA8
			RefCntAutoPtr<ITexture> pDepthZ;   // R32F (fallback 가능)
			RefCntAutoPtr<ITexture> pDepth;    // Depth buffer
		};

		struct ShadowTargets
		{
			RefCntAutoPtr<ITexture> pShadowMap; // Depth texture (DSV + SRV)
			RefCntAutoPtr<ITextureView> pShadowDSV;
			RefCntAutoPtr<ITextureView> pShadowSRV;
			uint32                  Width = 2048;
			uint32                  Height = 2048;
			float                   Bias = 0.0015f;
			float                   Strength = 1.0f;
		};

		struct PostTargets
		{
			RefCntAutoPtr<ITexture> pLightingHDR; // RGBA16F
		};

	private:
		// ---------------------------------------------------------------------
		// Pass creation
		// ---------------------------------------------------------------------
		void createShadowPass();
		void createGBufferPass();
		void createLightingPass();
		void createPostPass();

		void createShadowPSO(IShaderSourceInputStreamFactory* pShaderSourceFactory);
		void createGBufferPSO(IShaderSourceInputStreamFactory* pShaderSourceFactory);
		void createLightingPSO(IShaderSourceInputStreamFactory* pShaderSourceFactory);
		void createPostPSO(IShaderSourceInputStreamFactory* pShaderSourceFactory);

		RefCntAutoPtr<IFramebuffer> createShadowFramebuffer();
		RefCntAutoPtr<IFramebuffer> createGBufferFramebuffer();
		RefCntAutoPtr<IFramebuffer> createLightingFramebuffer();
		RefCntAutoPtr<IFramebuffer> createPostFramebuffer(ITextureView* pBackBufferRTV);

		void releaseWindowResources();

		// ---------------------------------------------------------------------
		// Draw helpers
		// ---------------------------------------------------------------------
		void drawScene_Shadow();
		void drawScene_GBuffer();
		void drawFullscreen_Lighting();
		void drawFullscreen_Post();

		// ---------------------------------------------------------------------
		// Lights
		// ---------------------------------------------------------------------
		void initLights();
		void updateLights(float fElapsedTime);
		void createLightsBuffer();

	private:
		// ---------------------------------------------------------------------
		// Geometry / textures (Tutorial07 스타일 유지)
		// ---------------------------------------------------------------------
		RefCntAutoPtr<IBuffer>      m_CubeVertexBuffer;
		RefCntAutoPtr<IBuffer>      m_CubeIndexBuffer;
		RefCntAutoPtr<ITextureView> m_CubeTextureSRV;

		RefCntAutoPtr<IBuffer>      m_PlaneVertexBuffer;
		RefCntAutoPtr<IBuffer>      m_PlaneIndexBuffer;
		RefCntAutoPtr<ITextureView> m_PlaneTextureSRV;

		// ---------------------------------------------------------------------
		// Constant buffers
		// ---------------------------------------------------------------------
		RefCntAutoPtr<IBuffer> m_pShaderConstantsCB; // ShaderConstants (camera/viewport)
		RefCntAutoPtr<IBuffer> m_pShadowConstantsCB; // ShadowConstants (light VP, bias, texel size)
		RefCntAutoPtr<IBuffer> m_pObjectConstantsCB; // ObjectConstants (world, world invert transpose)
		// ---------------------------------------------------------------------
		// Lights structured buffer (StructuredBuffer<LightAttribs>)
		// ---------------------------------------------------------------------
		RefCntAutoPtr<IBuffer>      m_pLightsBuffer;
		RefCntAutoPtr<IBufferView>  m_pLightsSRV;

		int  m_LightsCount = 512;  // Lighting.psh는 내부에서 MAX_LIGHTS=1024로 루프돎
		bool m_bAnimateLights = true;

		std::vector<HLSL::LightAttribs> m_Lights;
		std::vector<float3> m_LightMoveDirs;

		// ---------------------------------------------------------------------
		// Pass objects
		// ---------------------------------------------------------------------
		ShadowTargets  m_Shadow;
		GBufferTargets m_GBuffer;
		PostTargets    m_Post;

		RefCntAutoPtr<IRenderPass> m_pShadowRenderPass;
		RefCntAutoPtr<IRenderPass> m_pGBufferRenderPass;
		RefCntAutoPtr<IRenderPass> m_pLightingRenderPass;
		RefCntAutoPtr<IRenderPass> m_pPostRenderPass;

		RefCntAutoPtr<IFramebuffer> m_pShadowFB;
		RefCntAutoPtr<IFramebuffer> m_pGBufferFB;
		RefCntAutoPtr<IFramebuffer> m_pLightingFB;

		std::unordered_map<ITextureView*, RefCntAutoPtr<IFramebuffer>> m_PostFBCache;

		// ---------------------------------------------------------------------
		// PSOs / SRBs
		// ---------------------------------------------------------------------
		RefCntAutoPtr<IPipelineState>        m_pShadowPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pShadowSRB;

		RefCntAutoPtr<IPipelineState>        m_pGBufferPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGBufferSRB_Cube;
		RefCntAutoPtr<IShaderResourceBinding> m_pGBufferSRB_Plane;

		RefCntAutoPtr<IPipelineState>        m_pLightingPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pLightingSRB;

		RefCntAutoPtr<IPipelineState>        m_pPostPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pPostSRB;

		// ---------------------------------------------------------------------
		// Camera / matrices
		// ---------------------------------------------------------------------
		float4x4 m_CameraViewProjMatrix;
		float4x4 m_CameraViewProjInvMatrix;

		// Light view-projection
		float4x4 m_LightViewProj;

		bool m_ConvertPSOutputToGamma = false; // 기존 튜토리얼과 동일 옵션
	};

} // namespace shz
