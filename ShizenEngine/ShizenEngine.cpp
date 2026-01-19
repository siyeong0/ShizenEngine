#include "ShizenEngine.h"
#include "Engine/AssetRuntime/Public/AssimpImporter.h"

#include <iostream>
#include <cmath>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/ImGuiUtils.hpp"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/Material/Public/MaterialTemplate.h"
#include "Engine/Material/Public/MaterialInstance.h"

#include "Tools/Image/Public/TextureUtilities.h"

#include "Engine/AssetRuntime/Public/AssetTypeTraits.h"

namespace shz
{
	namespace
	{
#include "Engine/Renderer/Shaders/HLSL_Structures.hlsli"

		static float computeUniformScaleToFitUnitCube(const Box& bounds, float targetSize = 1.0f) noexcept
		{
			const float3 size = bounds.Max - bounds.Min;

			float maxDim = size.x;
			if (size.y > maxDim) maxDim = size.y;
			if (size.z > maxDim) maxDim = size.z;

			const float eps = 1e-6f;
			if (maxDim < eps)
				return 1.0f;

			return targetSize / maxDim;
		}
	}

	SampleBase* CreateSample()
	{
		return new ShizenEngine();
	}

	// ------------------------------------------------------------
	// AssetManager integration helpers
	// ------------------------------------------------------------

	AssetID ShizenEngine::makeAssetIDFromPath(AssetTypeID typeId, const std::string& path) const
	{
		const size_t h0 = std::hash<std::string>{}(path);
		const size_t h1 = std::hash<std::string>{}(path + std::to_string(static_cast<uint64>(typeId)));

		const uint64 hi = static_cast<uint64>(h0) ^ (static_cast<uint64>(typeId) * 0x9E3779B185EBCA87ull);
		const uint64 lo = static_cast<uint64>(h1) ^ (static_cast<uint64>(typeId) * 0xC2B2AE3D27D4EB4Full);

		return AssetID(hi, lo);
	}

	void ShizenEngine::registerAssetLoaders()
	{
		ASSERT(m_pAssetManager, "AssetManagerImpl is null.");

		// StaticMeshAsset loader (Assimp)
		m_pAssetManager->RegisterLoader(AssetTypeTraits<StaticMeshAsset>::TypeID,
			[](const AssetRegistry::AssetMeta& meta,
				std::unique_ptr<AssetObject>& outObject,
				uint64& outResidentBytes,
				std::string& outError) -> bool
			{
				StaticMeshAsset mesh = {};
				if (!AssimpImporter::LoadStaticMeshAsset(meta.SourcePath.c_str(), &mesh))
				{
					outError = "LoadStaticMeshAsset failed.";
					return false;
				}

				outObject = std::make_unique<TypedAssetObject<StaticMeshAsset>>(static_cast<StaticMeshAsset&&>(mesh));
				outResidentBytes = 0;
				outError.clear();
				return true;
			});

		// TextureAsset loader (optional / stub)
		m_pAssetManager->RegisterLoader(AssetTypeTraits<TextureAsset>::TypeID,
			[](const AssetRegistry::AssetMeta& meta,
				std::unique_ptr<AssetObject>& outObject,
				uint64& outResidentBytes,
				std::string& outError) -> bool
			{
				TextureAsset tex = {};
				(void)meta;

				outObject = std::make_unique<TypedAssetObject<TextureAsset>>(static_cast<TextureAsset&&>(tex));
				outResidentBytes = 0;
				outError.clear();
				return true;
			});

		// MaterialInstanceAsset loader (optional / stub)
		m_pAssetManager->RegisterLoader(AssetTypeTraits<MaterialInstanceAsset>::TypeID,
			[](const AssetRegistry::AssetMeta& meta,
				std::unique_ptr<AssetObject>& outObject,
				uint64& outResidentBytes,
				std::string& outError) -> bool
			{
				MaterialInstanceAsset mi = {};
				(void)meta;

				outObject = std::make_unique<TypedAssetObject<MaterialInstanceAsset>>(static_cast<MaterialInstanceAsset&&>(mi));
				outResidentBytes = 0;
				outError.clear();
				return true;
			});
	}

