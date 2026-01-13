#pragma once

#include <unordered_map>
#include <vector>

#include "Engine/Core/Runtime/Public/SampleBase.h"
#include "Engine/Core/Math/Math.h"

namespace shz
{

	class Tutorial07_RenderPass final : public SampleBase
	{
	public:
		virtual void ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs) override final;

		virtual void Initialize(const SampleInitInfo& InitInfo) override final;

		virtual void Render() override final;
		virtual void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;

		virtual const Char* GetSampleName() const override final { return "Tutorial19: Render Passes"; }

		virtual void ReleaseSwapChainBuffers() override final;
		virtual void WindowResize(uint32 Width, uint32 Height) override final;

	protected:
		virtual void UpdateUI() override final;

	private:
		void createCubePSO(IShaderSourceInputStreamFactory* pShaderSourceFactory);
		void createLightVolumePSO(IShaderSourceInputStreamFactory* pShaderSourceFactory);
		void createAmbientLightPSO(IShaderSourceInputStreamFactory* pShaderSourceFactory);
		void createRenderPass();
		RefCntAutoPtr<IFramebuffer> createFramebuffer(ITextureView* pDstRenderTarget);
		void initLights();
		void createLightsBuffer();

		void drawScene();
		void applyLighting();
		void updateLights(float fElapsedTime);
		void releaseWindowResources();

		IFramebuffer* getCurrentFramebuffer();

	private:
		// Use 16-bit format to make sure it works on mobile devices
		static constexpr TEXTURE_FORMAT DEPTH_BUFFER_FORMAT = TEX_FORMAT_D16_UNORM;

		// Cube resources
		RefCntAutoPtr<IPipelineState> m_pCubePSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pCubeSRB;
		RefCntAutoPtr<IBuffer> m_CubeVertexBuffer;
		RefCntAutoPtr<IBuffer> m_CubeIndexBuffer;
		RefCntAutoPtr<IBuffer> m_pShaderConstantsCB;
		RefCntAutoPtr<ITextureView> m_CubeTextureSRV;

		// Light resources
		RefCntAutoPtr<IBuffer> m_pLightsBuffer;
		RefCntAutoPtr<IPipelineState> m_pLightVolumePSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pLightVolumeSRB;
		RefCntAutoPtr<IPipelineState> m_pAmbientLightPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pAmbientLightSRB;

		// Deferred
		struct GBuffer
		{
			RefCntAutoPtr<ITexture> pColorBuffer;
			RefCntAutoPtr<ITexture> pDepthZBuffer;
			RefCntAutoPtr<ITexture> pDepthBuffer;
		};
		GBuffer m_GBuffer;

		RefCntAutoPtr<IRenderPass> m_pRenderPass;

		std::unordered_map<ITextureView*, RefCntAutoPtr<IFramebuffer>> m_FramebufferCache;

		// Lights
		Matrix4x4 m_CameraViewProjMatrix;
		Matrix4x4 m_CameraViewProjInvMatrix;

		bool m_bShowLightVolumes = false;
		bool m_bAnimateLights = true;

		int  m_LightsCount = 10000;
		struct LightAttribs
		{
			float3 Location;
			float  Size = 0;
			float3 Color;
		};
		std::vector<LightAttribs> m_Lights;
		std::vector<float3> m_LightMoveDirs;

		constexpr static int GRID_DIMENSION = 7;
	};

} // namespace shz