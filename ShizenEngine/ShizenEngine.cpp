#include "ShizenEngine.h"
#include "Engine/AssetRuntime/Public/AssimpImporter.h"

#include <iostream>
#include <cmath>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/ImGuiUtils.hpp"
#include "Engine/ImGui/Public/imGuIZMO.h"

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
		float spacingY,
		float spacingZ)
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
		const float totalZ = (rows - 1) * spacingZ;

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
				startZ + static_cast<float>(r) * spacingZ);

			entry.BaseRotation = float3(0, 0, 0);

			entry.RotateAxis = 1;
			entry.RotateSpeed = 0.6f + 0.2f * static_cast<float>(i % 5);

			entry.ObjectId = m_pRenderScene->AddObject(
				entry.MeshHandle,
				Matrix4x4::TRS(entry.Position, entry.BaseRotation, entry.Scale));

			m_Loaded.push_back(std::move(entry));
		}
	}

	static inline StaticMeshAsset CreateCubeStaticMeshAsset(const char* Name = "Cube")
	{
		StaticMeshAsset Mesh = {};
		Mesh.SetName(Name ? std::string(Name) : std::string("Cube"));
		Mesh.SetSourcePath("builtin://Cube");

		// 24 vertices (4 per face)
		std::vector<float3> Positions;
		std::vector<float3> Normals;
		std::vector<float3> Tangents;
		std::vector<float2> UVs;

		Positions.reserve(24);
		Normals.reserve(24);
		Tangents.reserve(24);
		UVs.reserve(24);

		auto PushFace = [&](float3 v0, float3 v1, float3 v2, float3 v3, float3 n, float3 t) // v0..v3 in CCW order
		{
			// UV convention:
			// v0: (0,1), v1: (1,1), v2: (1,0), v3: (0,0)
			Positions.push_back(v0); UVs.push_back(float2(0, 1)); Normals.push_back(n); Tangents.push_back(t);
			Positions.push_back(v1); UVs.push_back(float2(1, 1)); Normals.push_back(n); Tangents.push_back(t);
			Positions.push_back(v2); UVs.push_back(float2(1, 0)); Normals.push_back(n); Tangents.push_back(t);
			Positions.push_back(v3); UVs.push_back(float2(0, 0)); Normals.push_back(n); Tangents.push_back(t);
		};

		// Cube corners (unit cube centered at origin)
		const float3 p000 = float3(-0.5f, -0.5f, -0.5f);
		const float3 p001 = float3(-0.5f, -0.5f, +0.5f);
		const float3 p010 = float3(-0.5f, +0.5f, -0.5f);
		const float3 p011 = float3(-0.5f, +0.5f, +0.5f);
		const float3 p100 = float3(+0.5f, -0.5f, -0.5f);
		const float3 p101 = float3(+0.5f, -0.5f, +0.5f);
		const float3 p110 = float3(+0.5f, +0.5f, -0.5f);
		const float3 p111 = float3(+0.5f, +0.5f, +0.5f);

		// Faces (each face: 4 verts, CCW when looking at the face from outside)
		// -Z (back)
		PushFace(p000, p100, p110, p010, float3(0, 0, -1), float3(+1, 0, 0));
		// +Z (front)
		PushFace(p101, p001, p011, p111, float3(0, 0, +1), float3(+1, 0, 0));
		// -X (left)
		PushFace(p001, p000, p010, p011, float3(-1, 0, 0), float3(0, 0, -1));
		// +X (right)
		PushFace(p100, p101, p111, p110, float3(+1, 0, 0), float3(0, 0, +1));
		// -Y (bottom)
		PushFace(p001, p101, p100, p000, float3(0, -1, 0), float3(+1, 0, 0));
		// +Y (top)
		PushFace(p010, p110, p111, p011, float3(0, +1, 0), float3(+1, 0, 0));

		// Indices: 6 faces * 2 triangles * 3 = 36
		std::vector<uint32> Indices;
		Indices.reserve(36);

		for (uint32 face = 0; face < 6; ++face)
		{
			const uint32 base = face * 4;

			Indices.push_back(base + 0);
			Indices.push_back(base + 1);
			Indices.push_back(base + 2);

			Indices.push_back(base + 0);
			Indices.push_back(base + 2);
			Indices.push_back(base + 3);
		}

		Mesh.SetPositions(std::move(Positions));
		Mesh.SetNormals(std::move(Normals));
		Mesh.SetTangents(std::move(Tangents));
		Mesh.SetTexCoords(std::move(UVs));
		Mesh.SetIndicesU32(std::move(Indices));

		// One section that covers the whole mesh
		{
			StaticMeshAsset::Section Sec = {};
			Sec.FirstIndex = 0;
			Sec.IndexCount = Mesh.GetIndexCount();
			Sec.BaseVertex = 0;
			Sec.MaterialSlot = 0;
			Sec.LocalBounds = Box{ float3(-0.5f, -0.5f, -0.5f), float3(+0.5f, +0.5f, +0.5f) };

			std::vector<StaticMeshAsset::Section> Sections;
			Sections.push_back(Sec);
			Mesh.SetSections(std::move(Sections));
		}

		// Bounds (if your Box ctor above is correct, this is enough)
		// If you want robust: Mesh.RecomputeBounds();
		Mesh.RecomputeBounds();

		return Mesh;
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
		//StaticMeshAsset cubeMeshAsset = CreateCubeStaticMeshAsset();
		//auto cubeMeshHandle = m_pAssetManager->RegisterStaticMesh(cubeMeshAsset);
		//m_CubeHandle = m_pRenderer->CreateStaticMesh(cubeMeshHandle);
		//auto dummy1 = m_pRenderScene->AddObject(m_CubeHandle, Matrix4x4::TRS({ 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 20.0f, 0.2f, 20.0f }));

		StaticMeshAsset floorMeshAsset;
		AssimpImportOptions options = {};
		AssimpImporter::LoadStaticMeshAsset("C:/Dev/ShizenEngine/ShizenEngine/Assets/floor/FbxFloor.fbx", &floorMeshAsset);
		auto floorMeshHandle = m_pAssetManager->RegisterStaticMesh(floorMeshAsset);
		m_FloorHandle = m_pRenderer->CreateStaticMesh(floorMeshHandle);
		auto dummy2 = m_pRenderScene->AddObject(m_FloorHandle, Matrix4x4::TRS({ 0.0f, -0.5f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0, 1.0f, 1.0f }));

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
		const float3 gridCenter = float3(0.0f, 1.25f, 5.0f);
		const float spacingX = 1.0f;
		const float spacingY = 1.0f;
		const float spacingZ = 2.0f;

		spawnMeshesOnXYGrid(meshPaths, gridCenter, spacingX, spacingY, spacingZ);

		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);
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
		const float currTime = static_cast<float>(CurrTime);

		m_Camera.Update(m_InputController, dt);

		m_ViewFamily.DeltaTime = dt;
		m_ViewFamily.CurrentTime = currTime;
		m_ViewFamily.Views[0].CameraPosition = m_Camera.GetPos();
		m_ViewFamily.Views[0].ViewMatrix = m_Camera.GetViewMatrix();
		m_ViewFamily.Views[0].ProjMatrix = m_Camera.GetProjMatrix();
		m_ViewFamily.Views[0].NearPlane = m_Camera.GetProjAttribs().NearClipPlane;
		m_ViewFamily.Views[0].FarPlane = m_Camera.GetProjAttribs().FarClipPlane;

		for (auto& m : m_Loaded)
		{
			if (!m.ObjectId.IsValid())
				continue;

			const float angle = currTime * m.RotateSpeed;
			float3 rot = m.BaseRotation;
			rot[m.RotateAxis] += angle;

			m_pRenderScene->SetObjectTransform(m.ObjectId, Matrix4x4::TRS(m.Position, rot, m.Scale));
		}

		m_pRenderScene->UpdateLight(m_GlobalLightHandle, m_GlobalLight);
	}

	void ShizenEngine::UpdateUI()
	{
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::gizmo3D("##LightDirection", m_GlobalLight.Direction, ImGui::GetTextLineHeight() * 10);
			ImGui::ColorEdit3("##LightColor", reinterpret_cast<float*>(&m_GlobalLight.Color));
			ImGui::SliderFloat("Value", &m_GlobalLight.Intensity, 0.01f, 10.0f);
		}
		ImGui::End();
	}
} // namespace shz