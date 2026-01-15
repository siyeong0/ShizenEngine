#include "ShizenEngine.h"
#include "Engine/AssetRuntime/Public/AssimpImporter.h"

#include <iostream>
#include <cmath>

namespace shz
{
	SampleBase* CreateSample()
	{
		return new ShizenEngine();
	}

	// ------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------

	static float computeUniformScaleToFitUnitCube(const Box& bounds, float targetSize = 1.0f) noexcept
	{
		const float3 size = bounds.Max - bounds.Min;

		float maxDim = size.x;
		if (size.y > maxDim) maxDim = size.y;
		if (size.z > maxDim) maxDim = size.z;

		// Degenerate safety
		const float eps = 1e-6f;
		if (maxDim < eps)
			return 1.0f;

		return targetSize / maxDim;
	}


	void ShizenEngine::spawnMeshesOnXYGrid(
		const std::vector<const char*>& meshPaths,
		float3 gridCenter,
		float spacingX,
		float spacingY)
	{
		m_Loaded.clear();
		m_Loaded.reserve(meshPaths.size());

		const int n = static_cast<int>(meshPaths.size());
		if (n <= 0)
			return;

		const float fn = static_cast<float>(n);
		const int cols = static_cast<int>(std::ceil(std::sqrt(fn)));
		const int rows = static_cast<int>(std::ceil(fn / static_cast<float>(cols)));

		const float totalX = (cols - 1) * spacingX;
		const float totalY = (rows - 1) * spacingY;

		const float startX = gridCenter.x - totalX * 0.5f;
		const float startY = gridCenter.y - totalY * 0.5f;
		const float startZ = gridCenter.z;

		for (int i = 0; i < n; ++i)
		{
			const int r = i / cols;
			const int c = i % cols;

			LoadedMesh entry = {};
			entry.Path = meshPaths[static_cast<size_t>(i)];

			// --------------------------------------------------------
			// Load CPU asset
			// --------------------------------------------------------
			StaticMeshAsset cpuMesh = {};
			if (!AssimpImporter::LoadStaticMeshAsset(entry.Path.c_str(), &cpuMesh))
			{
				std::cout << "Load Failed: " << entry.Path << std::endl;
				continue;
			}

			// --------------------------------------------------------
			// Compute uniform scale so mesh fits inside 1x1x1
			// --------------------------------------------------------
			const Box& bounds = cpuMesh.GetBounds(); // AABB {Min,Max} 라고 가정
			const float uniform = computeUniformScaleToFitUnitCube(bounds, 1.0f);
			entry.Scale = float3(uniform, uniform, uniform);

			// Register CPU asset
			entry.AssetHandle = m_pAssetManager->RegisterStaticMesh(cpuMesh);

			// Create GPU mesh
			entry.MeshHandle = m_pRenderer->CreateStaticMesh(entry.AssetHandle);
			if (!entry.MeshHandle.IsValid())
			{
				std::cout << "CreateStaticMesh failed: " << entry.Path << std::endl;
				continue;
			}

			// --------------------------------------------------------
			// XZ grid placement (y fixed)
			// --------------------------------------------------------
			entry.Position = float3(
				startX + static_cast<float>(c) * spacingX,
				startY + static_cast<float>(r) * spacingY,
				startZ);

			entry.BaseRotation = float3(0, 0, 0);

			entry.RotateAxis = 1;
			entry.RotateSpeed = 0.6f + 0.2f * static_cast<float>(i % 5);

			entry.ObjectId = m_pRenderScene->AddObject(
				entry.MeshHandle,
				Matrix4x4::TRS(entry.Position, entry.BaseRotation, entry.Scale));

			m_Loaded.push_back(std::move(entry));
		}
	}


	// ------------------------------------------------------------
	// Initialize
	// ------------------------------------------------------------

	void ShizenEngine::Initialize(const SampleInitInfo& InitInfo)
	{
		SampleBase::Initialize(InitInfo);

		// 1) AssetManager
		m_pAssetManager = std::make_unique<AssetManager>();

		// 2) Renderer
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
		rendererCreateInfo.pAssetManager = m_pAssetManager.get();

		m_pRenderer->Initialize(rendererCreateInfo);

		// 3) RenderScene
		m_pRenderScene = std::make_unique<RenderScene>();

		// 4) Camera/ViewFamily
		m_Camera.SetProjAttribs(
			0.1f,
			100.0f,
			static_cast<float>(rendererCreateInfo.BackBufferWidth) / rendererCreateInfo.BackBufferHeight,
			PI / 4.0f,
			SURFACE_TRANSFORM_IDENTITY);

		m_ViewFamily.Views.clear();
		m_ViewFamily.Views.push_back({});
		m_ViewFamily.Views[0].Viewport = {};

		// ------------------------------------------------------------
		// Debug cubes (optional)
		// ------------------------------------------------------------
		m_CubeHandle = m_pRenderer->CreateCubeMesh();

		// ------------------------------------------------------------
		// Load mesh paths + spawn as ONE XZ grid
		// ------------------------------------------------------------
		std::vector<const char*> meshPaths;
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/AnisotropyBarnLamp/glTF/AnisotropyBarnLamp.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/BoomBoxWithAxes/glTF/BoomBoxWithAxes.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/CesiumMan/glTF/CesiumMan.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/DamagedHelmet/glTF/DamagedHelmet.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/DamagedHelmet/DamagedHelmet.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/FlightHelmet/glTF/FlightHelmet.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/GlamVelvetSofa/glTF/GlamVelvetSofa.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/IridescenceAbalone/glTF/IridescenceAbalone.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/IridescenceMetallicSpheres/glTF/IridescenceMetallicSpheres.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/IridescentDishWithOlives/glTF/IridescentDishWithOlives.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/ToyCar/glTF/ToyCar.gltf");

		// XZ grid settings
		const float3 gridCenter = float3(0.0f, 0.0f, 5.0f);
		const float spacingX = 1.0f;
		const float spacingY = 1.0f;

		spawnMeshesOnXYGrid(meshPaths, gridCenter, spacingX, spacingY);
	}

	void ShizenEngine::Render()
	{
		m_ViewFamily.FrameIndex++;

		m_pRenderer->BeginFrame();
		m_pRenderer->Render(*m_pRenderScene, m_ViewFamily);
		m_pRenderer->EndFrame();
	}

	void ShizenEngine::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
	{
		SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);

		const float dt = static_cast<float>(ElapsedTime);
		(void)dt;

		m_Camera.Update(m_InputController, dt);

		m_ViewFamily.DeltaTime = dt;
		m_ViewFamily.Views[0].ViewMatrix = m_Camera.GetViewMatrix();
		m_ViewFamily.Views[0].ProjMatrix = m_Camera.GetProjMatrix();

		const float t = static_cast<float>(CurrTime);

		for (auto& m : m_Loaded)
		{
			if (!m.ObjectId.IsValid())
				continue;

			const float angle = t * m.RotateSpeed;
			float3 rot = m.BaseRotation;
			rot[m.RotateAxis] += angle;

			m_pRenderScene->SetObjectTransform(m.ObjectId,Matrix4x4::TRS(m.Position, rot, m.Scale));
		}
	}

} // namespace shz