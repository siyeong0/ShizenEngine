#include "ShizenEngine.h"

namespace shz
{
	SampleBase* CreateSample()
	{
		return new ShizenEngine();
	}

	void ShizenEngine::Initialize(const SampleInitInfo& InitInfo)
	{
		SampleBase::Initialize(InitInfo);

		// Renderer 생성/초기화
		m_pRenderer = std::make_unique<Renderer>();

		RendererCreateInfo rendererCreateInfo = {};
		rendererCreateInfo.pEngineFactory = m_pEngineFactory;
		rendererCreateInfo.pDevice = m_pDevice;
		rendererCreateInfo.pImmediateContext = m_pImmediateContext;
		rendererCreateInfo.pDeferredContexts = m_pDeferredContexts;
		rendererCreateInfo.pSwapChain = m_pSwapChain;
		rendererCreateInfo.pImGui = m_pImGui;
		rendererCreateInfo.BackBufferWidth = m_pSwapChain->GetDesc().Width;
		rendererCreateInfo.BackBufferHeight = m_pSwapChain->GetDesc().Height;

		m_pRenderer->Initialize(rendererCreateInfo);

		m_pRenderScene = std::make_unique<RenderScene>();

		m_Camera.SetProjAttribs(0.1f, 100.0f, static_cast<float>(rendererCreateInfo.BackBufferWidth) / rendererCreateInfo.BackBufferHeight, PI / 4.0f, SURFACE_TRANSFORM_IDENTITY);

		m_ViewFamily.Views.push_back({});
		m_ViewFamily.Views[0].Viewport = {};

		MeshHandle handle = m_pRenderer->CreateCubeMesh();
		m_pRenderScene->AddObject(handle, Matrix4x4::Identity());
	}

	void ShizenEngine::Render()
	{
		m_ViewFamily.FrameIndex++;
		// viewFamily는 네 엔진에 이미 있던 걸 그대로 사용한다고 가정
		// (없으면 우선 더미를 만들거나 Renderer::Render에서 무시하도록)
		m_pRenderer->BeginFrame();
		m_pRenderer->Render(*m_pRenderScene, m_ViewFamily);
		m_pRenderer->EndFrame();

		// 샘플이 직접 Clear/Draw 하던 코드 제거!
		// (Clear + Draw는 Renderer 내부에서 처리)
	}

	void ShizenEngine::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
	{
		SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);

		float dt = static_cast<float>(ElapsedTime);

		m_Camera.Update(m_InputController, dt);

		m_ViewFamily.DeltaTime = dt;
		m_ViewFamily.Views[0].ViewMatrix = m_Camera.GetViewMatrix();
		m_ViewFamily.Views[0].ProjMatrix = m_Camera.GetProjMatrix();
		// 지금 단계에서는 오브젝트/라이트 갱신 없어도 됨.
		// 필요해지면 아래처럼 Scene을 갱신하는 식으로 확장.

		// 예) 디버그 삼각형 토글:
		// m_pRenderScene->SetDrawDebugTriangle(true/false);

		// 예) 나중에 메쉬 추가할 때:
		// MeshHandle cube = m_pRenderer->CreateCubeMesh("...");
		// RenderObjectId obj = m_pRenderScene->AddObject(cube, Matrix4x4::Identity());
	}

} // namespace shz