	AssetRef<StaticMeshAsset> ShizenEngine::registerStaticMeshPath(const std::string& path)
	{
		const AssetID id = makeAssetIDFromPath(AssetTypeTraits<StaticMeshAsset>::TypeID, path);
		m_pAssetManager->RegisterAsset(id, AssetTypeTraits<StaticMeshAsset>::TypeID, path);
		return AssetRef<StaticMeshAsset>(id);
	}

	AssetRef<TextureAsset> ShizenEngine::registerTexturePath(const std::string& path)
	{
		const AssetID id = makeAssetIDFromPath(AssetTypeTraits<TextureAsset>::TypeID, path);
		m_pAssetManager->RegisterAsset(id, AssetTypeTraits<TextureAsset>::TypeID, path);
		return AssetRef<TextureAsset>(id);
	}

	AssetPtr<StaticMeshAsset> ShizenEngine::loadStaticMeshBlocking(AssetRef<StaticMeshAsset> ref, EAssetLoadFlags flags)
	{
		return m_pAssetManager->LoadBlocking(ref, flags);
	}

	AssetPtr<TextureAsset> ShizenEngine::loadTextureBlocking(AssetRef<TextureAsset> ref, EAssetLoadFlags flags)
	{
		return m_pAssetManager->LoadBlocking(ref, flags);
	}

	void ShizenEngine::ensureResourceStateSRV(ITexture* pTex)
	{
		if (!m_pImmediateContext || !pTex)
			return;

		StateTransitionDesc barrier = {};
		barrier.pResource = pTex;
		barrier.OldState = RESOURCE_STATE_UNKNOWN;
		barrier.NewState = RESOURCE_STATE_SHADER_RESOURCE;
		barrier.Flags = STATE_TRANSITION_FLAG_UPDATE_STATE;

		m_pImmediateContext->TransitionResourceStates(1, &barrier);
	}

	ITextureView* ShizenEngine::getOrCreateTextureSRV(const std::string& path)
	{
		if (path.empty())
			return nullptr;

		auto it = m_RuntimeTextureCache.find(path);
		if (it != m_RuntimeTextureCache.end() && it->second)
		{
			return it->second->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		}

		// Register + load in AssetManagerImpl (pipeline validation)
		AssetRef<TextureAsset> texRef = registerTexturePath(path);
		(void)loadTextureBlocking(texRef, EAssetLoadFlags::AllowFallback);

		RefCntAutoPtr<ITexture> tex;

		TextureLoadInfo tli = {};
		tli.IsSRGB = true;

		CreateTextureFromFile(path.c_str(), tli, m_pDevice, &tex);
		if (!tex)
			return nullptr;

		ensureResourceStateSRV(tex.RawPtr());

		m_RuntimeTextureCache.emplace(path, tex);
		return tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
	}

	// ------------------------------------------------------------
	// Material creation
	// ------------------------------------------------------------

