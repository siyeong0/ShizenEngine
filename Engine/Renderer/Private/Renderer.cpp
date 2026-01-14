#include "pch.h"
#include "Renderer.h"
#include "ViewFamily.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

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

		pCtx->SetPipelineState(m_pBasicPSO);
		pCtx->CommitShaderResources(m_pBasicSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

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

			for (const auto& section : mesh.Sections)
			{
				pCtx->SetIndexBuffer(section.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION); // <-- VERIFY 금지

				DrawIndexedAttribs DIA;
				DIA.NumIndices = section.NumIndices;
				DIA.IndexType = VT_UINT32;
				DIA.Flags = DRAW_FLAG_VERIFY_ALL; // 디버그면 OK

				pCtx->DrawIndexed(DIA);
			}
		}
	}



	void Renderer::EndFrame()
	{
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

		// 너 셰이더는 Depth01을 출력하지만, 현재 PS에서 쓰지도 않고
		// Debug 목적이라면 Depth off가 맞음.
		GP.DepthStencilDesc.DepthEnable = false;

		// =========================
		// Input Layout (ATTRIB0/1/2)
		// VSInput:
		//  float3 Pos    : ATTRIB0;
		//  float2 UV     : ATTRIB1;
		//  float3 Normal : ATTRIB2;
		// =========================
		LayoutElement LayoutElems[] =
		{
			// Slot 0 기준
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // ATTRIB0 : float3
			LayoutElement{1, 0, 2, VT_FLOAT32, false}, // ATTRIB1 : float2
			LayoutElement{2, 0, 3, VT_FLOAT32, false}, // ATTRIB2 : float3
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
			// VS에서만 사용
			{ SHADER_TYPE_VERTEX, "FRAME_CONSTANTS",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE  },
			{ SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC  },
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


	MeshHandle Renderer::CreateCubeMesh()
	{
		MeshHandle handle{ m_NextMeshId++ };

		StaticMeshRenderData mesh;
		if (!CreateCubeMesh_Internal(mesh))
		{
			// 실패하면 invalid 반환
			return MeshHandle{};
		}

		m_MeshTable.emplace(handle, std::move(mesh));
		return handle;
	}

	bool Renderer::CreateCubeMesh_Internal(StaticMeshRenderData& outMesh)
	{
		struct SimpleVertex
		{
			float3 Pos;
			float2 UV;
			float3 Normal;
		};

		// 24 verts (4 per face)
		static const SimpleVertex Verts[] =
		{
			// -Z
			{{-0.5f,-0.5f,-0.5f},{0,1},{0,0,-1}},
			{{+0.5f,-0.5f,-0.5f},{1,1},{0,0,-1}},
			{{+0.5f,+0.5f,-0.5f},{1,0},{0,0,-1}},
			{{-0.5f,+0.5f,-0.5f},{0,0},{0,0,-1}},

			// +Z
			{{-0.5f,-0.5f,+0.5f},{0,1},{0,0,+1}},
			{{+0.5f,-0.5f,+0.5f},{1,1},{0,0,+1}},
			{{+0.5f,+0.5f,+0.5f},{1,0},{0,0,+1}},
			{{-0.5f,+0.5f,+0.5f},{0,0},{0,0,+1}},

			// -X
			{{-0.5f,-0.5f,+0.5f},{0,1},{-1,0,0}},
			{{-0.5f,-0.5f,-0.5f},{1,1},{-1,0,0}},
			{{-0.5f,+0.5f,-0.5f},{1,0},{-1,0,0}},
			{{-0.5f,+0.5f,+0.5f},{0,0},{-1,0,0}},

			// +X
			{{+0.5f,-0.5f,-0.5f},{0,1},{+1,0,0}},
			{{+0.5f,-0.5f,+0.5f},{1,1},{+1,0,0}},
			{{+0.5f,+0.5f,+0.5f},{1,0},{+1,0,0}},
			{{+0.5f,+0.5f,-0.5f},{0,0},{+1,0,0}},

			// -Y
			{{-0.5f,-0.5f,+0.5f},{0,1},{0,-1,0}},
			{{+0.5f,-0.5f,+0.5f},{1,1},{0,-1,0}},
			{{+0.5f,-0.5f,-0.5f},{1,0},{0,-1,0}},
			{{-0.5f,-0.5f,-0.5f},{0,0},{0,-1,0}},

			// +Y
			{{-0.5f,+0.5f,-0.5f},{0,1},{0,+1,0}},
			{{+0.5f,+0.5f,-0.5f},{1,1},{0,+1,0}},
			{{+0.5f,+0.5f,+0.5f},{1,0},{0,+1,0}},
			{{-0.5f,+0.5f,+0.5f},{0,0},{0,+1,0}},
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
			if (!outMesh.VertexBuffer) return false;
		}

		// IB (section 1개)
		MeshSection sec;
		sec.NumIndices = _countof(Indices);
		sec.IndexType = INDEX_TYPE_UINT32;

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
			if (!sec.IndexBuffer) return false;
		}

		outMesh.Sections.clear();
		outMesh.Sections.push_back(sec);
		return true;
	}

} // namespace shz
