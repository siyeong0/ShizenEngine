#pragma once
#include <string>
#include <vector>
#include <memory>

#include "Engine/Core/Runtime/Public/SampleBase.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/AssetManager/Public/AssetRef.hpp"
#include "Engine/AssetManager/Public/AssetTypeTraits.h"

#include "Engine/Framework/Public/FirstPersonCamera.h"

#include "Engine/ECS/Public/EcsWorld.h"

#include "Engine/ECS/Public/CName.h"
#include "Engine/ECS/Public/CTransform.h"
#include "Engine/ECS/Public/CMeshRenderer.h"

#include "Engine/Physics/Public/PhysicsSystem.h"

namespace shz
{
	class GrassViewer final : public SampleBase
	{
	public:
		void Initialize(const SampleInitInfo& InitInfo) override final;

		void Render() override final;
		void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;

		void ReleaseSwapChainBuffers() override final;
		void WindowResize(uint32 Width, uint32 Height) override final;

		const Char* GetSampleName() const override final { return "GrassViewer"; }

	protected:
		void UpdateUI() override final;

	public:
		struct ViewportState final
		{
			uint32 Width = 1;
			uint32 Height = 1;
		};

	private:
		struct EcsContext final
		{
			Renderer* pRenderer = nullptr;
			RenderScene* pRenderScene = nullptr;
			AssetManager* pAssetManager = nullptr;

			PhysicsSystem* pPhysicsSystem = nullptr;
		};

	private:
		void BuildSceneOnce();
		static Matrix4x4 ToMatrixTRS(const CTransform& t);

	private:
		std::unique_ptr<Renderer>     m_pRenderer = nullptr;
		std::unique_ptr<RenderScene>  m_pRenderScene = nullptr;
		std::unique_ptr<AssetManager> m_pAssetManager = nullptr;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		std::unique_ptr<shz::EcsWorld>     m_pEcs = nullptr;
		std::unique_ptr<PhysicsSystem>    m_pPhysicsSystem = nullptr;

		EcsContext m_EcsCtx = {};

		ViewportState     m_Viewport = {};
		ViewFamily        m_ViewFamily = {};
		FirstPersonCamera m_Camera = {};

		RenderScene::LightObject         m_GlobalLight = {};
		Handle<RenderScene::LightObject> m_GlobalLightHandle = {};

		float m_Speed = 3.0f;
	};
} // namespace shz
