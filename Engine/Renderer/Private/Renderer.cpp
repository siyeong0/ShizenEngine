#include "pch.h"
#include "Renderer.h"
#include "ViewFamily.h"
#include "Engine/RHI/Interface/IBuffer.h"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	struct CameraConstants
	{
		float4x4 ViewProj;
	};
	static_assert(sizeof(CameraConstants) % 16 == 0);

	static const char* g_TriangleVS = R"(
cbuffer CameraCB
{
    float4x4 g_ViewProj;
};

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float3 Color : COLOR;
};

void main(in  uint    VertId : SV_VertexID,
          out PSInput PSIn)
{
    float4 Pos[3];
    Pos[0] = float4(-0.5, -0.5, 0.0, 1.0);
    Pos[1] = float4( 0.0, +0.5, 0.0, 1.0);
    Pos[2] = float4(+0.5, -0.5, 0.0, 1.0);

    float3 Col[3];
    Col[0] = float3(1.0, 0.0, 0.0);
    Col[1] = float3(0.0, 1.0, 0.0);
    Col[2] = float3(0.0, 0.0, 1.0);

    // View/Proj 적용
    PSIn.Pos   = mul(Pos[VertId], g_ViewProj);
    PSIn.Color = Col[VertId];
}
)";


	static const char* g_TrianglePS = R"(
