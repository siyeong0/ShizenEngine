#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include "Engine/Core/Runtime/Public/SampleBase.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/AssetManager/Public/AssetRef.hpp"
#include "Engine/AssetManager/Public/AssetTypeTraits.h"

#include "Engine/Framework/Public/FirstPersonCamera.h"

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

		struct LoadedStaticMesh final
		{
			std::string Path = {};

			AssetRef<StaticMesh> MeshRef = {};
			StaticMeshRenderData      Mesh = {};

			Handle<RenderScene::RenderObject> ObjectId = {};

			bool bCastShadow = true;
			bool bAlphaMasked = false;

			bool IsValid() const noexcept { return ObjectId.IsValid(); }
		};

	private:
		// Scene building (hard-coded)
		bool loadStaticMeshObject(
			LoadedStaticMesh& InOut,
			const char* Path,
			float3 Position,
			float3 Rotation,
			float3 Scale,
			bool bCastShadow,
			bool bAlphaMasked);

	private:
		std::unique_ptr<Renderer>     m_pRenderer = nullptr;
		std::unique_ptr<RenderScene>  m_pRenderScene = nullptr;
		std::unique_ptr<AssetManager> m_pAssetManager = nullptr;

		RefCntAutoPtr<IShaderSourceInputStreamFactory> m_pShaderSourceFactory;

		ViewportState     m_Viewport = {};
		ViewFamily        m_ViewFamily = {};
		FirstPersonCamera m_Camera = {};

		// Global light (UI editable)
		RenderScene::LightObject         m_GlobalLight = {};
		Handle<RenderScene::LightObject> m_GlobalLightHandle = {};

		// Hard-coded scene objects
		LoadedStaticMesh              m_Floor = {};
		std::vector<LoadedStaticMesh> m_Grasses = {};

		float m_Speed = 3.0f;
	};
} // namespace shz