	MaterialInstance ShizenEngine::CreateMaterialInstanceFromAsset(const MaterialInstanceAsset& matInstanceAsset)
	{
		// ------------------------------------------------------------
		// Build MaterialInstanceCreateInfo for GBuffer pipeline
		// ------------------------------------------------------------

		MaterialInstanceCreateInfo matInstCI = {};
		matInstCI.PipelineType = MATERIAL_PIPELINE_TYPE_GRAPHICS;

		matInstCI.TemplateName = "PBR_GBuffer_Template";

		// Shaders (do NOT create shaders here; MaterialInstance will create them)
		{
			matInstCI.ShaderStages.clear();
			matInstCI.ShaderStages.reserve(2);

			MaterialShaderStageDesc vsStage = {};
			vsStage.ShaderType = SHADER_TYPE_VERTEX;
			vsStage.DebugName = "GBuffer VS";
			vsStage.FilePath = "GBuffer.vsh";
			vsStage.EntryPoint = "main";
			vsStage.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			vsStage.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			vsStage.UseCombinedTextureSamplers = false;

			MaterialShaderStageDesc psStage = {};
			psStage.ShaderType = SHADER_TYPE_PIXEL;
			psStage.DebugName = "GBuffer PS";
			psStage.FilePath = "GBuffer.psh";
			psStage.EntryPoint = "main";
			psStage.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			psStage.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
			psStage.UseCombinedTextureSamplers = false;

			matInstCI.ShaderStages.push_back(vsStage);
			matInstCI.ShaderStages.push_back(psStage);
		}

		// Graphics PSO info
		{
			matInstCI.Graphics.Name = "GBuffer PSO (Material)";

			matInstCI.Graphics.PSODesc = {};
			matInstCI.Graphics.PSODesc.Name = matInstCI.Graphics.Name.c_str();
			matInstCI.Graphics.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

			GraphicsPipelineDesc& gp = matInstCI.Graphics.GraphicsPipeline;
			gp = {};

			// NOTE:
			// You must provide a valid render pass for render-pass based pipelines.
			// Replace this with your actual pointer (renderer-owned render pass).
			gp.pRenderPass = m_pRenderer->GetGBufferRenderPass();
			gp.SubpassIndex = 0;

			gp.NumRenderTargets = 0;
			gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
			gp.RTVFormats[1] = TEX_FORMAT_UNKNOWN;
			gp.RTVFormats[2] = TEX_FORMAT_UNKNOWN;
			gp.RTVFormats[3] = TEX_FORMAT_UNKNOWN;
			gp.DSVFormat = TEX_FORMAT_UNKNOWN;

			gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
			gp.RasterizerDesc.FrontCounterClockwise = true;

			gp.DepthStencilDesc.DepthEnable = true;
			gp.DepthStencilDesc.DepthWriteEnable = true;
			gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

			static LayoutElement layoutElems[] =
			{
				LayoutElement{0, 0, 3, VT_FLOAT32, false},
				LayoutElement{1, 0, 2, VT_FLOAT32, false},
				LayoutElement{2, 0, 3, VT_FLOAT32, false},
				LayoutElement{3, 0, 3, VT_FLOAT32, false},

				LayoutElement{4, 1, 1, VT_UINT32,  false, LAYOUT_ELEMENT_AUTO_OFFSET, sizeof(uint32), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1},
			};
			layoutElems[4].Stride = sizeof(uint32);

			gp.InputLayout.LayoutElements = layoutElems;
			gp.InputLayout.NumElements = _countof(layoutElems);
		}

		// Resource layout
		// - STATIC: renderer-owned (FRAME_CONSTANTS, g_ObjectTable, etc)
		// - DYNAMIC: MATERIAL_CONSTANTS
		// - MUTABLE: textures
		{
			matInstCI.ResourceLayout = {};
			matInstCI.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

			matInstCI.ResourceLayout.Variables.clear();
			matInstCI.ResourceLayout.Variables.reserve(16);

			ShaderResourceVariableDesc v0 = {};
			v0.ShaderStages = SHADER_TYPE_PIXEL;
			v0.Name = "MATERIAL_CONSTANTS";
			v0.Type = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
			matInstCI.ResourceLayout.Variables.push_back(v0);

			ShaderResourceVariableDesc v1 = {};
			v1.ShaderStages = SHADER_TYPE_PIXEL;
			v1.Name = "g_BaseColorTex";
			v1.Type = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
			matInstCI.ResourceLayout.Variables.push_back(v1);

			ShaderResourceVariableDesc v2 = {};
			v2.ShaderStages = SHADER_TYPE_PIXEL;
			v2.Name = "g_NormalTex";
			v2.Type = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
			matInstCI.ResourceLayout.Variables.push_back(v2);

			ShaderResourceVariableDesc v3 = {};
			v3.ShaderStages = SHADER_TYPE_PIXEL;
			v3.Name = "g_MetallicRoughnessTex";
			v3.Type = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
			matInstCI.ResourceLayout.Variables.push_back(v3);

			ShaderResourceVariableDesc v4 = {};
			v4.ShaderStages = SHADER_TYPE_PIXEL;
			v4.Name = "g_AOTex";
			v4.Type = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
			matInstCI.ResourceLayout.Variables.push_back(v4);

			ShaderResourceVariableDesc v5 = {};
			v5.ShaderStages = SHADER_TYPE_PIXEL;
			v5.Name = "g_EmissiveTex";
			v5.Type = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
			matInstCI.ResourceLayout.Variables.push_back(v5);

			// Optional height
			ShaderResourceVariableDesc v6 = {};
			v6.ShaderStages = SHADER_TYPE_PIXEL;
			v6.Name = "g_HeightTex";
			v6.Type = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
			matInstCI.ResourceLayout.Variables.push_back(v6);

			matInstCI.ResourceLayout.ImmutableSamplers.clear();
			matInstCI.ResourceLayout.ImmutableSamplers.reserve(4);

			SamplerDesc linearWrap =
			{
				FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
				TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
			};

			ImmutableSamplerDesc s0 = {};
			s0.ShaderStages = SHADER_TYPE_PIXEL;
			s0.SamplerOrTextureName = "g_LinearWrapSampler";
			s0.Desc = linearWrap;

			matInstCI.ResourceLayout.ImmutableSamplers.push_back(s0);
		}

		// ------------------------------------------------------------
		// Create material instance (new flow)
		// ------------------------------------------------------------

		MaterialInstance materialInstance = {};
		{
			const bool ok = materialInstance.Initialize(m_pDevice, m_pShaderSourceFactory, matInstCI);
			ASSERT(ok, "MaterialInstance::Initialize failed.");
		}

		// ------------------------------------------------------------
		// Set material parameters (same as before)
		// ------------------------------------------------------------

		// BaseColorFactor
		{
			float bc[4] =
			{
				matInstanceAsset.GetParams().BaseColor.x,
				matInstanceAsset.GetParams().BaseColor.y,
				matInstanceAsset.GetParams().BaseColor.z,
				matInstanceAsset.GetParams().BaseColor.w
			};
			materialInstance.SetFloat4("g_BaseColorFactor", bc);
		}

		materialInstance.SetFloat("g_RoughnessFactor", matInstanceAsset.GetParams().Roughness);
		materialInstance.SetFloat("g_MetallicFactor", matInstanceAsset.GetParams().Metallic);
		materialInstance.SetFloat("g_OcclusionStrength", matInstanceAsset.GetParams().Occlusion);

		{
			float ec[3] =
			{
				matInstanceAsset.GetParams().EmissiveColor.x,
				matInstanceAsset.GetParams().EmissiveColor.y,
				matInstanceAsset.GetParams().EmissiveColor.z
			};
			materialInstance.SetFloat3("g_EmissiveFactor", ec);
			materialInstance.SetFloat("g_EmissiveIntensity", matInstanceAsset.GetParams().EmissiveIntensity);
		}

		materialInstance.SetFloat("g_AlphaCutoff", matInstanceAsset.GetParams().AlphaCutoff);
		materialInstance.SetFloat("g_NormalScale", matInstanceAsset.GetParams().NormalScale);

		uint materialFlags = 0;

		auto BindOrDefault = [&](MATERIAL_TEXTURE_SLOT texSlot, const char* shaderVar, const RefCntAutoPtr<ITexture>& defaultTex, uint flagBit)
		{
			if (matInstanceAsset.GetTexture(texSlot).IsValid())
			{
				const std::string texPath = matInstanceAsset.GetTexture(texSlot).GetSourcePath();
				ITextureView* srv = getOrCreateTextureSRV(texPath);
				if (srv)
				{
					materialInstance.SetTextureRuntimeView(shaderVar, srv);
					materialFlags |= flagBit;
					return;
				}
			}

			materialInstance.SetTextureRuntimeView(shaderVar, defaultTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		};

		BindOrDefault(MATERIAL_TEX_ALBEDO, "g_BaseColorTex", m_DefaultTextures.White, MAT_HAS_BASECOLOR);
		BindOrDefault(MATERIAL_TEX_NORMAL, "g_NormalTex", m_DefaultTextures.Normal, MAT_HAS_NORMAL);
		BindOrDefault(MATERIAL_TEX_ORM, "g_MetallicRoughnessTex", m_DefaultTextures.MetallicRoughness, MAT_HAS_MR);
		BindOrDefault(MATERIAL_TEX_EMISSIVE, "g_EmissiveTex", m_DefaultTextures.Emissive, MAT_HAS_EMISSIVE);
		BindOrDefault(MATERIAL_TEX_AO, "g_AOTex", m_DefaultTextures.AO, MAT_HAS_AO);
		BindOrDefault(MATERIAL_TEX_HEIGHT, "g_HeightTex", m_DefaultTextures.Black, MAT_HAS_HEIGHT);

		materialInstance.SetUint("g_MaterialFlags", materialFlags);
		materialInstance.MarkAllDirty();

		return materialInstance;
	}


	void ShizenEngine::LoadMesh(const char* path, float3 position, float3 rotation, float3 scale, bool bUniformScale, bool bRotate)
	{
		LoadedMesh entry = {};
		entry.Path = path;

		// Register + load through new AssetManagerImpl
		entry.MeshRef = registerStaticMeshPath(entry.Path);
		entry.MeshID = makeAssetIDFromPath(AssetTypeTraits<StaticMeshAsset>::TypeID, entry.Path);
		entry.MeshPtr = loadStaticMeshBlocking(entry.MeshRef);

		const StaticMeshAsset* cpuMesh = entry.MeshPtr.Get();
		if (!cpuMesh)
		{
			std::cout << "Load Failed: " << entry.Path << std::endl;
			ASSERT(false, "");
			return;
		}

		entry.Scale = { 1.0f ,1.0f, 1.0f };
		if (bUniformScale)
		{
			const Box& bounds = cpuMesh->GetBounds();
			const float uniform = computeUniformScaleToFitUnitCube(bounds, 1.0f);
			entry.Scale = float3(uniform, uniform, uniform);
		}

		// GPU mesh
		entry.MeshHandle = m_pRenderer->CreateStaticMesh(*cpuMesh);
		if (!entry.MeshHandle.IsValid())
		{
			std::cout << "CreateStaticMesh failed: " << entry.Path << std::endl;
			ASSERT(false, "");
			return;
		}

		entry.Position = position;
		entry.BaseRotation = rotation;
		entry.Scale *= scale;

		if (bRotate)
		{
			entry.RotateAxis = 1;
			entry.RotateSpeed = 0.6f;
		}

		std::vector<MaterialInstance> materials;
		for (const MaterialInstanceAsset& matInstanceAsset : cpuMesh->GetMaterialSlots())
		{
			materials.push_back(CreateMaterialInstanceFromAsset(matInstanceAsset));
		}

		entry.ObjectId = m_pRenderScene->AddObject(
			entry.MeshHandle,
			materials,
			Matrix4x4::TRS(entry.Position, entry.BaseRotation, entry.Scale));

		m_Loaded.push_back(std::move(entry));
	}

	// ------------------------------------------------------------
	// Initialize
	// ------------------------------------------------------------

	void ShizenEngine::Initialize(const SampleInitInfo& InitInfo)
	{
		SampleBase::Initialize(InitInfo);

		// 1) New AssetManagerImpl
		m_pAssetManager = std::make_unique<AssetManager>();
		registerAssetLoaders();

		// 2) Renderer
		m_pRenderer = std::make_unique<Renderer>();

		m_pEngineFactory->CreateDefaultShaderSourceStreamFactory("C:/Dev/ShizenEngine/Engine/Renderer/Shaders", &m_pShaderSourceFactory);

		RendererCreateInfo rendererCreateInfo = {};
		rendererCreateInfo.pEngineFactory = m_pEngineFactory;
		rendererCreateInfo.pShaderSourceFactory = m_pShaderSourceFactory;
		rendererCreateInfo.pDevice = m_pDevice;
		rendererCreateInfo.pImmediateContext = m_pImmediateContext;
		rendererCreateInfo.pDeferredContexts = m_pDeferredContexts;
		rendererCreateInfo.pSwapChain = m_pSwapChain;
		rendererCreateInfo.pImGui = m_pImGui;
		rendererCreateInfo.BackBufferWidth = m_pSwapChain->GetDesc().Width;
		rendererCreateInfo.BackBufferHeight = m_pSwapChain->GetDesc().Height;

		// If your RendererCreateInfo expects IAssetManager*, AssetManagerImpl should be compatible (inherits IAssetManager).
		rendererCreateInfo.pAssetManager = m_pAssetManager.get();

		m_pRenderer->Initialize(rendererCreateInfo);

		// 3) RenderScene
		m_pRenderScene = std::make_unique<RenderScene>();

		// ------------------------------------------------------------
		// Default 1x1 textures
		// ------------------------------------------------------------
		auto PackRGBA8 = [](uint8 r, uint8 g, uint8 b, uint8 a) -> uint32
		{
			return (uint32(r) << 0) | (uint32(g) << 8) | (uint32(b) << 16) | (uint32(a) << 24);
		};

		auto Create1x1Texture = [&](const char* name, uint32 rgba, RefCntAutoPtr<ITexture>& outTex) -> bool
		{
			TextureDesc desc = {};
			desc.Name = name;
			desc.Type = RESOURCE_DIM_TEX_2D;
			desc.Width = 1;
			desc.Height = 1;
			desc.MipLevels = 1;
			desc.Format = TEX_FORMAT_RGBA8_UNORM;
			desc.Usage = USAGE_IMMUTABLE;
			desc.BindFlags = BIND_SHADER_RESOURCE;

			TextureSubResData sub = {};
			sub.pData = &rgba;
			sub.Stride = sizeof(uint32);

			TextureData data = {};
			data.pSubResources = &sub;
			data.NumSubresources = 1;

			m_pDevice->CreateTexture(desc, &data, &outTex);
			return (outTex != nullptr);
		};

		Create1x1Texture("DefaultWhite1x1", PackRGBA8(255, 255, 255, 255), m_DefaultTextures.White);
		Create1x1Texture("DefaultBlack1x1", PackRGBA8(0, 0, 0, 255), m_DefaultTextures.Black);
		Create1x1Texture("DefaultNormal1x1", PackRGBA8(128, 128, 255, 255), m_DefaultTextures.Normal);
		Create1x1Texture("DefaultMR1x1", PackRGBA8(0, 255, 0, 255), m_DefaultTextures.MetallicRoughness);
		Create1x1Texture("DefaultAO1x1", PackRGBA8(255, 255, 255, 255), m_DefaultTextures.AO);
		Create1x1Texture("DefaultEmissive1x1", PackRGBA8(0, 0, 0, 255), m_DefaultTextures.Emissive);

		ensureResourceStateSRV(m_DefaultTextures.White.RawPtr());
		ensureResourceStateSRV(m_DefaultTextures.Black.RawPtr());
		ensureResourceStateSRV(m_DefaultTextures.Normal.RawPtr());
		ensureResourceStateSRV(m_DefaultTextures.MetallicRoughness.RawPtr());
		ensureResourceStateSRV(m_DefaultTextures.AO.RawPtr());
		ensureResourceStateSRV(m_DefaultTextures.Emissive.RawPtr());

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
		// Models
		// ------------------------------------------------------------
		LoadMesh(
			"C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/floor/FbxFloor.fbx",
			{ -2.0f, -0.5f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, false);

		LoadMesh(
			"C:/Dev/ShizenEngine/ShizenEngine/Assets/Grass/chinese-fountain-grass/source/untitled/Grass.fbx",
			{ 0.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });

		//LoadMesh(
		//	"C:/Dev/ShizenEngine/ShizenEngine/Assets/Grass/clovers-plants-foliage/source/Clovers.fbx",
		//	{ 2.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });

		//LoadMesh(
		//	"C:/Dev/ShizenEngine/ShizenEngine/Assets/Grass/grass0/scene.gltf",
		//	{ -4.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 0.01f, 0.01f, 0.01f });

		/*LoadMesh(
			"C:/Dev/ShizenEngine/ShizenEngine/Assets/Grass/grass-free-download/source/grass.fbx",
			{ -2.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 0.01f, 0.01f, 0.01f });

		LoadMesh(
			"C:/Dev/ShizenEngine/ShizenEngine/Assets/Grass/cgrass-lowpoly/source/grass_cereale.fbx",
			{ 0.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 0.01f, 0.01f, 0.01f });

		LoadMesh(
			"C:/Dev/ShizenEngine/ShizenEngine/Assets/Grass/grass-vegitation-mix/source/Grass_Vegitation_mix.fbx",
			{ 2.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 0.01f, 0.01f, 0.01f });

		LoadMesh(
			"C:/Dev/ShizenEngine/ShizenEngine/Assets/Grass/lowpoly-plant/source/grass3.fbx",
			{ 4.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 0.01f, 0.01f, 0.01f });*/


		// ------------------------------------------------------------
		// Load mesh paths + spawn as grid
		// ------------------------------------------------------------
		std::vector<const char*> meshPaths;
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/AnisotropyBarnLamp/glTF/AnisotropyBarnLamp.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/BoomBoxWithAxes/glTF/BoomBoxWithAxes.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/CesiumMan/glTF/CesiumMan.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/DamagedHelmet/glTF/DamagedHelmet.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/DamagedHelmet/DamagedHelmet.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/FlightHelmet/glTF/FlightHelmet.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/GlamVelvetSofa/glTF/GlamVelvetSofa.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/IridescenceAbalone/glTF/IridescenceAbalone.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/IridescenceMetallicSpheres/glTF/IridescenceMetallicSpheres.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/IridescentDishWithOlives/glTF/IridescentDishWithOlives.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf");
		meshPaths.push_back("C:/Dev/ShizenEngine/ShizenEngine/Assets/Basic/ToyCar/glTF/ToyCar.gltf");

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

			m_pRenderScene->UpdateObjectTransform(m.ObjectId, Matrix4x4::TRS(m.Position, rot, m.Scale));
		}

		m_pRenderScene->UpdateLight(m_GlobalLightHandle, m_GlobalLight);
	}

	void ShizenEngine::ReleaseSwapChainBuffers()
	{
		SampleBase::ReleaseSwapChainBuffers();
		if (m_pRenderer)
		{
			m_pRenderer->ReleaseSwapChainBuffers();
		}
	}

	void ShizenEngine::WindowResize(uint32 Width, uint32 Height)
	{
		SampleBase::WindowResize(Width, Height);
		m_Camera.SetProjAttribs(
			m_Camera.GetProjAttribs().NearClipPlane,
			m_Camera.GetProjAttribs().FarClipPlane,
			static_cast<float>(Width) / Height,
			PI / 4.0f,
			SURFACE_TRANSFORM_IDENTITY);
		m_pRenderer->OnResize(Width, Height);
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

	// ------------------------------------------------------------
	// Mesh spawn
	// ------------------------------------------------------------

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

			// Register + load through new AssetManagerImpl
			entry.MeshRef = registerStaticMeshPath(entry.Path);
			entry.MeshID = makeAssetIDFromPath(AssetTypeTraits<StaticMeshAsset>::TypeID, entry.Path);
			entry.MeshPtr = loadStaticMeshBlocking(entry.MeshRef);

			const StaticMeshAsset* cpuMesh = entry.MeshPtr.Get();
			if (!cpuMesh)
			{
				std::cout << "Load Failed: " << entry.Path << std::endl;
				continue;
			}

			const Box& bounds = cpuMesh->GetBounds();
			const float uniform = computeUniformScaleToFitUnitCube(bounds, 1.0f);
			entry.Scale = float3(uniform, uniform, uniform);

			// GPU mesh
			entry.MeshHandle = m_pRenderer->CreateStaticMesh(*cpuMesh);
			if (!entry.MeshHandle.IsValid())
			{
				std::cout << "CreateStaticMesh failed: " << entry.Path << std::endl;
				continue;
			}

			entry.Position = float3(
				startX + static_cast<float>(c) * spacingX,
				startY + static_cast<float>(r) * spacingY,
				startZ + static_cast<float>(r) * spacingZ);

			entry.BaseRotation = float3(0, 0, 0);

			entry.RotateAxis = 1;
			entry.RotateSpeed = 0.6f + 0.2f * static_cast<float>(i % 5);

			std::vector<MaterialInstance> materials;
			for (const MaterialInstanceAsset& matInstanceAsset : cpuMesh->GetMaterialSlots())
			{
				materials.push_back(CreateMaterialInstanceFromAsset(matInstanceAsset));
			}

			entry.ObjectId = m_pRenderScene->AddObject(
				entry.MeshHandle,
				materials,
				Matrix4x4::TRS(entry.Position, entry.BaseRotation, entry.Scale));

			m_Loaded.push_back(std::move(entry));
		}
	}

} // namespace shz
