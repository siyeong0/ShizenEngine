/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include <array>

#include "Tutorial07_RenderPass.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Tools/Image/Public/TextureUtilities.h"
#include "Engine/GraphicsUtils/Public/ColorConversion.h"
#include "TexturedCube.h"

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/ImGuiUtils.hpp"
#include "Engine/Core/Common/Public/FastRand.hpp"

namespace shz
{

	namespace
	{

		namespace HLSL
		{
#include "Assets/shader_structs.fxh"
		}

	} // namespace

	SampleBase* CreateSample()
	{
		return new Tutorial07_RenderPass();
	}

	void Tutorial07_RenderPass::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& attribs)
	{
		SampleBase::ModifyEngineInitInfo(attribs);

		// We do not need the depth buffer from the swap chain in this sample
		attribs.SCDesc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;
	}

	void Tutorial07_RenderPass::Initialize(const SampleInitInfo& InitInfo)
	{
		SampleBase::Initialize(InitInfo);

		CreateUniformBuffer(m_pDevice, sizeof(HLSL::Constants), "Shader constants CB", &m_pShaderConstantsCB);

		// Load textured cube
		m_CubeVertexBuffer = TexturedCube::CreateVertexBuffer(m_pDevice, GEOMETRY_PRIMITIVE_VERTEX_FLAG_POS_TEX);
		m_CubeIndexBuffer = TexturedCube::CreateIndexBuffer(m_pDevice);
		m_CubeTextureSRV = TexturedCube::LoadTexture(m_pDevice, "Assets/pearl_abyss_logo.png")->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

		createRenderPass();
		createLightsBuffer();
		initLights();

		// Create a shader source stream factory to load shaders from files.
		RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
		m_pEngineFactory->CreateDefaultShaderSourceStreamFactory("Assets", &pShaderSourceFactory);

		createCubePSO(pShaderSourceFactory);
		createLightVolumePSO(pShaderSourceFactory);
		createAmbientLightPSO(pShaderSourceFactory);

		// Transition all resources to required states as no transitions are allowed within the render pass.
		StateTransitionDesc barriers[] = //
		{
			{m_pShaderConstantsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
			{m_CubeVertexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
			{m_CubeIndexBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
			{m_pLightsBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
			{m_CubeTextureSRV->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE} //
		};

		m_pImmediateContext->TransitionResourceStates(_countof(barriers), barriers);
	}

	void Tutorial07_RenderPass::Render()
	{
		const SwapChainDesc& swapChainDesc = m_pSwapChain->GetDesc();

		// Update constant buffer
		{
			MapHelper<HLSL::Constants> constants(m_pImmediateContext, m_pShaderConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD);
			constants->ViewProj = m_CameraViewProjMatrix;
			constants->ViewProjInv = m_CameraViewProjInvMatrix;
			constants->ViewportSize = float4{
				static_cast<float>(swapChainDesc.Width),
				static_cast<float>(swapChainDesc.Height),
				1.f / static_cast<float>(swapChainDesc.Width),
				1.f / static_cast<float>(swapChainDesc.Height) //
			};
			constants->ShowLightVolumes = m_bShowLightVolumes ? 1 : 0;
		}

		IFramebuffer* framebuffer = getCurrentFramebuffer();

		BeginRenderPassAttribs renderPassBeginInfo;
		renderPassBeginInfo.pRenderPass = m_pRenderPass;
		renderPassBeginInfo.pFramebuffer = framebuffer;

		OptimizedClearValue clearValues[4];
		// Color
		clearValues[0].Color[0] = 0.f;
		clearValues[0].Color[1] = 0.f;
		clearValues[0].Color[2] = 0.f;
		clearValues[0].Color[3] = 0.f;

		// Depth Z
		clearValues[1].Color[0] = 1.f;
		clearValues[1].Color[1] = 1.f;
		clearValues[1].Color[2] = 1.f;
		clearValues[1].Color[3] = 1.f;

		// Depth buffer
		clearValues[2].DepthStencil.Depth = 1.f;

		// Final color buffer
		clearValues[3].Color[0] = 0.0625f;
		clearValues[3].Color[1] = 0.0625f;
		clearValues[3].Color[2] = 0.0625f;
		clearValues[3].Color[3] = 1.f;
		if (m_ConvertPSOutputToGamma)
		{
			for (size_t i = 0; i < 3; ++i)
			{
				clearValues[3].Color[i] = LinearToGamma(clearValues[3].Color[i]);
			}
		}

		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.ClearValueCount = _countof(clearValues);
		renderPassBeginInfo.StateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
		m_pImmediateContext->BeginRenderPass(renderPassBeginInfo);
		{
			drawScene();
			m_pImmediateContext->NextSubpass();
			applyLighting();
		}
		m_pImmediateContext->EndRenderPass();

		if (m_pDevice->GetDeviceInfo().IsGLDevice())
		{
			// In OpenGL we now have to copy our off-screen buffer to the default framebuffer
			ITexture* pOffscreenRenderTarget = framebuffer->GetDesc().ppAttachments[3]->GetTexture();
			ITexture* pBackBuffer = m_pSwapChain->GetCurrentBackBufferRTV()->GetTexture();

			CopyTextureAttribs CopyAttribs{ pOffscreenRenderTarget, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
										   pBackBuffer, RESOURCE_STATE_TRANSITION_MODE_TRANSITION };
			m_pImmediateContext->CopyTexture(CopyAttribs);
		}
	}

	void Tutorial07_RenderPass::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
	{
		SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);

		if (m_bAnimateLights)
			updateLights(static_cast<float>(ElapsedTime));

		float4x4 view = float4x4::Translation({ 0.0f, 0.0f, 25.0f });

		// Get pretransform matrix that rotates the scene according the surface orientation
		float4x4 srfPreTransform = GetSurfacePretransformMatrix(float3{ 0, 0, 1 });

		// Get projection matrix adjusted to the current screen orientation
		float4x4 proj = GetAdjustedProjectionMatrix(PI / 4.0f, 0.1f, 100.f);

		// Compute world-view-projection matrix
		m_CameraViewProjMatrix = view * srfPreTransform * proj;
		m_CameraViewProjInvMatrix = m_CameraViewProjMatrix.Inversed();
	}

	void Tutorial07_RenderPass::ReleaseSwapChainBuffers()
	{
		m_FramebufferCache.clear();
	}

	void Tutorial07_RenderPass::WindowResize(uint32 Width, uint32 Height)
	{
		releaseWindowResources();
	}

	void Tutorial07_RenderPass::UpdateUI()
	{
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (ImGui::InputInt("Lights count", &m_LightsCount, 100, 1000, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				m_LightsCount = std::max(m_LightsCount, 100);
				m_LightsCount = std::min(m_LightsCount, 50000);
				initLights();
				createLightsBuffer();
			}

			ImGui::Checkbox("Show light volumes", &m_bShowLightVolumes);
			ImGui::Checkbox("Animate lights", &m_bAnimateLights);
		}
		ImGui::End();
	}

	void Tutorial07_RenderPass::createCubePSO(IShaderSourceInputStreamFactory* pShaderSourceFactory)
	{
		GraphicsPipelineStateCreateInfo PSOCreateInfo;
		PipelineStateDesc& PSODesc = PSOCreateInfo.PSODesc;

		// Pipeline state name is used by the engine to report issues.
		PSODesc.Name = "Cube PSO";

		PSOCreateInfo.GraphicsPipeline.pRenderPass = m_pRenderPass;
		PSOCreateInfo.GraphicsPipeline.SubpassIndex = 0; // This PSO will be used within the first subpass
		// When pRenderPass is not null, all RTVFormats and DSVFormat must be TEX_FORMAT_UNKNOWN,
		// while NumRenderTargets must be 0

		PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;

		ShaderCreateInfo ShaderCI;
		// Tell the system that the shader source code is in HLSL.
		ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

		// OpenGL backend requires emulated combined HLSL texture samplers (g_Texture + g_Texture_sampler combination)
		ShaderCI.Desc.UseCombinedTextureSamplers = true;

		// Pack matrices in row-major order
		ShaderCI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

		ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;
		// Create cube vertex shader
		RefCntAutoPtr<IShader> pVS;
		{
			ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			ShaderCI.EntryPoint = "main";
			ShaderCI.Desc.Name = "Cube VS";
			ShaderCI.FilePath = "cube.vsh";
			m_pDevice->CreateShader(ShaderCI, &pVS);
			ASSERT_EXPR(pVS != nullptr);
		}

		// Create cube pixel shader
		RefCntAutoPtr<IShader> pPS;
		{
			ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			ShaderCI.EntryPoint = "main";
			ShaderCI.Desc.Name = "Cube PS";
			ShaderCI.FilePath = "cube.psh";
			m_pDevice->CreateShader(ShaderCI, &pPS);
			ASSERT_EXPR(pPS != nullptr);
		}

		const LayoutElement LayoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Attribute 0 - vertex position
			LayoutElement{1, 0, 2, VT_FLOAT32, false}  // Attribute 1 - texture coordinates
		};

		PSOCreateInfo.pVS = pVS;
		PSOCreateInfo.pPS = pPS;

		PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
		PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

		// Define variable type that will be used by default
		PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{SHADER_TYPE_PIXEL, "g_Texture", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
		};
		PSODesc.ResourceLayout.Variables = Vars;
		PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		// Define immutable sampler for g_Texture.
		SamplerDesc SamLinearClampDesc
		{
			FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
		};
		ImmutableSamplerDesc ImtblSamplers[] =
		{
			{SHADER_TYPE_PIXEL, "g_Texture", SamLinearClampDesc}
		};
		PSODesc.ResourceLayout.ImmutableSamplers = ImtblSamplers;
		PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);

		m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pCubePSO);
		ASSERT_EXPR(m_pCubePSO != nullptr);

		m_pCubePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ShaderConstants")->Set(m_pShaderConstantsCB);

		m_pCubePSO->CreateShaderResourceBinding(&m_pCubeSRB, true);
		ASSERT_EXPR(m_pCubeSRB != nullptr);
		m_pCubeSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_CubeTextureSRV);
	}

	void Tutorial07_RenderPass::createLightVolumePSO(IShaderSourceInputStreamFactory* pShaderSourceFactory)
	{
		GraphicsPipelineStateCreateInfo PSOCreateInfo;
		PipelineStateDesc& PSODesc = PSOCreateInfo.PSODesc;

		PSODesc.Name = "Deferred lighting PSO";

		PSOCreateInfo.GraphicsPipeline.pRenderPass = m_pRenderPass;
		PSOCreateInfo.GraphicsPipeline.SubpassIndex = 1; // This PSO will be used within the second subpass

		PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false; // Do not write depth

		// We will use alpha-blending to accumulate influence of all lights
		RenderTargetBlendDesc& RT0Blend{ PSOCreateInfo.GraphicsPipeline.BlendDesc.RenderTargets[0] };
		RT0Blend.BlendEnable = true;
		RT0Blend.BlendOp = BLEND_OPERATION_ADD;
		RT0Blend.SrcBlend = BLEND_FACTOR_ONE;
		RT0Blend.DestBlend = BLEND_FACTOR_ONE;
		RT0Blend.SrcBlendAlpha = BLEND_FACTOR_ZERO;
		RT0Blend.DestBlendAlpha = BLEND_FACTOR_ONE;

		ShaderCreateInfo ShaderCI;
		ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

		// OpenGL backend requires emulated combined HLSL texture samplers (g_Texture + g_Texture_sampler combination)
		ShaderCI.Desc.UseCombinedTextureSamplers = true;

		// Pack matrices in row-major order
		ShaderCI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

		// If the device does not support gamma correction, we will have to do it in the shader.
		// Notice that blending in gamma space is not mathematically correct, but we have no choice.
		ShaderMacro Macros[] = { {"CONVERT_PS_OUTPUT_TO_GAMMA", m_ConvertPSOutputToGamma ? "1" : "0"} };
		ShaderCI.Macros = { Macros, _countof(Macros) };

		ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;
		// Create a vertex shader
		RefCntAutoPtr<IShader> pVS;
		{
			ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			ShaderCI.EntryPoint = "main";
			ShaderCI.Desc.Name = "Light volume VS";
			ShaderCI.FilePath = "light_volume.vsh";
			m_pDevice->CreateShader(ShaderCI, &pVS);
			ASSERT_EXPR(pVS != nullptr);
		}

		// Create a pixel shader
		RefCntAutoPtr<IShader> pPS;
		{
			ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			ShaderCI.EntryPoint = "main";
			ShaderCI.Desc.Name = "Light volume PS";
			ShaderCI.FilePath = "light_volume.psh";
			m_pDevice->CreateShader(ShaderCI, &pPS);
			ASSERT_EXPR(pPS != nullptr);
		}

		const LayoutElement LayoutElems[] =
		{
			LayoutElement{0, 0, 3, VT_FLOAT32, false}, // Attribute 0 - vertex position
			LayoutElement{1, 0, 2, VT_FLOAT32, false}, // Attribute 1 - texture coordinates (we don't use them)
			LayoutElement{2, 1, 4, VT_FLOAT32, false, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}, // Attribute 2 - light position
			LayoutElement{3, 1, 3, VT_FLOAT32, false, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}  // Attribute 3 - light color
		};

		PSOCreateInfo.pVS = pVS;
		PSOCreateInfo.pPS = pPS;

		PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
		PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

		// Define variable type that will be used by default
		PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		ShaderResourceVariableDesc Vars[] =
		{
			{SHADER_TYPE_PIXEL, "g_SubpassInputColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
			{SHADER_TYPE_PIXEL, "g_SubpassInputDepthZ", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
		};
		PSODesc.ResourceLayout.Variables = Vars;
		PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pLightVolumePSO);
		ASSERT_EXPR(m_pLightVolumePSO != nullptr);

		m_pLightVolumePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ShaderConstants")->Set(m_pShaderConstantsCB);
		m_pLightVolumePSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "ShaderConstants")->Set(m_pShaderConstantsCB);
	}

	void Tutorial07_RenderPass::createAmbientLightPSO(IShaderSourceInputStreamFactory* pShaderSourceFactory)
	{
		GraphicsPipelineStateCreateInfo PSOCreateInfo;
		PipelineStateDesc& PSODesc = PSOCreateInfo.PSODesc;

		PSODesc.Name = "Ambient light PSO";

		PSOCreateInfo.GraphicsPipeline.pRenderPass = m_pRenderPass;
		PSOCreateInfo.GraphicsPipeline.SubpassIndex = 1; // This PSO will be used within the second subpass

		PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
		PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false; // Disable depth

		ShaderCreateInfo ShaderCI;
		ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

		ShaderCI.Desc.UseCombinedTextureSamplers = true;

		ShaderCI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

		// If the device does not support gamma correction, we will have to do it in the shader.
		// Notice that blending in gamma space is not mathematically correct, but we have no choice.
		ShaderMacro Macros[] = { {"CONVERT_PS_OUTPUT_TO_GAMMA", m_ConvertPSOutputToGamma ? "1" : "0"} };
		ShaderCI.Macros = { Macros, _countof(Macros) };

		ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;
		// Create a vertex shader
		RefCntAutoPtr<IShader> pVS;
		{
			ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			ShaderCI.EntryPoint = "main";
			ShaderCI.Desc.Name = "Ambient light VS";
			ShaderCI.FilePath = "ambient_light.vsh";
			m_pDevice->CreateShader(ShaderCI, &pVS);
			ASSERT_EXPR(pVS != nullptr);
		}

		// Create a pixel shader
		RefCntAutoPtr<IShader> pPS;
		{
			ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			ShaderCI.EntryPoint = "main";
			ShaderCI.Desc.Name = "Ambient light PS";
			ShaderCI.FilePath = "ambient_light.psh";
			m_pDevice->CreateShader(ShaderCI, &pPS);
			ASSERT_EXPR(pPS != nullptr);
		}

		PSOCreateInfo.pVS = pVS;
		PSOCreateInfo.pPS = pPS;

		PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;


		ShaderResourceVariableDesc Vars[] =
		{
			{SHADER_TYPE_PIXEL, "g_SubpassInputColor", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
			{SHADER_TYPE_PIXEL, "g_SubpassInputDepthZ", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
		};

		PSODesc.ResourceLayout.Variables = Vars;
		PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pAmbientLightPSO);
		ASSERT_EXPR(m_pAmbientLightPSO != nullptr);
	}


	void Tutorial07_RenderPass::createRenderPass()
	{
		// Attachment 0 - Color buffer
		// Attachment 1 - Depth Z
		// Attachment 2 - Depth buffer
		// Attachment 3 - Final color buffer
		constexpr uint32 NumAttachments = 4;

		// Prepare render pass attachment descriptions
		// Attachment 0 - Color buffer
		RenderPassAttachmentDesc Attachments[NumAttachments];
		Attachments[0].Format = TEX_FORMAT_RGBA8_UNORM;
		Attachments[0].InitialState = RESOURCE_STATE_RENDER_TARGET;
		Attachments[0].FinalState = RESOURCE_STATE_INPUT_ATTACHMENT;
		Attachments[0].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Attachments[0].StoreOp = ATTACHMENT_STORE_OP_DISCARD; // We will not need the result after the end of the render pass

		// Attachment 1 - Depth Z
		for (TEXTURE_FORMAT Fmt : {TEX_FORMAT_R32_FLOAT, TEX_FORMAT_R16_UNORM, TEX_FORMAT_R16_FLOAT})
		{
			if (m_pDevice->GetTextureFormatInfoExt(Fmt).BindFlags & BIND_RENDER_TARGET)
			{
				Attachments[1].Format = Fmt;
				break;
			}
		}

		if (Attachments[1].Format == TEX_FORMAT_UNKNOWN)
		{
			LOG_WARNING_MESSAGE("This device does not support rendering to any of R32_FLOAT, R16_UNORM or R16_FLOAT formats. Using R8 as fallback.");
			Attachments[1].Format = TEX_FORMAT_R8_UNORM;
		}

		Attachments[1].InitialState = RESOURCE_STATE_RENDER_TARGET;
		Attachments[1].FinalState = RESOURCE_STATE_INPUT_ATTACHMENT;
		Attachments[1].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Attachments[1].StoreOp = ATTACHMENT_STORE_OP_DISCARD; // We will not need the result after the end of the render pass

		// Attachment 2 - Depth buffer
		Attachments[2].Format = DEPTH_BUFFER_FORMAT;
		Attachments[2].InitialState = RESOURCE_STATE_DEPTH_WRITE;
		Attachments[2].FinalState = RESOURCE_STATE_DEPTH_WRITE;
		Attachments[2].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Attachments[2].StoreOp = ATTACHMENT_STORE_OP_DISCARD; // We will not need the result after the end of the render pass

		// Attachment 3 - Final color buffer
		Attachments[3].Format = m_pSwapChain->GetDesc().ColorBufferFormat;
		Attachments[3].InitialState = RESOURCE_STATE_RENDER_TARGET;
		Attachments[3].FinalState = RESOURCE_STATE_RENDER_TARGET;
		Attachments[3].LoadOp = ATTACHMENT_LOAD_OP_CLEAR;
		Attachments[3].StoreOp = ATTACHMENT_STORE_OP_STORE;

		// Subpass 1 - Render G-buffer
		// Subpass 2 - Lighting
		constexpr uint32 NumSubpasses = 2;

		SubpassDesc Subpasses[NumSubpasses];

		// Subpass 0 attachments - 2 render targets and depth buffer
		AttachmentReference RTAttachmentRefs0[] =
		{
			{0, RESOURCE_STATE_RENDER_TARGET},
			{1, RESOURCE_STATE_RENDER_TARGET}
		};
		AttachmentReference DepthAttachmentRef0 = { 2, RESOURCE_STATE_DEPTH_WRITE };

		// Subpass 1 attachments - 1 render target, depth buffer, 2 input attachments
		AttachmentReference RTAttachmentRefs1[] =
		{
			{3, RESOURCE_STATE_RENDER_TARGET}
		};
		AttachmentReference DepthAttachmentRef1 = { 2, RESOURCE_STATE_DEPTH_WRITE };
		AttachmentReference InputAttachmentRefs1[] =
		{
			{0, RESOURCE_STATE_INPUT_ATTACHMENT},
			{1, RESOURCE_STATE_INPUT_ATTACHMENT}
		};

		Subpasses[0].RenderTargetAttachmentCount = _countof(RTAttachmentRefs0);
		Subpasses[0].pRenderTargetAttachments = RTAttachmentRefs0;
		Subpasses[0].pDepthStencilAttachment = &DepthAttachmentRef0;

		Subpasses[1].RenderTargetAttachmentCount = _countof(RTAttachmentRefs1);
		Subpasses[1].pRenderTargetAttachments = RTAttachmentRefs1;
		Subpasses[1].pDepthStencilAttachment = &DepthAttachmentRef1;
		Subpasses[1].InputAttachmentCount = _countof(InputAttachmentRefs1);
		Subpasses[1].pInputAttachments = InputAttachmentRefs1;

		// We need to define dependency between subpasses 0 and 1 to ensure that
		// all writes are complete before we use the attachments for input in subpass 1.
		SubpassDependencyDesc Dependencies[1];
		Dependencies[0].SrcSubpass = 0;
		Dependencies[0].DstSubpass = 1;
		Dependencies[0].SrcStageMask = PIPELINE_STAGE_FLAG_RENDER_TARGET;
		Dependencies[0].DstStageMask = PIPELINE_STAGE_FLAG_PIXEL_SHADER;
		Dependencies[0].SrcAccessMask = ACCESS_FLAG_RENDER_TARGET_WRITE;
		Dependencies[0].DstAccessMask = ACCESS_FLAG_SHADER_READ;

		RenderPassDesc RPDesc;
		RPDesc.Name = "Deferred shading render pass desc";
		RPDesc.AttachmentCount = _countof(Attachments);
		RPDesc.pAttachments = Attachments;
		RPDesc.SubpassCount = _countof(Subpasses);
		RPDesc.pSubpasses = Subpasses;
		RPDesc.DependencyCount = _countof(Dependencies);
		RPDesc.pDependencies = Dependencies;

		m_pDevice->CreateRenderPass(RPDesc, &m_pRenderPass);
		ASSERT_EXPR(m_pRenderPass != nullptr);
	}

	RefCntAutoPtr<IFramebuffer> Tutorial07_RenderPass::createFramebuffer(ITextureView* pDstRenderTarget)
	{
		const RenderPassDesc& RPDesc = m_pRenderPass->GetDesc();
		const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
		const BIND_FLAGS MemorylessTexBindFlags = m_pDevice->GetAdapterInfo().Memory.MemorylessTextureBindFlags;

		// Create window-size offscreen render target
		TextureDesc TexDesc;
		TexDesc.Name = "Color G-buffer";
		TexDesc.Type = RESOURCE_DIM_TEX_2D;
		TexDesc.BindFlags = BIND_RENDER_TARGET | BIND_INPUT_ATTACHMENT;
		TexDesc.Format = RPDesc.pAttachments[0].Format;
		TexDesc.Width = SCDesc.Width;
		TexDesc.Height = SCDesc.Height;
		TexDesc.MipLevels = 1;
		TexDesc.MiscFlags =
			((MemorylessTexBindFlags & TexDesc.BindFlags) == TexDesc.BindFlags) ?
			MISC_TEXTURE_FLAG_MEMORYLESS :
			MISC_TEXTURE_FLAG_NONE;

		// Define optimal clear value
		TexDesc.ClearValue.Format = TexDesc.Format;
		TexDesc.ClearValue.Color[0] = 0.f;
		TexDesc.ClearValue.Color[1] = 0.f;
		TexDesc.ClearValue.Color[2] = 0.f;
		TexDesc.ClearValue.Color[3] = 1.f;

		if (!m_GBuffer.pColorBuffer)
			m_pDevice->CreateTexture(TexDesc, nullptr, &m_GBuffer.pColorBuffer);

		TexDesc.Name = "Depth Z G-buffer";
		TexDesc.Format = RPDesc.pAttachments[1].Format;
		TexDesc.MiscFlags =
			((MemorylessTexBindFlags & TexDesc.BindFlags) == TexDesc.BindFlags) ?
			MISC_TEXTURE_FLAG_MEMORYLESS :
			MISC_TEXTURE_FLAG_NONE;

		TexDesc.ClearValue.Format = TexDesc.Format;
		TexDesc.ClearValue.Color[0] = 1.f;
		TexDesc.ClearValue.Color[1] = 1.f;
		TexDesc.ClearValue.Color[2] = 1.f;
		TexDesc.ClearValue.Color[3] = 1.f;

		if (!m_GBuffer.pDepthZBuffer)
			m_pDevice->CreateTexture(TexDesc, nullptr, &m_GBuffer.pDepthZBuffer);


		TexDesc.Name = "Depth buffer";
		TexDesc.Format = RPDesc.pAttachments[2].Format;
		TexDesc.BindFlags = BIND_DEPTH_STENCIL;
		TexDesc.MiscFlags =
			((MemorylessTexBindFlags & TexDesc.BindFlags) == TexDesc.BindFlags) ?
			MISC_TEXTURE_FLAG_MEMORYLESS :
			MISC_TEXTURE_FLAG_NONE;

		TexDesc.ClearValue.Format = TexDesc.Format;
		TexDesc.ClearValue.DepthStencil.Depth = 1.f;
		TexDesc.ClearValue.DepthStencil.Stencil = 0;

		if (!m_GBuffer.pDepthBuffer)
			m_pDevice->CreateTexture(TexDesc, nullptr, &m_GBuffer.pDepthBuffer);


		ITextureView* pAttachments[] = //
		{
			m_GBuffer.pColorBuffer->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
			m_GBuffer.pDepthZBuffer->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
			m_GBuffer.pDepthBuffer->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
			pDstRenderTarget //
		};

		FramebufferDesc FBDesc;
		FBDesc.Name = "G-buffer framebuffer";
		FBDesc.pRenderPass = m_pRenderPass;
		FBDesc.AttachmentCount = _countof(pAttachments);
		FBDesc.ppAttachments = pAttachments;

		RefCntAutoPtr<IFramebuffer> pFramebuffer;
		m_pDevice->CreateFramebuffer(FBDesc, &pFramebuffer);
		ASSERT_EXPR(pFramebuffer != nullptr);


		// Create SRBs that reference the framebuffer textures

		if (!m_pLightVolumeSRB)
		{
			m_pLightVolumePSO->CreateShaderResourceBinding(&m_pLightVolumeSRB, true);
			if (IShaderResourceVariable* pInputColor = m_pLightVolumeSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SubpassInputColor"))
				pInputColor->Set(m_GBuffer.pColorBuffer->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (IShaderResourceVariable* pInputDepthZ = m_pLightVolumeSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SubpassInputDepthZ"))
				pInputDepthZ->Set(m_GBuffer.pDepthZBuffer->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		}

		if (!m_pAmbientLightSRB)
		{
			m_pAmbientLightPSO->CreateShaderResourceBinding(&m_pAmbientLightSRB, true);
			if (IShaderResourceVariable* pInputColor = m_pAmbientLightSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SubpassInputColor"))
				pInputColor->Set(m_GBuffer.pColorBuffer->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (IShaderResourceVariable* pInputDepthZ = m_pAmbientLightSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SubpassInputDepthZ"))
				pInputDepthZ->Set(m_GBuffer.pDepthZBuffer->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		}

		return pFramebuffer;
	}

	void Tutorial07_RenderPass::initLights()
	{
		// Randomly distribute lights within the volume

		FastRandReal<float> Rnd{ 0, 0, 1 };

		m_Lights.resize(m_LightsCount);
		for (LightAttribs& Light : m_Lights)
		{
			Light.Location = (float3{ Rnd(), Rnd(), Rnd() } - float3{ 0.5f, 0.5f, 0.5f }) * 2.0 * static_cast<float>(GRID_DIMENSION);
			Light.Size = 0.25f + Rnd() * 0.25f;
			Light.Color = float3{ Rnd(), Rnd(), Rnd() };
		}

		m_LightMoveDirs.resize(m_Lights.size());
		for (float3& MoveDir : m_LightMoveDirs)
		{
			MoveDir = (float3{ Rnd(), Rnd(), Rnd() } - float3{ 0.5f, 0.5f, 0.5f }) * 1.f;
		}
	}

	void Tutorial07_RenderPass::createLightsBuffer()
	{
		m_pLightsBuffer.Release();

		BufferDesc VertBuffDesc;
		VertBuffDesc.Name = "Lights instances buffer";
		VertBuffDesc.Usage = USAGE_DYNAMIC;
		VertBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
		VertBuffDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
		VertBuffDesc.Size = sizeof(LightAttribs) * m_LightsCount;

		m_pDevice->CreateBuffer(VertBuffDesc, nullptr, &m_pLightsBuffer);
	}

	void Tutorial07_RenderPass::drawScene()
	{
		// Bind vertex and index buffers
		IBuffer* pBuffs[] = { m_CubeVertexBuffer };
		// Note that RESOURCE_STATE_TRANSITION_MODE_TRANSITION are not allowed inside render pass!
		m_pImmediateContext->SetVertexBuffers(0, 1, pBuffs, nullptr, RESOURCE_STATE_TRANSITION_MODE_VERIFY, SET_VERTEX_BUFFERS_FLAG_RESET);
		m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		// Set the cube's pipeline state
		m_pImmediateContext->SetPipelineState(m_pCubePSO);

		// Commit the cube shader's resources
		m_pImmediateContext->CommitShaderResources(m_pCubeSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		// Draw the grid
		DrawIndexedAttribs DrawAttrs;
		DrawAttrs.IndexType = VT_UINT32; // Index type
		DrawAttrs.NumIndices = 36;
		DrawAttrs.NumInstances = GRID_DIMENSION * GRID_DIMENSION;
		DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL; // Verify the state of vertex and index buffers
		m_pImmediateContext->DrawIndexed(DrawAttrs);
	}

	void Tutorial07_RenderPass::applyLighting()
	{
		// Set the lighting PSO
		m_pImmediateContext->SetPipelineState(m_pAmbientLightPSO);

		// Commit shader resources
		m_pImmediateContext->CommitShaderResources(m_pAmbientLightSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		{
			// Draw quad
			DrawAttribs DrawAttrs;
			DrawAttrs.NumVertices = 4;
			DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL; // Verify the state of vertex and index buffers
			m_pImmediateContext->Draw(DrawAttrs);
		}

		{
			// Map the cube's constant buffer and fill it in with its view-projection matrix
			MapHelper<LightAttribs> LightsData(m_pImmediateContext, m_pLightsBuffer, MAP_WRITE, MAP_FLAG_DISCARD);
			memcpy(LightsData, m_Lights.data(), m_Lights.size() * sizeof(m_Lights[0]));
		}

		// Bind vertex and index buffers
		IBuffer* pBuffs[2] = { m_CubeVertexBuffer, m_pLightsBuffer };
		// Note that RESOURCE_STATE_TRANSITION_MODE_TRANSITION are not allowed inside render pass!
		m_pImmediateContext->SetVertexBuffers(0, _countof(pBuffs), pBuffs, nullptr, RESOURCE_STATE_TRANSITION_MODE_VERIFY, SET_VERTEX_BUFFERS_FLAG_RESET);
		m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		// Set the lighting PSO
		m_pImmediateContext->SetPipelineState(m_pLightVolumePSO);

		// Commit shader resources
		m_pImmediateContext->CommitShaderResources(m_pLightVolumeSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

		{
			// Draw lights
			DrawIndexedAttribs DrawAttrs;
			DrawAttrs.IndexType = VT_UINT32; // Index type
			DrawAttrs.NumIndices = 36;
			DrawAttrs.NumInstances = m_LightsCount;
			DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL; // Verify the state of vertex and index buffers
			m_pImmediateContext->DrawIndexed(DrawAttrs);
		}
	}

	void Tutorial07_RenderPass::updateLights(float fElapsedTime)
	{
		float3 VolumeMin{ -static_cast<float>(GRID_DIMENSION), -static_cast<float>(GRID_DIMENSION), -static_cast<float>(GRID_DIMENSION) };
		float3 VolumeMax{ +static_cast<float>(GRID_DIMENSION), +static_cast<float>(GRID_DIMENSION), +static_cast<float>(GRID_DIMENSION) };
		for (int light = 0; light < m_LightsCount; ++light)
		{
			LightAttribs& Light = m_Lights[light];
			float3& Dir = m_LightMoveDirs[light];
			Light.Location += Dir * fElapsedTime;
			auto ClampCoordinate = [](float& Coord, float& Dir, float Min, float Max) //
			{
				if (Coord < Min)
				{
					Coord += (Min - Coord) * 2.f;
					Dir *= -1.f;
				}
				else if (Coord > Max)
				{
					Coord -= (Coord - Max) * 2.f;
					Dir *= -1.f;
				}
			};
			ClampCoordinate(Light.Location.x, Dir.x, VolumeMin.x, VolumeMax.x);
			ClampCoordinate(Light.Location.y, Dir.y, VolumeMin.y, VolumeMax.y);
			ClampCoordinate(Light.Location.z, Dir.z, VolumeMin.z, VolumeMax.z);
		}
	}

	void Tutorial07_RenderPass::releaseWindowResources()
	{
		m_GBuffer = {};
		m_FramebufferCache.clear();
		m_pLightVolumeSRB.Release();
		m_pAmbientLightSRB.Release();
	}

	IFramebuffer* Tutorial07_RenderPass::getCurrentFramebuffer()
	{
		ITextureView* pCurrentBackBufferRTV = m_pDevice->GetDeviceInfo().IsGLDevice() ?
			nullptr :
			m_pSwapChain->GetCurrentBackBufferRTV();

		auto fb_it = m_FramebufferCache.find(pCurrentBackBufferRTV);
		if (fb_it != m_FramebufferCache.end())
		{
			return fb_it->second;
		}
		else
		{
			auto it = m_FramebufferCache.emplace(pCurrentBackBufferRTV, createFramebuffer(pCurrentBackBufferRTV));
			ASSERT_EXPR(it.second);
			return it.first->second;
		}
	}



} // namespace shz