struct PSInput
{
    float4 Pos   : SV_POSITION;
    float3 Color : COLOR;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in  PSInput  PSIn,
          out PSOutput PSOut)
{
    PSOut.Color = float4(PSIn.Color.rgb, 1.0);
}
)";

	bool Renderer::Initialize(const RendererCreateInfo& createInfo)
	{
		m_CreateInfo = createInfo;

		if (!m_CreateInfo.pDevice || !m_CreateInfo.pImmediateContext || !m_CreateInfo.pSwapChain)
			return false;

		const auto& SCDesc = m_CreateInfo.pSwapChain->GetDesc();
		m_Width = (m_CreateInfo.BackBufferWidth != 0) ? m_CreateInfo.BackBufferWidth : SCDesc.Width;
		m_Height = (m_CreateInfo.BackBufferHeight != 0) ? m_CreateInfo.BackBufferHeight : SCDesc.Height;

		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(CameraConstants), "Shader constants CB", &m_pCameraCB);

		if (!CreateDebugTrianglePSO())
			return false;

		return true;
	}

	void Renderer::Cleanup()
	{
		m_pTrianglePSO.Release();
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

		// ---- RenderTarget 바인딩(권장) ----
		ITextureView* pRTV = pSC->GetCurrentBackBufferRTV();
		ITextureView* pDSV = pSC->GetDepthBufferDSV();
		pCtx->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		// ---- Clear ----
		const float ClearColor[] = { 0.350f, 0.350f, 0.350f, 1.0f };
		pCtx->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
		pCtx->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		// ---- ViewFamily에서 View/Proj 가져오기 ----
		// ⚠️ 아래는 "ViewFamily가 Views[0]을 가진다"는 전형적인 형태를 가정.
		// 너의 ViewFamily 구조에 맞게 필드명만 맞춰주면 됨.
		const auto& view = viewFamily.Views[0];

		// 너의 수학 라이브러리가 row/col-major 어느 쪽인지에 따라
		// ViewProj 곱 순서를 맞춰야 함.
		// (대부분: ViewProj = View * Proj 또는 Proj * View 중 하나)
		Matrix4x4 viewProj = view.ViewMatrix * view.ProjMatrix; // <-- 네 엔진 규약에 맞게 조정

		// ---- CameraCB 업데이트 ----
		{
			MapHelper<CameraConstants> CBData(pCtx, m_pCameraCB, MAP_WRITE, MAP_FLAG_DISCARD);
			CBData->ViewProj = viewProj;
		}

		// ---- Debug triangle draw ----
		if (m_pTrianglePSO)
		{
			pCtx->SetPipelineState(m_pTrianglePSO);

			pCtx->CommitShaderResources(m_pTriangleSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			DrawAttribs DA;
			DA.NumVertices = 3;
			pCtx->Draw(DA);
		}

		// scene object draw는 그대로 (나중에 확장)
		(void)scene;
	}


	void Renderer::EndFrame()
	{
	}

	bool Renderer::CreateDebugTrianglePSO()
	{
		auto* pDevice = m_CreateInfo.pDevice.RawPtr();
		if (!pDevice)
			return false;

		GraphicsPipelineStateCreateInfo PSOCreateInfo;
		PSOCreateInfo.PSODesc.Name = "Debug Triangle PSO";
		PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		const auto& SCDesc = m_CreateInfo.pSwapChain->GetDesc();

		PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
		PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = SCDesc.ColorBufferFormat;
		PSOCreateInfo.GraphicsPipeline.DSVFormat = SCDesc.DepthBufferFormat;

		PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;

		ShaderCreateInfo ShaderCI;
		ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		ShaderCI.Desc.UseCombinedTextureSamplers = true;
		ShaderCI.EntryPoint = "main";

		RefCntAutoPtr<IShader> pVS;
		{
			ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			ShaderCI.Desc.Name = "Debug Triangle VS";
			ShaderCI.Source = g_TriangleVS;
			pDevice->CreateShader(ShaderCI, &pVS);
			if (!pVS) return false;
		}

		RefCntAutoPtr<IShader> pPS;
		{
			ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			ShaderCI.Desc.Name = "Debug Triangle PS";
			ShaderCI.Source = g_TrianglePS;
			pDevice->CreateShader(ShaderCI, &pPS);
			if (!pPS) return false;
		}

		PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{ SHADER_TYPE_VERTEX, "CameraCB", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE  }
		};
		PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
		PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		PSOCreateInfo.pVS = pVS;
		PSOCreateInfo.pPS = pPS;

		pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pTrianglePSO);
		if (!m_pTrianglePSO)
			return false;

		m_pTrianglePSO->CreateShaderResourceBinding(&m_pTriangleSRB, true /*InitStaticResources*/);
		if (!m_pTriangleSRB)
			return false;

		//auto* pVar = m_pTrianglePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "CameraCB");
		//if (pVar)
		//	pVar->Set(m_pCameraCB);
		auto* pCBVar = m_pTriangleSRB->GetVariableByName(SHADER_TYPE_VERTEX, "CameraCB");
		if (!pCBVar)
			return false;
		pCBVar->Set(m_pCameraCB);

		return true;
	}

	MeshHandle Renderer::CreateCubeMesh()
	{
		MeshHandle handle{ m_NextMeshId++ };

		StaticMesh mesh;
		if (!CreateCubeMesh_Internal(mesh))
		{
			// 실패하면 invalid 반환
			return MeshHandle{};
		}

		m_MeshTable.emplace(handle, std::move(mesh));
		return handle;
	}

	bool Renderer::CreateCubeMesh_Internal(StaticMesh& outMesh)
	{
		// 매우 최소: Position(Color/UV 없음) 정점 + 36 indices
		// 네가 다음 단계에서 Vertex layout을 확정하면 여기 정점 구조를 교체하면 됨.

		struct SimpleVertex
		{
			float3 Pos;
		};

		static const SimpleVertex Verts[8] =
		{
			{ {-0.5f, -0.5f, -0.5f} },
			{ {+0.5f, -0.5f, -0.5f} },
			{ {+0.5f, +0.5f, -0.5f} },
			{ {-0.5f, +0.5f, -0.5f} },
			{ {-0.5f, -0.5f, +0.5f} },
			{ {+0.5f, -0.5f, +0.5f} },
			{ {+0.5f, +0.5f, +0.5f} },
			{ {-0.5f, +0.5f, +0.5f} },
		};

		static const uint32 Indices[36] =
		{
			// -Z
			0,2,1, 0,3,2,
			// +Z
			4,5,6, 4,6,7,
			// -X
			0,7,3, 0,4,7,
			// +X
			1,2,6, 1,6,5,
			// -Y
			0,1,5, 0,5,4,
			// +Y
			3,7,6, 3,6,2
		};

		outMesh.NumVertices = 8;
		outMesh.VertexStride = sizeof(SimpleVertex);
		outMesh.LocalBounds = { {-0.5f, -0.5f, -0.5f}, { 0.5f, 0.5f, 0.5f } };

		// --- Create vertex buffer ---
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

		// --- Create index buffer (section 1개) ---
		MeshSection sec;
		sec.NumIndices = 36;
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
