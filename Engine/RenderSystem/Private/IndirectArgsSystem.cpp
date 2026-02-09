#include "pch.h"
#include "Engine/RenderSystem/Public/IndirectArgsSystem.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/RHI/Interface/GraphicsTypes.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	static inline uint32 DivUp(uint32 x, uint32 d) { return (x + d - 1u) / d; }

	void IndirectArgsSystem::InstallPasses(Renderer& renderer)
	{
		// Indirect args (RAW 20 bytes)
		{
			BufferDesc bd = {};
			bd.Name = "IndirectArgsBuffer";
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS;
			bd.Mode = BUFFER_MODE_RAW;
			bd.Size = 20 * MAX_NUM_INDIRECTS;

			renderer.AddBuffer(STRING_HASH("IndirectArgsBuffer"), bd);
		}

		// Indirect counts (RAW 4 bytes * slots)
		{
			BufferDesc bd = {};
			bd.Name = "IndirectCountBuffer";
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_UNORDERED_ACCESS; // UAV만
			bd.Mode = BUFFER_MODE_RAW;
			bd.Size = 4u * MAX_NUM_INDIRECTS;

			renderer.AddBuffer(STRING_HASH("IndirectCountBuffer"), bd);
		}

		// IndirectArgsWriter CB (contains templates array)
		{
			BufferDesc bd = {};
			bd.Name = "IndirectArgsWriterCB";
			bd.Usage = USAGE_DYNAMIC;
			bd.BindFlags = BIND_UNIFORM_BUFFER;
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;

			bd.Size = sizeof(hlsl::IndirectConstants);
			renderer.AddBuffer(STRING_HASH("IndirectArgsWriterCB"), bd);
		}

		renderer.AddPass(
			"IndirectWriteArgs",
			[&](RenderPassBuilder& b)
			{
				// read counts, write args
				b.DeclareBufferUAV(STRING_HASH("IndirectCountBuffer"), RENDER_ACCESS_READ);
				b.DeclareBufferUAV(STRING_HASH("IndirectArgsBuffer"), RENDER_ACCESS_WRITE);

				// constants
				b.DeclareBufferCBVRead(STRING_HASH("IndirectArgsWriterCB"));
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pWriteArgsCSO && m_pWriteArgsSRB, "IndirectWriteArgs PSO/SRB not ready.");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// (0) Update CB
				{
					MapHelper<hlsl::IndirectConstants> cb(
						pContext,
						ctx.pRegistry->GetBuffer(STRING_HASH("IndirectArgsWriterCB")),
						MAP_WRITE,
						MAP_FLAG_DISCARD);

					cb->NumSlots = std::min<uint32>(m_NumSlots, 256u);
					cb->MaxInstances = 1u << 24;
					cb->Pad0 = 0;
					cb->Pad1 = 0;

					for (uint32 i = 0; i < cb->NumSlots; ++i)
					{
						cb->Templates[i] = m_Templates[i];
					}
				}

				// (1) Dispatch
				pContext->SetPipelineState(m_pWriteArgsCSO);
				pContext->CommitShaderResources(m_pWriteArgsSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				const uint32 numSlots = std::min<uint32>(m_NumSlots, 256u);

				DispatchComputeAttribs disp = {};
				disp.ThreadGroupCountX = std::max(1u, DivUp(numSlots, 64u));
				disp.ThreadGroupCountY = 1;
				disp.ThreadGroupCountZ = 1;
				pContext->DispatchCompute(disp);
			},
				[this, &renderer]()
			{
				// PSO + SRB create once
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "WriteIndirectArgs";
				csCI.Desc.Name = "WriteIndirectArgsCS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_WriteArgsCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "WriteIndirectArgsCS compile failed.");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_IndirectWriteArgs";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				// HLSL 변수명과 반드시 일치
				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_IndirectArgs",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_IndirectCounts",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "INDIRECT_ARGS_WRITER_CB", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				psoCI.pCS = cs;

				m_pWriteArgsCSO = renderer.AcquirePipelineState(psoCI, true);
				ASSERT(m_pWriteArgsCSO, "AcquireCompute(IndirectWriteArgs) failed.");

				m_pWriteArgsCSO->CreateShaderResourceBinding(&m_pWriteArgsSRB, true);
				ASSERT(m_pWriteArgsSRB, "IndirectWriteArgs SRB create failed.");

				// Bind resources
				if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_IndirectArgs"))
				{
					var->Set(renderer.GetBufferUAV(STRING_HASH("IndirectArgsBuffer")));
				}
				if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_IndirectCounts"))
				{
					var->Set(renderer.GetBufferUAV(STRING_HASH("IndirectCountBuffer")));
				}
				if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INDIRECT_ARGS_WRITER_CB"))
				{
					var->Set(renderer.GetBuffer(STRING_HASH("IndirectArgsWriterCB")));
				}
			});
	}

	uint32 IndirectArgsSystem::AllocateSlot(const std::string& debugName)
	{
		for (uint32 i = 0; i < MAX_NUM_INDIRECTS; ++i)
		{
			if (m_SlotUsed[i] == 0)
			{
				m_SlotUsed[i] = 1;
				m_NumSlots = std::max(m_NumSlots, i + 1);
				return i;
			}
		}

		ASSERT(false, "AllocateSlot failed: MAX_NUM_INDIRECTS exhausted.");
		return 0;
	}

	void IndirectArgsSystem::ResetAllSlots()
	{
		m_SlotUsed.fill(0);
		m_NumSlots = 0;

		// template도 0으로 초기화(디버그 안정성)
		std::memset(m_Templates.data(), 0, sizeof(m_Templates));
	}

	void IndirectArgsSystem::SetTemplate(uint32 slot, const hlsl::IndirectArgsTemplate& t)
	{
		ASSERT(slot < MAX_NUM_INDIRECTS, "slot out of range.");
		m_Templates[slot] = t;
	}
} // namespace shz
