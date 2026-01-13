#include "pch.h"
#include "Renderer.h"
#include "ViewFamily.h"
#include "Engine/RHI/Interface/IBuffer.h"

namespace shz
{
	static const char* g_TriangleVS = R"(
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

    PSIn.Pos   = Pos[VertId];
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
		m_FrameParam = {};
	}

	void Renderer::OnResize(uint32 width, uint32 height)
	{
		m_Width = width;
		m_Height = height;
	}

	void Renderer::BeginFrame(const FrameData& params)
	{
		m_FrameParam = params;
	}

	void Renderer::Render(const RenderScene& scene, const ViewFamily&)
	{
		auto* pCtx = m_CreateInfo.pImmediateContext.RawPtr();
		auto* pSC = m_CreateInfo.pSwapChain.RawPtr();
		if (!pCtx || !pSC)
			return;

		ITextureView* pRTV = pSC->GetCurrentBackBufferRTV();
		ITextureView* pDSV = pSC->GetDepthBufferDSV();

		const float ClearColor[] = { 0.350f, 0.350f, 0.350f, 1.0f };
		pCtx->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		pCtx->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		// 1) Debug triangle (내장)
		if (m_pTrianglePSO)
		{
			pCtx->SetPipelineState(m_pTrianglePSO);
			DrawAttribs DA;
			DA.NumVertices = 3;
			pCtx->Draw(DA);
		}

		// 2) RenderScene objects (StaticMesh draw) - 지금은 "파이프라인/바인딩"이 없어서 뼈대만
		//    네가 다음 단계에서 material/PSO를 붙이면 여기서 section loop로 DrawIndexed가 들어감.
		for (const auto& kv : scene.GetObjects())
		{
			const RenderScene::RenderObject& obj = kv.second;
			auto mit = m_MeshTable.find(obj.meshHandle);
			if (mit == m_MeshTable.end())
				continue;

			const StaticMesh& mesh = mit->second;
			if (!mesh.VertexBuffer)
				continue;

			// TODO:
			// - SetVertexBuffers(mesh.VertexBuffer, stride)
			// - for each section: SetIndexBuffer, BindMaterial, DrawIndexed
		}
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

		PSOCreateInfo.pVS = pVS;
		PSOCreateInfo.pPS = pPS;

		pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pTrianglePSO);
		return m_pTrianglePSO != nullptr;
	}

	MeshHandle Renderer::CreateCubeMesh(const char* /*path*/)
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
