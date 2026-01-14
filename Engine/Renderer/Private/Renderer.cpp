#include "pch.h"
#include "Renderer.h"
#include "ViewFamily.h"
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

	bool Renderer::Initialize(const RendererCreateInfo& createInfo)
	{
		m_CreateInfo = createInfo;

		if (!m_CreateInfo.pDevice || !m_CreateInfo.pImmediateContext || !m_CreateInfo.pSwapChain)
			return false;

		const auto& SCDesc = m_CreateInfo.pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : SCDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : SCDesc.Height;

		m_CreateInfo.pEngineFactory->CreateDefaultShaderSourceStreamFactory("C:/Dev/ShizenEngine/Engine/Renderer/Shaders", &m_pShaderSourceFactory);

		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::FrameConstants), "Shader constants CB", &m_pFrameCB);
		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::ObjectConstants), "Shader constants CB", &m_pObjectCB);

		if (!CreateBasicPSO())
			return false;

		// Default material (magenta / white texture 없음이어도 OK)
		{
			Material mat{};
			mat.BaseColorFactor = float3(1.0f, 1.0f, 1.0f);
			mat.Opacity = 1.0f;
			mat.AlphaMode = MATERIAL_ALPHA_OPAQUE;
			mat.RoughnessFactor = 0.5f;
			mat.MetallicFactor = 0.0f;

			m_DefaultMaterial = { m_NextMaterialId++ };
			m_MaterialTable[m_DefaultMaterial] = mat;

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

		return true;
	}

	void Renderer::Cleanup()
	{
		m_pBasicPSO.Release();
		m_MeshTable.clear();
		m_NextMeshId = 1;

		m_CreateInfo = {};
		m_Width = m_Height = 0;
	}

	void Renderer::OnResize(uint32 width, uint32 height)
	{
		m_Width = width;
		m_Height = height;
	}

	void Renderer::BeginFrame()
	{

	}

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
		Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix; // 네 규약에 맞게

		// Frame CB 업데이트 (프레임 1회)
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

			// Object CB 업데이트 (오브젝트마다)
			{
				MapHelper<hlsl::ObjectConstants> CBData(pCtx, m_pObjectCB, MAP_WRITE, MAP_FLAG_DISCARD);
				CBData->World = renderObject.transform;
			}

			IBuffer* pVBs[] = { mesh.VertexBuffer };
			uint64 Offsets[] = { 0 };
			pCtx->SetVertexBuffers(
				0, 1, pVBs, Offsets,
				RESOURCE_STATE_TRANSITION_MODE_TRANSITION, // <-- VERIFY 금지
				SET_VERTEX_BUFFERS_FLAG_RESET);

			MaterialHandle lastMat{};

			for (const auto& section : mesh.Sections)
			{
				pCtx->SetIndexBuffer(section.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				MaterialHandle matHandle = section.Material;
				MaterialRenderData* matRD = GetOrCreateMaterialRenderData(matHandle);
				ASSERT(matRD != nullptr, "Material GPU Data is null.");

				if (matHandle.Id != lastMat.Id) // Handle에 비교 연산 있으면 그걸 써도 됨
				{
					pCtx->SetPipelineState(matRD->pPSO);
					pCtx->CommitShaderResources(matRD->pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
					lastMat = matHandle;
				}

				DrawIndexedAttribs DIA;
				DIA.NumIndices = section.NumIndices;
				DIA.IndexType = (section.IndexType == INDEX_TYPE_UINT16) ? VT_UINT16 : VT_UINT32;
				DIA.Flags = DRAW_FLAG_VERIFY_ALL;
				DIA.FirstIndexLocation = section.StartIndex;
				DIA.BaseVertex = section.BaseVertex;

				pCtx->DrawIndexed(DIA);
			}

		}
	}



	void Renderer::EndFrame()
	{
	}

	MaterialRenderData* Renderer::GetOrCreateMaterialRenderData(MaterialHandle h)
	{
		// 0) invalid handle 방어
		if (!h.IsValid())
			return nullptr;

		// 1) 캐시 hit
		auto it = m_MatRenderDataTable.find(h);
		if (it != m_MatRenderDataTable.end())
			return &it->second;

		// 2) Material 조회
		auto mit = m_MaterialTable.find(h);
		if (mit == m_MaterialTable.end())
		{
			ASSERT(false, "MaterialHandle not found in m_MaterialTable.");
			return nullptr;
		}
		const Material& mat = mit->second;

		// 3) Basic PSO가 없으면 생성 불가
		if (!m_pBasicPSO)
		{
			ASSERT(false, "m_pBasicPSO is null.");
			return nullptr;
		}

		// 4) fallback 1x1 white SRV (최초 1회 생성)
		//    - 셰이더가 무조건 Sample 하니까 null SRV면 터질 수 있어서 필수
		static RefCntAutoPtr<ITextureView> s_WhiteSRV;
		if (!s_WhiteSRV)
		{
			auto* pDevice = m_CreateInfo.pDevice.RawPtr();
			if (!pDevice)
				return nullptr;

			// 1x1 RGBA8 white
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
			{
				ASSERT(false, "Failed to create DefaultWhite1x1 texture.");
				return nullptr;
			}

			s_WhiteSRV = pWhiteTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
			if (!s_WhiteSRV)
			{
				ASSERT(false, "DefaultWhite1x1 SRV is null.");
				return nullptr;
			}
			s_WhiteSRV->SetSampler(m_pDefaultSampler);
		}

		// 5) MaterialRenderData 생성 (Basic 셰이더 하드코딩)
		MaterialRenderData rd{};
		rd.Handle = h;

		// RenderQueue (지금은 Basic pass라 큰 의미는 없지만 일단 채워둠)
		rd.RenderQueue = MaterialRenderData::GetQueueFromAlphaMode(mat.AlphaMode);

		// 파라미터(현재 Basic shader가 안 쓰더라도 RD에 저장해두면 이후 확장 쉬움)
		rd.BaseColor = float4(mat.BaseColorFactor.x, mat.BaseColorFactor.y, mat.BaseColorFactor.z, mat.Opacity);
		rd.Metallic = mat.MetallicFactor;
		rd.Roughness = mat.RoughnessFactor;
		rd.NormalScale = mat.NormalScale;
		rd.OcclusionStrength = mat.AmbientOcclusionStrength;
		rd.Emissive = mat.EmissiveFactor;
		rd.AlphaCutoff = mat.AlphaCutoff;

		// Basic PSO/SRB
		rd.pPSO = m_pBasicPSO;

		rd.pPSO->CreateShaderResourceBinding(&rd.pSRB, true /*InitStaticResources*/);
		if (!rd.pSRB)
		{
			ASSERT(false, "Failed to create SRB for material.");
			return nullptr;
		}

		// 6) 상수버퍼 바인딩 (VS)
		auto BindVS_CB = [&](const char* name, IBuffer* pCB) -> bool
		{
			if (!pCB) return false;
			if (auto* Var = rd.pSRB->GetVariableByName(SHADER_TYPE_VERTEX, name))
			{
				Var->Set(pCB);
				return true;
			}
			return false;
		};

		if (!BindVS_CB("FRAME_CONSTANTS", m_pFrameCB))
		{
			ASSERT(false, "Failed to bind FRAME_CONSTANTS.");
			return nullptr;
		}

		if (!BindVS_CB("OBJECT_CONSTANTS", m_pObjectCB))
		{
			ASSERT(false, "Failed to bind OBJECT_CONSTANTS.");
			return nullptr;
		}

		// 7) BaseColor 텍스처 바인딩 (PS)
		//    - Material에 BaseColorTexture가 있으면 그걸 사용
		//    - 없으면 white fallback
		ITextureView* pBaseColorSRV = s_WhiteSRV.RawPtr();

		if (mat.BaseColorTexture.IsValid())
		{
			auto tit = m_TextureTable.find(mat.BaseColorTexture);
			if (tit != m_TextureTable.end() && tit->second)
			{
				RefCntAutoPtr<ITexture> pTex = tit->second;
				if (auto* pSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))
					pBaseColorSRV = pSRV;
			}
		}

		// 셰이더 변수 이름: g_BaseColorTex
		if (auto* TexVar = rd.pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_BaseColorTex"))
		{
			TexVar->Set(pBaseColorSRV);
		}
		else
		{
			ASSERT(false, "PS SRB variable 'g_BaseColorTex' not found.");
			return nullptr;
		}

		// Combined sampler 켜둔 상태면 보통 "g_BaseColorTex_sampler"가 잡힘
		// (없어도 괜찮게 optional 처리)
		if (auto* SampVar = rd.pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_BaseColorTex_sampler"))
		{
			// 샘플러를 따로 만들고 싶으면 여기서 Set.
			// 지금은 PSO static sampler나 엔진 기본 sampler 정책이 있다면 그걸 쓰면 됨.
			// SampVar->Set(rd.pDefaultSampler); // optional
		}

		// 9) 캐시에 저장 후 포인터 반환
		auto [insIt, ok] = m_MatRenderDataTable.emplace(h, std::move(rd));
		return &insIt->second;
	}



	bool Renderer::CreateBasicPSO()
	{
		auto* pDevice = m_CreateInfo.pDevice.RawPtr();
		if (!pDevice)
			return false;

		const auto& SCDesc = m_CreateInfo.pSwapChain->GetDesc();

		GraphicsPipelineStateCreateInfo PSOCreateInfo = {};
		PSOCreateInfo.PSODesc.Name = "Debug Basic PSO";
		PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		// =========================
		// Graphics pipeline states
		// =========================
		auto& GP = PSOCreateInfo.GraphicsPipeline;
		GP.NumRenderTargets = 1;
		GP.RTVFormats[0] = SCDesc.ColorBufferFormat;
		GP.DSVFormat = SCDesc.DepthBufferFormat;
		GP.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		GP.RasterizerDesc.CullMode = CULL_MODE_NONE;
		GP.RasterizerDesc.FrontCounterClockwise = true;
		GP.DepthStencilDesc.DepthEnable = true;

		// =========================
		// Input Layout (ATTRIB0/1/2)
		// VSInput:
		//  float3 Pos     : ATTRIB0;
		//  float2 UV      : ATTRIB1;
		//  float3 Normal  : ATTRIB2;
		//  float3 Tangent : ATTRIB3;
		// =========================

		{
			float3 Pos;
			float2 UV;
			float3 Normal;
			float3 Tangent;
		};
		LayoutElement LayoutElems[] =
		{
			// Slot 0 기준
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // ATTRIB0 : float3
			LayoutElement{1, 0, 2, VT_FLOAT32, false}, // ATTRIB1 : float2
			LayoutElement{2, 0, 3, VT_FLOAT32, false}, // ATTRIB2 : float3
			LayoutElement{3, 0, 3, VT_FLOAT32, false}, // ATTRIB3 : float3
		};
		GP.InputLayout.LayoutElements = LayoutElems;
		GP.InputLayout.NumElements = _countof(LayoutElems);

		// =========================
		// Shaders
		// =========================
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
			ShaderCI.Desc.UseCombinedTextureSamplers = true; // 텍스처 안 쓰면 False여도 됨

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

		// =========================
		// Resource Layout
		// 셰이더에 텍스처/샘플러 없음.
		// 상수버퍼만 바인딩하면 됨.
		//
		// 여기서 "Variables"는 '이름 기반'으로 SRB에서 Set할 리소스를 선언하는 곳.
		// Diligent은 cbuffer의 "상수버퍼 변수명"으로 노출되는 경우가 많음:
		//   - cbuffer FRAME_CONSTANTS { FrameConstants g_FrameCB; }
		//     => 보통 "g_FrameCB" 가 변수명으로 잡힘
		//
		// 그래서 g_FrameCB / g_ObjectCB를 기본으로 넣어주고,
		// 만약 네 구현이 cbuffer 블록 이름으로 노출하면
		// 아래 GetVariableByName에서 fallback 처리해줌.
		// =========================
		PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			// Vertex shader constants
			{ SHADER_TYPE_VERTEX, "FRAME_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE  },
			{ SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC  },

			// Pixel shader texture
			{ SHADER_TYPE_PIXEL,  "g_BaseColorTex",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL,  "g_BaseColorTex_sampler",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
		PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		// =========================
		// Create PSO
		// =========================
		pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pBasicPSO);
		if (!m_pBasicPSO)
			return false;

		// =========================
		// SRB
		// =========================
		m_pBasicPSO->CreateShaderResourceBinding(&m_pBasicSRB, true /*InitStaticResources*/);
		if (!m_pBasicSRB)
			return false;

		// =========================
		// Bind constant buffers
		// =========================
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

	TextureHandle Renderer::CreateTexture(const TextureAsset& asset)
	{
		if (!asset.IsValid())
			return {};

		TextureLoadInfo loadInfo = asset.BuildTextureLoadInfo();

		RefCntAutoPtr<ITexture> pTex;
		CreateTextureFromFile(asset.GetSourcePath().c_str(), loadInfo, m_CreateInfo.pDevice, &pTex);

		ITextureView* pSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		if (pSRV && m_pDefaultSampler)
			pSRV->SetSampler(m_pDefaultSampler);

		TextureHandle handle{ m_NextTexId++ };
		m_TextureTable[handle] = pTex;

		return handle;
	}

	MaterialHandle Renderer::CreateMaterial(const MaterialAsset& asset)
	{
		Material mat{};

		// -------------------------
		// Parameters
		// -------------------------
		const auto& P = asset.GetParams();
		mat.BaseColorFactor = float3(P.BaseColor.x, P.BaseColor.y, P.BaseColor.z);
		mat.Opacity = P.BaseColor.w; // 또는 별도 Opacity 정책이면 수정
		mat.MetallicFactor = P.Metallic;
		mat.RoughnessFactor = P.Roughness;
		mat.NormalScale = P.NormalScale;
		mat.EmissiveFactor = P.EmissiveColor * P.EmissiveIntensity;
		mat.AmbientOcclusionStrength = P.Occlusion;

		// -------------------------
		// Alpha / Options
		// -------------------------
		mat.AlphaCutoff = P.AlphaCutoff;

		switch (asset.GetOptions().AlphaMode)
		{
		case MaterialAsset::ALPHA_OPAQUE: mat.AlphaMode = MATERIAL_ALPHA_OPAQUE; break;
		case MaterialAsset::ALPHA_MASK:   mat.AlphaMode = MATERIAL_ALPHA_MASK;   break;
		case MaterialAsset::ALPHA_BLEND:  mat.AlphaMode = MATERIAL_ALPHA_BLEND;  break;
		default:                          mat.AlphaMode = MATERIAL_ALPHA_OPAQUE; break;
		}

		// TwoSided / CastShadow 같은 건 Material에 아직 필드가 없으니
		// 나중에 Material(렌더용)에 옵션 추가하거나 MaterialRenderData 쪽에 둬도 됨.

		// -------------------------
		// Textures
		// -------------------------
		if (asset.HasTexture(MaterialAsset::TEX_ALBEDO))
			mat.BaseColorTexture = CreateTexture(asset.GetTexture(MaterialAsset::TEX_ALBEDO));

		if (asset.HasTexture(MaterialAsset::TEX_NORMAL))
			mat.NormalTexture = CreateTexture(asset.GetTexture(MaterialAsset::TEX_NORMAL));

		if (asset.HasTexture(MaterialAsset::TEX_ORM))
			mat.MetallicRoughnessTexture = CreateTexture(asset.GetTexture(MaterialAsset::TEX_ORM)); // 네 Material 필드명 기준

		// AO를 ORM에서 분리하고 싶다면:
		// out.AmbientOcclusionTexture = CreateTexture(...)

		if (asset.HasTexture(MaterialAsset::TEX_EMISSIVE))
			mat.EmissiveTexture = CreateTexture(asset.GetTexture(MaterialAsset::TEX_EMISSIVE));

		MaterialHandle handle = { m_NextMaterialId++ };
		m_MaterialTable[handle] = mat;
		return handle;
	}


	MeshHandle Renderer::CreateStaticMesh(const StaticMeshAsset& asset)
	{
		if (!asset.IsValid())
		{
			return MeshHandle{};
		}

		MeshHandle handle{ m_NextMeshId++ };

		StaticMeshRenderData outMesh;

		struct SimpleVertex
		{
			float3 Pos;
			float2 UV;
			float3 Normal;
			float3 Tangent;
		};

		const uint32 vtxCount = asset.GetVertexCount();
		outMesh.NumVertices = vtxCount;
		outMesh.VertexStride = sizeof(SimpleVertex);
		outMesh.LocalBounds = asset.GetBounds();

		const auto& positions = asset.GetPositions();
		const auto& normals = asset.GetNormals();
		const auto& tangents = asset.GetTangents();
		const auto& uvs = asset.GetTexCoords();

		// Build interleaved VB (AoS) to match your current pipeline.
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
			{
				return MeshHandle{};
			}
		}

		// ------------------------------------------------------------
		// Sections
		// - If asset has sections: create per-section IB (simple path)
		// - Else: create one section over full index buffer
		// ------------------------------------------------------------

		outMesh.Sections.clear();

		const bool useU32 = (asset.GetIndexType() == VT_UINT32);

		const auto& idx32 = asset.GetIndicesU32();
		const auto& idx16 = asset.GetIndicesU16();

		auto CreateSectionIB = [&](
			MeshSection& sec,
			const void* pIndexData,
			uint64 indexDataBytes) -> bool
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

		const auto& assetSections = asset.GetSections();
		if (!assetSections.empty())
		{
			outMesh.Sections.reserve(assetSections.size());

			for (const StaticMeshAsset::Section& asec : assetSections)
			{
				if (asec.IndexCount == 0)
					continue;

				MeshSection sec{};
				sec.NumIndices = asec.IndexCount;
				sec.IndexType = useU32 ? INDEX_TYPE_UINT32 : INDEX_TYPE_UINT16;

				// Optional fields if your MeshSection has them
				sec.Material = CreateMaterial(asset.GetMaterialSlot(asec.MaterialSlot));
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
				{
					return MeshHandle{};
				}

				outMesh.Sections.push_back(sec);
			}
		}
		else
		{
			// Fallback: one section over full indices
			MeshSection sec{};
			sec.NumIndices = asset.GetIndexCount();
			sec.IndexType = useU32 ? INDEX_TYPE_UINT32 : INDEX_TYPE_UINT16;
			sec.StartIndex = 0;

			if (!CreateSectionIB(sec, asset.GetIndexData(), asset.GetIndexDataSizeBytes()))
			{
				return MeshHandle{};
			}

			sec.Material = m_DefaultMaterial;
			outMesh.Sections.push_back(sec);
		}

		// Store
		m_MeshTable.emplace(handle, std::move(outMesh));
		return handle;
	}


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

		// 24 verts (4 per face)
		static const SimpleVertex Verts[] =
		{
			// -Z (U: +X)
			{{-0.5f,-0.5f,-0.5f},{0,1},{0,0,-1},{+1,0,0}},
			{{+0.5f,-0.5f,-0.5f},{1,1},{0,0,-1},{+1,0,0}},
			{{+0.5f,+0.5f,-0.5f},{1,0},{0,0,-1},{+1,0,0}},
			{{-0.5f,+0.5f,-0.5f},{0,0},{0,0,-1},{+1,0,0}},

			// +Z (U: +X)
			{{-0.5f,-0.5f,+0.5f},{0,1},{0,0,+1},{+1,0,0}},
			{{+0.5f,-0.5f,+0.5f},{1,1},{0,0,+1},{+1,0,0}},
			{{+0.5f,+0.5f,+0.5f},{1,0},{0,0,+1},{+1,0,0}},
			{{-0.5f,+0.5f,+0.5f},{0,0},{0,0,+1},{+1,0,0}},

			// -X (U: -Z)
			{{-0.5f,-0.5f,+0.5f},{0,1},{-1,0,0},{0,0,-1}},
			{{-0.5f,-0.5f,-0.5f},{1,1},{-1,0,0},{0,0,-1}},
			{{-0.5f,+0.5f,-0.5f},{1,0},{-1,0,0},{0,0,-1}},
			{{-0.5f,+0.5f,+0.5f},{0,0},{-1,0,0},{0,0,-1}},

			// +X (U: +Z)
			{{+0.5f,-0.5f,-0.5f},{0,1},{+1,0,0},{0,0,+1}},
			{{+0.5f,-0.5f,+0.5f},{1,1},{+1,0,0},{0,0,+1}},
			{{+0.5f,+0.5f,+0.5f},{1,0},{+1,0,0},{0,0,+1}},
			{{+0.5f,+0.5f,-0.5f},{0,0},{+1,0,0},{0,0,+1}},

			// -Y (U: +X)
			{{-0.5f,-0.5f,+0.5f},{0,1},{0,-1,0},{+1,0,0}},
			{{+0.5f,-0.5f,+0.5f},{1,1},{0,-1,0},{+1,0,0}},
			{{+0.5f,-0.5f,-0.5f},{1,0},{0,-1,0},{+1,0,0}},
			{{-0.5f,-0.5f,-0.5f},{0,0},{0,-1,0},{+1,0,0}},

			// +Y (U: +X)
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
			{
				return MeshHandle{};
			}
		}

		// IB (one section)
		MeshSection sec = {};
		sec.NumIndices = _countof(Indices);
		sec.IndexType = INDEX_TYPE_UINT32;
		sec.StartIndex = 0;
		sec.BaseVertex = 0;

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
			{
				return MeshHandle{};
			}
		}
		sec.Material = m_DefaultMaterial;

		outMesh.Sections.clear();
		outMesh.Sections.push_back(sec);

		m_MeshTable.emplace(handle, std::move(outMesh));
		return handle;
	}




} // namespace shz
