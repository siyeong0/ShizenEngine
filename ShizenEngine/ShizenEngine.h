#pragma once

#include <vector>
#include <string>

#include "Engine/Core/Runtime/Public/SampleBase.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/ViewFamily.h"

#include "Engine/AssetRuntime/Public/AssetManager.h"
#include "FirstPersonCamera.h"

namespace shz
{
	class ShizenEngine final : public SampleBase
	{
	public:
		virtual void Initialize(const SampleInitInfo& InitInfo) override final;

		virtual void Render() override final;
		virtual void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;

		virtual const Char* GetSampleName() const override final { return "Shizen Engine"; }

	protected:
		void UpdateUI() override;

	private:
		struct LoadedMesh final
		{
			std::string Path = {};

			Handle<StaticMeshAsset>      AssetHandle = {};
			Handle<StaticMeshRenderData> MeshHandle = {};
			Handle<RenderScene::RenderObject> ObjectId = {};

			float3 Position = { 0, 0, 0 };
			float3 BaseRotation = { 0, 0, 0 };
			float3 Scale = { 1, 1, 1 };

			uint32 RotateAxis = 1;     // 0:X, 1:Y, 2:Z
			float  RotateSpeed = 1.0f; // rad/sec
		};

	private:
		void spawnMeshesOnXYGrid(
			const std::vector<const char*>& meshPaths,
			float3 gridCenter,
			float spacingX,
			float spacingY,
			float spacingZ);
	private:
		std::unique_ptr<Renderer>    m_pRenderer = nullptr;
		std::unique_ptr<RenderScene> m_pRenderScene = nullptr;
		std::unique_ptr<AssetManager> m_pAssetManager = nullptr;

		ViewFamily m_ViewFamily = {};
		FirstPersonCamera m_Camera;

		// Debug cube (renderer-owned)
		Handle<StaticMeshRenderData> m_CubeHandle = {};
		Handle<StaticMeshRenderData> m_FloorHandle = {};

		// Loaded meshes (1 object per path)
		std::vector<LoadedMesh> m_Loaded = {};

		RenderScene::LightObject m_GlobalLight = {};
		Handle<RenderScene::LightObject> m_GlobalLightHandle;
	};
} // namespace shz
