// Renderer.cpp
#include "pch.h"
#include "Renderer.h"
#include "ViewFamily.h"

#include "Engine/AssetRuntime/Public/AssetManager.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"
#include "Engine/AssetRuntime/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"

#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Tools/Image/Public/TextureUtilities.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	// ============================================================
	// Renderer
	// ============================================================

	bool Renderer::Initialize(const RendererCreateInfo& createInfo)
	{
		m_CreateInfo = createInfo;
		m_pAssetManager = m_CreateInfo.pAssetManager;

		if (!m_CreateInfo.pDevice || !m_CreateInfo.pImmediateContext || !m_CreateInfo.pSwapChain || !m_pAssetManager)
			return false;

		const auto& SCDesc = m_CreateInfo.pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : SCDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : SCDesc.Height;

		// NOTE: hard-coded shader root (same as your current setup)
		m_CreateInfo.pEngineFactory->CreateDefaultShaderSourceStreamFactory(
			"C:/Dev/ShizenEngine/Engine/Renderer/Shaders",
			&m_pShaderSourceFactory);

		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::FrameConstants), "Frame constants CB", &m_pFrameCB);
		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ObjectConstants), "Object constants CB", &m_pObjectCB);

		if (!CreateBasicPSO())
			return false;

		// Default sampler
		{
			SamplerDesc SamDesc = {};
			SamDesc.MinFilter = FILTER_TYPE_LINEAR;
			SamDesc.MagFilter = FILTER_TYPE_LINEAR;
			SamDesc.MipFilter = FILTER_TYPE_LINEAR;
			SamDesc.AddressU = TEXTURE_ADDRESS_WRAP;
			SamDesc.AddressV = TEXTURE_ADDRESS_WRAP;
			SamDesc.AddressW = TEXTURE_ADDRESS_WRAP;

			m_CreateInfo.pDevice->CreateSampler(SamDesc, &m_pDefaultSampler);
			ASSERT(m_pDefaultSampler, "Failed to create default sampler.");
		}

		// Default material instance (renderer-owned)
		{
			MaterialInstance mat{};
			mat.OverrideBaseColorFactor(float3(1.0f, 1.0f, 1.0f));
			mat.OverrideOpacity(1.0f);
			mat.OverrideAlphaMode(MATERIAL_ALPHA_OPAQUE);
			mat.OverrideRoughness(0.5f);
			mat.OverrideMetallic(0.0f);

			m_DefaultMaterial = MaterialHandle{ m_NextMaterialId++ };
			m_MaterialTable[m_DefaultMaterial] = mat;
		}

		return true;
	}

	void Renderer::Cleanup()
	{
		m_pBasicSRB.Release();
		m_pBasicPSO.Release();

		m_pFrameCB.Release();
		m_pObjectCB.Release();

		m_pShaderSourceFactory.Release();
		m_pDefaultSampler.Release();

		m_MeshTable.clear();
		m_TextureTable.clear();
		m_TexAssetToGpuHandle.clear();
		m_MaterialTable.clear();
		m_MatRenderDataTable.clear();

		m_NextMeshId = 1;
		m_NextTexId = 1;
		m_NextMaterialId = 1;

		m_DefaultMaterial = {};
		m_pAssetManager = nullptr;

		m_CreateInfo = {};
		m_Width = m_Height = 0;
	}

	void Renderer::OnResize(uint32 width, uint32 height)
	{
		m_Width = width;
		m_Height = height;
	}

	void Renderer::BeginFrame() {}
	void Renderer::EndFrame() {}

	// ============================================================
	// GPU resource creation (from AssetManager-owned CPU assets)
	// ============================================================

	TextureHandle Renderer::CreateTextureGPU(TextureAssetHandle h)
	{
		if (!h.IsValid())
			return {};

		// cache hit: same texture asset -> same GPU texture handle
		auto it = m_TexAssetToGpuHandle.find(h);
		if (it != m_TexAssetToGpuHandle.end())
			return it->second;

		const TextureAsset& texAsset = m_pAssetManager->GetTexture(h);

		TextureLoadInfo loadInfo = texAsset.BuildTextureLoadInfo();

		RefCntAutoPtr<ITexture> pTex;
		CreateTextureFromFile(texAsset.GetSourcePath().c_str(), loadInfo, m_CreateInfo.pDevice, &pTex);
		if (!pTex)
			return {};

		ITextureView* pSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		if (pSRV && m_pDefaultSampler)
			pSRV->SetSampler(m_pDefaultSampler);

		TextureHandle gpuHandle{ m_NextTexId++ };
		m_TextureTable[gpuHandle] = pTex;
		m_TexAssetToGpuHandle.emplace(h, gpuHandle);

		return gpuHandle;
	}

	MaterialHandle Renderer::CreateMaterialInstance(MaterialAssetHandle h)
	{
		if (!h.IsValid())
			return m_DefaultMaterial;

		const MaterialAsset& matAsset = m_pAssetManager->GetMaterial(h);

		MaterialInstance inst{};

		// -------------------------
		// Parameters
		// -------------------------
		const auto& P = matAsset.GetParams();

		inst.OverrideBaseColorFactor(float3(P.BaseColor.x, P.BaseColor.y, P.BaseColor.z));
		inst.OverrideOpacity(P.BaseColor.w);

		inst.OverrideMetallic(P.Metallic);
		inst.OverrideRoughness(P.Roughness);

		inst.OverrideNormalScale(P.NormalScale);
		inst.OverrideOcclusionStrength(P.Occlusion);

		inst.OverrideEmissiveFactor(P.EmissiveColor * P.EmissiveIntensity);

		inst.OverrideAlphaCutoff(P.AlphaCutoff);

		// -------------------------
		// Alpha Mode
		// -------------------------
		switch (matAsset.GetOptions().AlphaMode)
		{
		case MaterialAsset::ALPHA_OPAQUE: inst.OverrideAlphaMode(MATERIAL_ALPHA_OPAQUE); break;
		case MaterialAsset::ALPHA_MASK:   inst.OverrideAlphaMode(MATERIAL_ALPHA_MASK);   break;
		case MaterialAsset::ALPHA_BLEND:  inst.OverrideAlphaMode(MATERIAL_ALPHA_BLEND);  break;
		default:                          inst.OverrideAlphaMode(MATERIAL_ALPHA_OPAQUE); break;
		}

		// -------------------------
		// Textures
		//
		// NOTE:
		// MaterialAsset currently stores TextureAsset by value.
		// We register those TextureAssets into AssetManager and then
		// create/cached GPU textures from TextureAssetHandle.
		// -------------------------
		if (matAsset.HasTexture(MaterialAsset::TEX_ALBEDO))
		{
			TextureAssetHandle hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MaterialAsset::TEX_ALBEDO));
			inst.OverrideBaseColorTexture(hTex);
		}

		if (matAsset.HasTexture(MaterialAsset::TEX_NORMAL))
		{
			TextureAssetHandle hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MaterialAsset::TEX_NORMAL));
			inst.OverrideNormalTexture(hTex);
		}

		if (matAsset.HasTexture(MaterialAsset::TEX_ORM))
		{
			TextureAssetHandle hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MaterialAsset::TEX_ORM));
			inst.OverrideMetallicRoughnessTexture(hTex);
		}

		if (matAsset.HasTexture(MaterialAsset::TEX_EMISSIVE))
		{
			TextureAssetHandle hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MaterialAsset::TEX_EMISSIVE));
			inst.OverrideEmissiveTexture(hTex);
		}

		MaterialHandle hInst{ m_NextMaterialId++ };
		m_MaterialTable[hInst] = inst;

		return hInst;
	}

	MeshHandle Renderer::CreateStaticMesh(StaticMeshAssetHandle h)
	{
		if (!h.IsValid())
			return {};

		const StaticMeshAsset& meshAsset = m_pAssetManager->GetStaticMesh(h);

		MeshHandle handle{ m_NextMeshId++ };

		StaticMeshRenderData outMesh;

		struct SimpleVertex
		{
			float3 Pos;
			float2 UV;
			float3 Normal;
			float3 Tangent;
		};

		const uint32 vtxCount = meshAsset.GetVertexCount();
		outMesh.NumVertices = vtxCount;
		outMesh.VertexStride = sizeof(SimpleVertex);
		outMesh.LocalBounds = meshAsset.GetBounds();

		const auto& positions = meshAsset.GetPositions();
		const auto& normals = meshAsset.GetNormals();
		const auto& tangents = meshAsset.GetTangents();
		const auto& uvs = meshAsset.GetTexCoords();

		std::vector<SimpleVertex> vbCPU;
		vbCPU.resize(vtxCount);

		for (uint32 i = 0; i < vtxCount; ++i)
		{
			SimpleVertex v{};
			v.Pos = positions[i];
			v.UV = uvs[i];
			v.Normal = normals[i];
			v.Tangent = tangents[i];
			vbCPU[i] = v;
		}

		// VB
		{
			BufferDesc VBDesc;
			VBDesc.Name = "StaticMesh VB";
			VBDesc.Usage = USAGE_IMMUTABLE;
			VBDesc.BindFlags = BIND_VERTEX_BUFFER;
			VBDesc.Size = static_cast<uint64>(vbCPU.size() * sizeof(SimpleVertex));

			BufferData VBData;
			VBData.pData = vbCPU.data();
			VBData.DataSize = static_cast<uint64>(vbCPU.size() * sizeof(SimpleVertex));

			m_CreateInfo.pDevice->CreateBuffer(VBDesc, &VBData, &outMesh.VertexBuffer);
			if (!outMesh.VertexBuffer)
				return {};
		}

		outMesh.Sections.clear();

		const bool useU32 = (meshAsset.GetIndexType() == VT_UINT32);
		const auto& idx32 = meshAsset.GetIndicesU32();
		const auto& idx16 = meshAsset.GetIndicesU16();

		auto CreateSectionIB = [&](MeshSection& sec, const void* pIndexData, uint64 indexDataBytes) -> bool
		{
			BufferDesc IBDesc;
			IBDesc.Name = "StaticMesh IB";
			IBDesc.Usage = USAGE_IMMUTABLE;
			IBDesc.BindFlags = BIND_INDEX_BUFFER;
			IBDesc.Size = indexDataBytes;

			BufferData IBData;
			IBData.pData = pIndexData;
			IBData.DataSize = indexDataBytes;

			m_CreateInfo.pDevice->CreateBuffer(IBDesc, &IBData, &sec.IndexBuffer);
			return (sec.IndexBuffer != nullptr);
		};

		const auto& assetSections = meshAsset.GetSections();
		if (!assetSections.empty())
		{
			outMesh.Sections.reserve(assetSections.size());

			int slotIndex = 0;
			for (const StaticMeshAsset::Section& asec : assetSections)
			{
				if (asec.IndexCount == 0)
					continue;

				MeshSection sec{};
				sec.NumIndices = asec.IndexCount;
				sec.IndexType = useU32 ? VT_UINT32 : VT_UINT16;
				sec.LocalBounds = asec.LocalBounds;
				sec.StartIndex = 0;

				const uint32 first = asec.FirstIndex;
				const uint32 count = asec.IndexCount;

				const void* pData = nullptr;
				uint64 bytes = 0;

				if (useU32)
				{
					pData = idx32.empty() ? nullptr : static_cast<const void*>(idx32.data() + first);
					bytes = static_cast<uint64>(count) * sizeof(uint32);
				}
				else
				{
					pData = idx16.empty() ? nullptr : static_cast<const void*>(idx16.data() + first);
					bytes = static_cast<uint64>(count) * sizeof(uint16);
				}

				if (!CreateSectionIB(sec, pData, bytes))
					return {};

				// section material: register MaterialAsset (by value) -> create renderer instance
				const MaterialAsset& slotMat = meshAsset.GetMaterialSlot(asec.MaterialSlot);
				MaterialAssetHandle hMatAsset = m_pAssetManager->RegisterMaterial(slotMat, slotIndex++);
				sec.Material = CreateMaterialInstance(hMatAsset);

				outMesh.Sections.push_back(sec);
			}
		}
		else
		{
			// Fallback: one section over full indices
			MeshSection sec{};
			sec.NumIndices = meshAsset.GetIndexCount();
			sec.IndexType = useU32 ? VT_UINT32 : VT_UINT16;
			sec.StartIndex = 0;

			if (!CreateSectionIB(sec, meshAsset.GetIndexData(), meshAsset.GetIndexDataSizeBytes()))
				return {};

			sec.Material = m_DefaultMaterial;
			outMesh.Sections.push_back(sec);
		}

		m_MeshTable.emplace(handle, std::move(outMesh));
		return handle;
	}

	// ============================================================
	// Render
	// ============================================================

	void Renderer::Render(const RenderScene& scene, const ViewFamily& viewFamily)
	{
		auto* pCtx = m_CreateInfo.pImmediateContext.RawPtr();
		auto* pSC = m_CreateInfo.pSwapChain.RawPtr();
		if (!pCtx || !pSC)
			return;

		ITextureView* pRTV = pSC->GetCurrentBackBufferRTV();
		ITextureView* pDSV = pSC->GetDepthBufferDSV();
		pCtx->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		const float ClearColor[] = { 0.350f, 0.350f, 0.350f, 1.0f };
		pCtx->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
		pCtx->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		const auto& view = viewFamily.Views[0];
		Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix; // your convention

		// Frame CB update (once per frame)
		{
			MapHelper<hlsl::FrameConstants> CBData(pCtx, m_pFrameCB, MAP_WRITE, MAP_FLAG_DISCARD);
			CBData->ViewProj = viewProj;
		}

		if (!m_pBasicPSO)
			return;

		for (const auto& obj : scene.GetObjects())
		{
			const RenderScene::RenderObject& renderObject = obj.second;
			const StaticMeshRenderData& mesh = m_MeshTable.at(renderObject.meshHandle);

			// Object CB update (per object)
			{
				MapHelper<hlsl::ObjectConstants> CBData(pCtx, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
				CBData->World = renderObject.transform;
			}

			IBuffer* pVBs[] = { mesh.VertexBuffer };
			uint64 Offsets[] = { 0 };
			pCtx->SetVertexBuffers(
				0, 1, pVBs, Offsets,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
				SET_VERTEX_BUFFERS_FLAG_RESET);

			MaterialHandle lastMat{};

			for (const auto& section : mesh.Sections)
			{
				pCtx->SetIndexBuffer(section.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				const MaterialHandle matHandle = section.Material;
				MaterialRenderData* matRD = GetOrCreateMaterialRenderData(matHandle);
				ASSERT(matRD != nullptr, "Material GPU Data is null.");

				if (matHandle.Id != lastMat.Id)
				{
					pCtx->SetPipelineState(matRD->pPSO);
					pCtx->CommitShaderResources(matRD->pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
					lastMat = matHandle;
				}

				DrawIndexedAttribs DIA;
				DIA.NumIndices = section.NumIndices;
				DIA.IndexType = (section.IndexType == VT_UINT16) ? VT_UINT16 : VT_UINT32;
				DIA.Flags = DRAW_FLAG_VERIFY_ALL;
				DIA.FirstIndexLocation = section.StartIndex;

				pCtx->DrawIndexed(DIA);
			}
		}
	}

	// ============================================================
	// MaterialRenderData (SRB/PSO bindings)
	// ============================================================

	MaterialRenderData* Renderer::GetOrCreateMaterialRenderData(MaterialHandle h)
	{
		// 0) invalid guard
		if (!h.IsValid())
			return nullptr;

		// 1) cache hit
		auto it = m_MatRenderDataTable.find(h);
		if (it != m_MatRenderDataTable.end())
			return &it->second;

		// 2) material instance lookup
		auto mit = m_MaterialTable.find(h);
		if (mit == m_MaterialTable.end())
		{
			ASSERT(false, "MaterialHandle not found in m_MaterialTable.");
			return nullptr;
		}
		const MaterialInstance& MatInst = mit->second;

		if (!m_pBasicPSO)
			return nullptr;

		// 3) fallback white SRV (create once)
		static RefCntAutoPtr<ITextureView> s_WhiteSRV;
		if (!s_WhiteSRV)
		{
			auto* pDevice = m_CreateInfo.pDevice.RawPtr();
			if (!pDevice)
				return nullptr;

			const uint32 whiteRGBA = 0xFFFFFFFFu;

			TextureDesc TexDesc = {};
			TexDesc.Name = "DefaultWhite1x1";
			TexDesc.Type = RESOURCE_DIM_TEX_2D;
			TexDesc.Width = 1;
			TexDesc.Height = 1;
			TexDesc.MipLevels = 1;
			TexDesc.Format = TEX_FORMAT_RGBA8_UNORM;
			TexDesc.Usage = USAGE_IMMUTABLE;
			TexDesc.BindFlags = BIND_SHADER_RESOURCE;

			TextureSubResData Sub = {};
			Sub.pData = &whiteRGBA;
			Sub.Stride = sizeof(uint32);

			TextureData InitData = {};
			InitData.pSubResources = &Sub;
			InitData.NumSubresources = 1;

			RefCntAutoPtr<ITexture> pWhiteTex;
			pDevice->CreateTexture(TexDesc, &InitData, &pWhiteTex);
			if (!pWhiteTex)
				return nullptr;

			s_WhiteSRV = pWhiteTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
			if (!s_WhiteSRV)
				return nullptr;

			if (m_pDefaultSampler)
				s_WhiteSRV->SetSampler(m_pDefaultSampler);
		}

		// 4) resolve effective parameters (MaterialInstance getters with fallback)
		const float3 BaseColor = MatInst.GetBaseColorFactor(float3(1, 1, 1));
		const float  Opacity = MatInst.GetOpacity(1.0f);
		const float  Metallic = MatInst.GetMetallic(0.0f);
		const float  Roughness = MatInst.GetRoughness(0.5f);
		const float  NormalScale = MatInst.GetNormalScale(1.0f);
		const float  Occlusion = MatInst.GetOcclusionStrength(1.0f);
		const float3 Emissive = MatInst.GetEmissiveFactor(float3(0, 0, 0));
		const auto   AlphaMode = MatInst.GetAlphaMode(MATERIAL_ALPHA_OPAQUE);
		const float  AlphaCutoff = MatInst.GetAlphaCutoff(0.5f);

		// 5) create RD
		MaterialRenderData RD = {};
		RD.Handle = h;

		RD.RenderQueue = MaterialRenderData::GetQueueFromAlphaMode(AlphaMode);

		RD.BaseColor = float4(BaseColor.x, BaseColor.y, BaseColor.z, Opacity);
		RD.Metallic = Metallic;
		RD.Roughness = Roughness;
		RD.NormalScale = NormalScale;
		RD.OcclusionStrength = Occlusion;
		RD.Emissive = Emissive;
		RD.AlphaCutoff = AlphaCutoff;

		RD.pPSO = m_pBasicPSO;

		RD.pPSO->CreateShaderResourceBinding(&RD.pSRB, true /*InitStaticResources*/);
		if (!RD.pSRB)
			return nullptr;

		// 6) bind CBs (VS)
		auto BindVS_CB = [&](const char* name, IBuffer* pCB) -> bool
		{
			if (!pCB) return false;
			if (auto* Var = RD.pSRB->GetVariableByName(SHADER_TYPE_VERTEX, name))
			{
				Var->Set(pCB);
				return true;
			}
			return false;
		};

		if (!BindVS_CB("FRAME_CONSTANTS", m_pFrameCB))
			return nullptr;

		if (!BindVS_CB("OBJECT_CONSTANTS", m_pObjectCB))
			return nullptr;

		// 7) base color texture bind (PS)
		ITextureView* pBaseColorSRV = s_WhiteSRV.RawPtr();

		const TextureAssetHandle baseColorAssetHandle = MatInst.GetBaseColorTextureOverrideOrInvalid();
		if (baseColorAssetHandle.IsValid())
		{
			TextureHandle texGPUHandle = CreateTextureGPU(baseColorAssetHandle);
			RefCntAutoPtr<ITexture> pTex = m_TextureTable[texGPUHandle];
			if (auto* pSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))
			{
				if (m_pDefaultSampler)
					pSRV->SetSampler(m_pDefaultSampler);
				pBaseColorSRV = pSRV;
			}
		}

		if (auto* TexVar = RD.pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_BaseColorTex"))
		{
			TexVar->Set(pBaseColorSRV);
		}
		else
		{
			ASSERT(false, "PS SRB variable 'g_BaseColorTex' not found.");
			return nullptr;
		}

		// 8) cache insert
		auto [insIt, ok] = m_MatRenderDataTable.emplace(h, std::move(RD));
		(void)ok;
		return &insIt->second;
	}

	// ============================================================
	// PSO
	// ============================================================

	bool Renderer::CreateBasicPSO()
	{
		auto* pDevice = m_CreateInfo.pDevice.RawPtr();
		if (!pDevice)
			return false;

		const auto& SCDesc = m_CreateInfo.pSwapChain->GetDesc();

		GraphicsPipelineStateCreateInfo PSOCreateInfo = {};
		PSOCreateInfo.PSODesc.Name = "Debug Basic PSO";
		PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		// Graphics pipeline states
		auto& GP = PSOCreateInfo.GraphicsPipeline;
		GP.NumRenderTargets = 1;
		GP.RTVFormats[0] = SCDesc.ColorBufferFormat;
		GP.DSVFormat = SCDesc.DepthBufferFormat;
		GP.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		GP.RasterizerDesc.CullMode = CULL_MODE_NONE;
		GP.RasterizerDesc.FrontCounterClockwise = true;
		GP.DepthStencilDesc.DepthEnable = true;

		// Input Layout
		LayoutElement LayoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // ATTRIB0 : float3
			LayoutElement{1, 0, 2, VT_FLOAT32, false}, // ATTRIB1 : float2
			LayoutElement{2, 0, 3, VT_FLOAT32, false}, // ATTRIB2 : float3
			LayoutElement{3, 0, 3, VT_FLOAT32, false}, // ATTRIB3 : float3
		};
		GP.InputLayout.LayoutElements = LayoutElems;
		GP.InputLayout.NumElements = _countof(LayoutElems);

		// Shaders
		ShaderCreateInfo ShaderCI = {};
		ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		ShaderCI.EntryPoint = "main";
		ShaderCI.pShaderSourceStreamFactory = m_pShaderSourceFactory;
		ShaderCI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

		RefCntAutoPtr<IShader> pVS;
		{
			ShaderCI.Desc = {};
			ShaderCI.Desc.Name = "Basic VS";
			ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			ShaderCI.FilePath = "Basic.vsh";
			ShaderCI.Desc.UseCombinedTextureSamplers = true;

			pDevice->CreateShader(ShaderCI, &pVS);
			if (!pVS)
				return false;
		}

		RefCntAutoPtr<IShader> pPS;
		{
			ShaderCI.Desc = {};
			ShaderCI.Desc.Name = "Basic PS";
			ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			ShaderCI.FilePath = "Basic.psh";
			ShaderCI.Desc.UseCombinedTextureSamplers = true;

			pDevice->CreateShader(ShaderCI, &pPS);
			if (!pPS)
				return false;
		}

		PSOCreateInfo.pVS = pVS;
		PSOCreateInfo.pPS = pPS;

		// Resource layout
		PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{ SHADER_TYPE_VERTEX, "FRAME_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE  },
			{ SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC  },
			{ SHADER_TYPE_PIXEL,  "g_BaseColorTex",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE  },
		};
		PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
		PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pBasicPSO);
		if (!m_pBasicPSO)
			return false;

		// Optional: create a shared SRB for static resources (not required per-material)
		m_pBasicPSO->CreateShaderResourceBinding(&m_pBasicSRB, true /*InitStaticResources*/);
		if (!m_pBasicSRB)
			return false;

		// Bind constant buffers on shared SRB (optional / debugging)
		auto BindCB = [&](const char* name, IBuffer* pCB) -> bool
		{
			if (!pCB) return false;
			if (auto* Var = m_pBasicSRB->GetVariableByName(SHADER_TYPE_VERTEX, name))
			{
				Var->Set(pCB);
				return true;
			}
			return false;
		};

		if (!BindCB("FRAME_CONSTANTS", m_pFrameCB))
			return false;

		if (!BindCB("OBJECT_CONSTANTS", m_pObjectCB))
			return false;

		return true;
	}

	// ============================================================
	// Cube mesh (same behavior as your current sample)
	// ============================================================

	MeshHandle Renderer::CreateCubeMesh()
	{
		MeshHandle handle{ m_NextMeshId++ };

		StaticMeshRenderData outMesh;

		struct SimpleVertex
		{
			float3 Pos;
			float2 UV;
			float3 Normal;
			float3 Tangent;
		};

		static const SimpleVertex Verts[] =
		{
			// -Z
			{{-0.5f,-0.5f,-0.5f},{0,1},{0,0,-1},{+1,0,0}},
			{{+0.5f,-0.5f,-0.5f},{1,1},{0,0,-1},{+1,0,0}},
			{{+0.5f,+0.5f,-0.5f},{1,0},{0,0,-1},{+1,0,0}},
			{{-0.5f,+0.5f,-0.5f},{0,0},{0,0,-1},{+1,0,0}},

			// +Z
			{{-0.5f,-0.5f,+0.5f},{0,1},{0,0,+1},{+1,0,0}},
			{{+0.5f,-0.5f,+0.5f},{1,1},{0,0,+1},{+1,0,0}},
			{{+0.5f,+0.5f,+0.5f},{1,0},{0,0,+1},{+1,0,0}},
			{{-0.5f,+0.5f,+0.5f},{0,0},{0,0,+1},{+1,0,0}},

			// -X
			{{-0.5f,-0.5f,+0.5f},{0,1},{-1,0,0},{0,0,-1}},
			{{-0.5f,-0.5f,-0.5f},{1,1},{-1,0,0},{0,0,-1}},
			{{-0.5f,+0.5f,-0.5f},{1,0},{-1,0,0},{0,0,-1}},
			{{-0.5f,+0.5f,+0.5f},{0,0},{-1,0,0},{0,0,-1}},

			// +X
			{{+0.5f,-0.5f,-0.5f},{0,1},{+1,0,0},{0,0,+1}},
			{{+0.5f,-0.5f,+0.5f},{1,1},{+1,0,0},{0,0,+1}},
			{{+0.5f,+0.5f,+0.5f},{1,0},{+1,0,0},{0,0,+1}},
			{{+0.5f,+0.5f,-0.5f},{0,0},{+1,0,0},{0,0,+1}},

			// -Y
			{{-0.5f,-0.5f,+0.5f},{0,1},{0,-1,0},{+1,0,0}},
			{{+0.5f,-0.5f,+0.5f},{1,1},{0,-1,0},{+1,0,0}},
			{{+0.5f,-0.5f,-0.5f},{1,0},{0,-1,0},{+1,0,0}},
			{{-0.5f,-0.5f,-0.5f},{0,0},{0,-1,0},{+1,0,0}},

			// +Y
			{{-0.5f,+0.5f,-0.5f},{0,1},{0,+1,0},{+1,0,0}},
			{{+0.5f,+0.5f,-0.5f},{1,1},{0,+1,0},{+1,0,0}},
			{{+0.5f,+0.5f,+0.5f},{1,0},{0,+1,0},{+1,0,0}},
			{{-0.5f,+0.5f,+0.5f},{0,0},{0,+1,0},{+1,0,0}},
		};

		static const uint32 Indices[] =
		{
			0,2,1, 0,3,2,       // -Z
			4,5,6, 4,6,7,       // +Z
			8,10,9, 8,11,10,    // -X
			12,14,13, 12,15,14, // +X
			16,18,17, 16,19,18, // -Y
			20,22,21, 20,23,22  // +Y
		};

		outMesh.NumVertices = _countof(Verts);
		outMesh.VertexStride = sizeof(SimpleVertex);
		outMesh.LocalBounds = { {-0.5f,-0.5f,-0.5f}, {+0.5f,+0.5f,+0.5f} };

		// VB
		{
			BufferDesc VBDesc;
			VBDesc.Name = "Cube VB";
			VBDesc.Usage = USAGE_IMMUTABLE;
			VBDesc.BindFlags = BIND_VERTEX_BUFFER;
			VBDesc.Size = sizeof(Verts);

			BufferData VBData;
			VBData.pData = Verts;
			VBData.DataSize = sizeof(Verts);

			m_CreateInfo.pDevice->CreateBuffer(VBDesc, &VBData, &outMesh.VertexBuffer);
			if (!outMesh.VertexBuffer)
				return {};
		}

		// IB (one section)
		MeshSection sec = {};
		sec.NumIndices = _countof(Indices);
		sec.IndexType = VT_UINT32;
		sec.StartIndex = 0;

		{
			BufferDesc IBDesc;
			IBDesc.Name = "Cube IB";
			IBDesc.Usage = USAGE_IMMUTABLE;
			IBDesc.BindFlags = BIND_INDEX_BUFFER;
			IBDesc.Size = sizeof(Indices);

			BufferData IBData;
			IBData.pData = Indices;
			IBData.DataSize = sizeof(Indices);

			m_CreateInfo.pDevice->CreateBuffer(IBDesc, &IBData, &sec.IndexBuffer);
			if (!sec.IndexBuffer)
				return {};
		}

		sec.Material = m_DefaultMaterial;

		outMesh.Sections.clear();
		outMesh.Sections.push_back(sec);

		m_MeshTable.emplace(handle, std::move(outMesh));
		return handle;
	}

} // namespace shz
