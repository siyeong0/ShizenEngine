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

#pragma once



 // \file
 // Definition of the shz::IPipelineResourceSignature interface and related data structures

#include "Primitives/Object.h"
#include "Platforms/Common/PlatformDefinitions.h"
#include "GraphicsTypes.h"
#include "IShader.h"
#include "ISampler.h"
#include "IShaderResourceVariable.h"
#include "IShaderResourceBinding.h"

namespace shz
{

	// Immutable sampler description.

	// An immutable sampler is compiled into the pipeline state and can't be changed.
	// It is generally more efficient than a regular sampler and should be used
	// whenever possible.
	struct ImmutableSamplerDesc
	{
		// Shader stages that this immutable sampler applies to. More than one shader stage can be specified.
		SHADER_TYPE ShaderStages = SHADER_TYPE_UNKNOWN;

		// The name of the sampler itself or the name of the texture variable that
		// this immutable sampler is assigned to if combined texture samplers are used.
		const Char* SamplerOrTextureName = nullptr;

		// Sampler description
		struct SamplerDesc Desc;

		constexpr ImmutableSamplerDesc() noexcept {}

		constexpr ImmutableSamplerDesc(SHADER_TYPE _ShaderStages, const Char* _SamplerOrTextureName, const SamplerDesc& _Desc) noexcept
			: ShaderStages(_ShaderStages)
			, SamplerOrTextureName(_SamplerOrTextureName)
			, Desc(_Desc)
		{
		}

		bool operator==(const ImmutableSamplerDesc& rhs) const noexcept
		{
			return ShaderStages == rhs.ShaderStages &&
				Desc == rhs.Desc &&
				SafeStrEqual(SamplerOrTextureName, rhs.SamplerOrTextureName);
		}
		bool operator!=(const ImmutableSamplerDesc& rhs) const noexcept
		{
			return !(*this == rhs);
		}
	};


	// Pipeline resource property flags.
	enum PIPELINE_RESOURCE_FLAGS : uint8
	{
		// Resource has no special properties
		PIPELINE_RESOURCE_FLAG_NONE = 0,

		// Indicates that dynamic buffers will never be bound to the resource
		// variable. Applies to SHADER_RESOURCE_TYPE_CONSTANT_BUFFER,
		// SHADER_RESOURCE_TYPE_BUFFER_UAV, SHADER_RESOURCE_TYPE_BUFFER_SRV resources.
		//
		// In Vulkan and Direct3D12 backends, dynamic buffers require extra work
		// at run time. If an application knows it will never bind a dynamic buffer to
		// the variable, it should use PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS flag
		// to improve performance. This flag is not required and non-dynamic buffers
		// will still work even if the flag is not used. It is an error to bind a
		// dynamic buffer to resource that uses
		// PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS flag.
		PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS = 1u << 0,

		// Indicates that a texture SRV will be combined with a sampler.
		// Applies to SHADER_RESOURCE_TYPE_TEXTURE_SRV resources.
		PIPELINE_RESOURCE_FLAG_COMBINED_SAMPLER = 1u << 1,

		// Indicates that this variable will be used to bind formatted buffers.
		// Applies to SHADER_RESOURCE_TYPE_BUFFER_UAV and SHADER_RESOURCE_TYPE_BUFFER_SRV
		// resources.
		//
		// In Vulkan backend formatted buffers require another descriptor type
		// as opposed to structured buffers. If an application will be using
		// formatted buffers with buffer UAVs and SRVs, it must specify the
		// PIPELINE_RESOURCE_FLAG_FORMATTED_BUFFER flag.
		PIPELINE_RESOURCE_FLAG_FORMATTED_BUFFER = 1u << 2,

		// Indicates that resource is a run-time sized shader array (e.g. an array without a specific size).
		PIPELINE_RESOURCE_FLAG_RUNTIME_ARRAY = 1u << 3,

		// Indicates that the resource is an input attachment in general layout, which allows simultaneously
		// reading from the resource through the input attachment and writing to it via color or depth-stencil
		// attachment.
		//
		// \note This flag is only valid in Vulkan.
		PIPELINE_RESOURCE_FLAG_GENERAL_INPUT_ATTACHMENT = 1u << 4,

		PIPELINE_RESOURCE_FLAG_LAST = PIPELINE_RESOURCE_FLAG_GENERAL_INPUT_ATTACHMENT
	};
	DEFINE_FLAG_ENUM_OPERATORS(PIPELINE_RESOURCE_FLAGS);


	// WebGPU-specific resource binding types.
	enum WEB_GPU_BINDING_TYPE : uint8
	{
		// Default resource binding.
		WEB_GPU_BINDING_TYPE_DEFAULT = 0,


		// When resource type is SHADER_RESOURCE_TYPE_SAMPLER, specifies the
		// WebGPU sampler binding type as "filtering".
		// This is the default sampler binding type if WEB_GPU_BINDING_TYPE_DEFAULT is used.
		WEB_GPU_BINDING_TYPE_FILTERING_SAMPLER,

		// When resource type is SHADER_RESOURCE_TYPE_SAMPLER, specifies the
		// WebGPU sampler binding type as "non-filtering".
		WEB_GPU_BINDING_TYPE_NON_FILTERING_SAMPLER,

		// When resource type is SHADER_RESOURCE_TYPE_SAMPLER, specifies the
		// WebGPU sampler binding type as "comparison".
		WEB_GPU_BINDING_TYPE_COMPARISON_SAMPLER,


		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV, specifies the
		// WebGPU texture sample type as "float".
		// This is the default texture sample type if WEB_GPU_BINDING_TYPE_DEFAULT is used.
		WEB_GPU_BINDING_TYPE_FLOAT_TEXTURE,

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV, specifies the
		// WebGPU texture sample type as "unfilterable-float".
		WEB_GPU_BINDING_TYPE_UNFILTERABLE_FLOAT_TEXTURE,

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV, specifies the
		// WebGPU texture sample type as "depth".
		WEB_GPU_BINDING_TYPE_DEPTH_TEXTURE,

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV, specifies the
		// WebGPU texture sample type as "sint".
		WEB_GPU_BINDING_TYPE_SINT_TEXTURE,

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV, specifies the
		// WebGPU texture sample type as "uint".
		WEB_GPU_BINDING_TYPE_UINT_TEXTURE,


		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV, specifies the
		// WebGPU texture sample type as "float" and the texture is multisampled.
		WEB_GPU_BINDING_TYPE_FLOAT_TEXTURE_MS,

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV, specifies the
		// WebGPU texture sample type as "unfilterable-float" and the texture is multisampled.
		WEB_GPU_BINDING_TYPE_UNFILTERABLE_FLOAT_TEXTURE_MS,

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV, specifies the
		// WebGPU texture sample type as "depth" and the texture is multisampled.
		WEB_GPU_BINDING_TYPE_DEPTH_TEXTURE_MS,

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV, specifies the
		// WebGPU texture sample type as "sint" and the texture is multisampled.
		WEB_GPU_BINDING_TYPE_SINT_TEXTURE_MS,

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV, specifies the
		// WebGPU texture sample type as "uint" and the texture is multisampled.
		WEB_GPU_BINDING_TYPE_UINT_TEXTURE_MS,


		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_UAV, specifies the
		// WebGPU storage texture access type as "write-only".
		// This is the default storage texture access type if WEB_GPU_BINDING_TYPE_DEFAULT is used.
		WEB_GPU_BINDING_TYPE_WRITE_ONLY_TEXTURE_UAV,

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_UAV, specifies the
		// WebGPU storage texture access type as "read-only".
		WEB_GPU_BINDING_TYPE_READ_ONLY_TEXTURE_UAV,

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_UAV, specifies the
		// WebGPU storage texture access type as "read-write".
		WEB_GPU_BINDING_TYPE_READ_WRITE_TEXTURE_UAV,

		WEB_GPU_BINDING_TYPE_COUNT
	};

	// WebGPU-specific resource attributes.
	struct WebGPUResourceAttribs
	{
		// WebGPU-specific binding type, see shz::WEB_GPU_BINDING_TYPE.
		WEB_GPU_BINDING_TYPE BindingType = WEB_GPU_BINDING_TYPE_DEFAULT;

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_SRV or SHADER_RESOURCE_TYPE_TEXTURE_UAV,
		// specifies the texture view dimension.
		// If not specified, the dimension is assumed to be RESOURCE_DIM_TEX_2D.
		RESOURCE_DIMENSION   TextureViewDim = RESOURCE_DIM_TEX_2D;

		// When resource type is SHADER_RESOURCE_TYPE_TEXTURE_UAV, the texture view format.
		TEXTURE_FORMAT       UAVTextureFormat = TEX_FORMAT_UNKNOWN;

		constexpr WebGPUResourceAttribs() noexcept {}

		constexpr WebGPUResourceAttribs(
			WEB_GPU_BINDING_TYPE _BindingType,
			RESOURCE_DIMENSION   _TextureViewDim,
			TEXTURE_FORMAT       _UAVTextureFormat = TEX_FORMAT_UNKNOWN) noexcept
			: BindingType(_BindingType)
			, TextureViewDim(_TextureViewDim)
			, UAVTextureFormat(_UAVTextureFormat)
		{
		}

		constexpr bool operator==(const WebGPUResourceAttribs& rhs) const noexcept
		{
			return BindingType == rhs.BindingType &&
				TextureViewDim == rhs.TextureViewDim &&
				UAVTextureFormat == rhs.UAVTextureFormat;
		}

		constexpr bool operator!=(const WebGPUResourceAttribs& rhs) const noexcept
		{
			return !(*this == rhs);
		}
	};

	// Pipeline resource description.
	struct PipelineResourceDesc
	{
		// Resource name in the shader
		const Char* Name = nullptr;

		// Shader stages that this resource applies to.

		// When multiple shader stages are specified, all stages will share the same resource.
		//
		// There may be multiple resources with the same name in different shader stages,
		// but the stages specified for different resources with the same name must not overlap.
		SHADER_TYPE                    ShaderStages = SHADER_TYPE_UNKNOWN;

		// Resource array size (must be 1 for non-array resources).
		uint32                         ArraySize = 1;

		// Resource type, see shz::SHADER_RESOURCE_TYPE.
		SHADER_RESOURCE_TYPE           ResourceType = SHADER_RESOURCE_TYPE_UNKNOWN;

		// Resource variable type, see shz::SHADER_RESOURCE_VARIABLE_TYPE.
		SHADER_RESOURCE_VARIABLE_TYPE  VarType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

		// Special resource flags, see shz::PIPELINE_RESOURCE_FLAGS.
		PIPELINE_RESOURCE_FLAGS        Flags = PIPELINE_RESOURCE_FLAG_NONE;

		// WebGPU-specific resource attributes.

		// WebGPU requires additional information for certain resources.
		// This member is used to provide that information.
		// The member is ignored by all backends other than WebGPU.
		WebGPUResourceAttribs 	       WebGPUAttribs = {};

		constexpr PipelineResourceDesc() noexcept {}

		constexpr PipelineResourceDesc(
			SHADER_TYPE _ShaderStages,
			const Char* _Name,
			uint32 _ArraySize,
			SHADER_RESOURCE_TYPE _ResourceType,
			SHADER_RESOURCE_VARIABLE_TYPE _VarType = PipelineResourceDesc{}.VarType,
			PIPELINE_RESOURCE_FLAGS _Flags = PipelineResourceDesc{}.Flags,
			WebGPUResourceAttribs _WebGPUAttribs = PipelineResourceDesc{}.WebGPUAttribs) noexcept
			: Name(_Name)
			, ShaderStages(_ShaderStages)
			, ArraySize(_ArraySize)
			, ResourceType(_ResourceType)
			, VarType(_VarType)
			, Flags(_Flags)
			, WebGPUAttribs(_WebGPUAttribs)
		{
		}

		constexpr PipelineResourceDesc(
			SHADER_TYPE _ShaderStages,
			const Char* _Name,
			SHADER_RESOURCE_TYPE _ResourceType,
			SHADER_RESOURCE_VARIABLE_TYPE _VarType = PipelineResourceDesc{}.VarType,
			PIPELINE_RESOURCE_FLAGS _Flags = PipelineResourceDesc{}.Flags,
			WebGPUResourceAttribs _WebGPUAttribs = PipelineResourceDesc{}.WebGPUAttribs) noexcept
			: Name(_Name)
			, ShaderStages(_ShaderStages)
			, ResourceType(_ResourceType)
			, VarType(_VarType)
			, Flags(_Flags)
			, WebGPUAttribs(_WebGPUAttribs)
		{
		}

		bool operator==(const PipelineResourceDesc& rhs) const noexcept
		{
			return ShaderStages == rhs.ShaderStages &&
				ArraySize == rhs.ArraySize &&
				ResourceType == rhs.ResourceType &&
				VarType == rhs.VarType &&
				Flags == rhs.Flags &&
				WebGPUAttribs == rhs.WebGPUAttribs &&
				SafeStrEqual(Name, rhs.Name);
		}
		bool operator!=(const PipelineResourceDesc& rhs) const noexcept
		{
			return !(*this == rhs);
		}
	};


	// Pipeline resource signature description.
	struct PipelineResourceSignatureDesc : public DeviceObjectAttribs
	{
		// A pointer to an array of resource descriptions. See shz::PipelineResourceDesc.
		const PipelineResourceDesc* Resources = nullptr;

		// The number of resources in Resources array.
		uint32  NumResources = 0;

		// A pointer to an array of immutable samplers. See shz::ImmutableSamplerDesc.
		const ImmutableSamplerDesc* ImmutableSamplers = nullptr;

		// The number of immutable samplers in ImmutableSamplers array.
		uint32  NumImmutableSamplers = 0;

		// Binding index that this resource signature uses.

		// Every resource signature must be assign to one signature slot.
		// The total number of slots is given by MAX_RESOURCE_SIGNATURES constant.
		// All resource signatures used by a pipeline state must be assigned
		// to different slots.
		uint8  BindingIndex = 0;

		// Whether to use combined texture samplers.

		// If set to true, textures will be combined with texture samplers.
		// The `CombinedSamplerSuffix` member defines the suffix added to the texture variable
		// name to get corresponding sampler name. When using combined samplers,
		// the sampler assigned to the shader resource view is automatically set when
		// the view is bound. Otherwise samplers need to be explicitly set similar to other
		// shader variables.
		bool UseCombinedTextureSamplers = false;

		// Combined sampler suffix.

		// If `UseCombinedTextureSamplers` is `true`, defines the suffix added to the
		// texture variable name to get corresponding sampler name.  For example,
		// for default value "_sampler", a texture named "tex" will be combined
		// with sampler named "tex_sampler".
		// If `UseCombinedTextureSamplers` is `false`, this member is ignored.
		const Char* CombinedSamplerSuffix = "_sampler";

		// Shader resource binding allocation granularity

		// This member defines the allocation granularity for internal resources required by
		// the shader resource binding object instances.
		uint32 SRBAllocationGranularity = 1;


		// Tests if two pipeline resource signature descriptions are equal.

		// \param [in] rhs - reference to the structure to compare with.
		//
		// \return     true if all members of the two structures *except for the Name* are equal,
		//             and false otherwise.
		//
		// \note   The operator ignores the Name field as it is used for debug purposes and
		//         doesn't affect the pipeline resource signature properties.
		bool operator==(const PipelineResourceSignatureDesc& rhs) const noexcept
		{
			// Ignore Name. This is consistent with the hasher (HashCombiner<HasherType, PipelineResourceSignatureDesc>).
			if (NumResources != rhs.NumResources ||
				NumImmutableSamplers != rhs.NumImmutableSamplers ||
				BindingIndex != rhs.BindingIndex ||
				UseCombinedTextureSamplers != rhs.UseCombinedTextureSamplers)
				return false;

			if (UseCombinedTextureSamplers && !SafeStrEqual(CombinedSamplerSuffix, rhs.CombinedSamplerSuffix))
				return false;

			// ignore SRBAllocationGranularity

			for (uint32 r = 0; r < NumResources; ++r)
			{
				if (!(Resources[r] == rhs.Resources[r]))
					return false;
			}
			for (uint32 s = 0; s < NumImmutableSamplers; ++s)
			{
				if (!(ImmutableSamplers[s] == rhs.ImmutableSamplers[s]))
					return false;
			}
			return true;
		}
		bool operator!=(const PipelineResourceSignatureDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};

	// {DCE499A5-F812-4C93-B108-D684A0B56118}
	static constexpr INTERFACE_ID IID_PipelineResourceSignature =
	{ 0xdce499a5, 0xf812, 0x4c93, {0xb1, 0x8, 0xd6, 0x84, 0xa0, 0xb5, 0x61, 0x18} };


	// Pipeline resource signature interface
	struct SHZ_INTERFACE IPipelineResourceSignature : public IDeviceObject
	{
		// Returns the pipeline resource signature description, see shz::PipelineResourceSignatureDesc.
		virtual const PipelineResourceSignatureDesc& GetDesc() const override = 0;

		// Creates a shader resource binding object

		// \param [out] ppShaderResourceBinding - Memory location where pointer to the new shader resource
		//                                        binding object is written.
		// \param [in] InitStaticResources      - If set to true, the method will initialize static resources in
		//                                        the created object, which has the exact same effect as calling
		//                                        IPipelineResourceSignature::InitializeStaticSRBResources().
		virtual void CreateShaderResourceBinding(
			IShaderResourceBinding** ppShaderResourceBinding,
			bool InitStaticResources = false) = 0;


		// Binds static resources for the specified shader stages in the pipeline resource signature.

		// \param [in] ShaderStages     - Flags that specify shader stages, for which resources will be bound.
		//                                Any combination of shz::SHADER_TYPE may be used.
		// \param [in] pResourceMapping - Pointer to the resource mapping interface.
		// \param [in] Flags            - Additional flags. See shz::BIND_SHADER_RESOURCES_FLAGS.
		virtual void BindStaticResources(
			SHADER_TYPE ShaderStages,
			IResourceMapping* pResourceMapping,
			BIND_SHADER_RESOURCES_FLAGS Flags) = 0;


		// Returns static shader resource variable. If the variable is not found,
		// returns nullptr.

		// \param [in] ShaderType - Type of the shader to look up the variable.
		//                          Must be one of shz::SHADER_TYPE.
		// \param [in] Name       - Name of the variable.
		//
		// If a variable is shared between multiple shader stages,
		// it can be accessed using any of those shader stages. Even
		// though IShaderResourceVariable instances returned by the method
		// may be different for different stages, internally they will
		// reference the same resource.
		//
		// Only static shader resource variables can be accessed using this method.
		// Mutable and dynamic variables are accessed through Shader Resource
		// Binding object.
		//
		// The method does not increment the reference counter of the
		// returned interface, and the application must *not* call Release()
		// unless it explicitly called AddRef().
		virtual IShaderResourceVariable* GetStaticVariableByName(SHADER_TYPE ShaderType, const Char* Name) = 0;


		// Returns static shader resource variable by its index.

		// \param [in] ShaderType - Type of the shader to look up the variable.
		//                          Must be one of shz::SHADER_TYPE.
		// \param [in] Index      - Shader variable index. The index must be between
		//                          0 and the total number of variables returned by
		//                          GetStaticVariableCount().
		//
		//
		// If a variable is shared between multiple shader stages,
		// it can be accessed using any of those shader stages. Even
		// though IShaderResourceVariable instances returned by the method
		// may be different for different stages, internally they will
		// reference the same resource.
		//
		// Only static shader resource variables can be accessed using this method.
		// Mutable and dynamic variables are accessed through Shader Resource
		// Binding object.
		//
		// The method does not increment the reference counter of the
		// returned interface, and the application must *not* call Release()
		// unless it explicitly called AddRef().
		virtual IShaderResourceVariable* GetStaticVariableByIndex(SHADER_TYPE ShaderType, uint32 Index) = 0;


		// Returns the number of static shader resource variables.

		// \param [in] ShaderType - Type of the shader.
		//
		// \remarks   Only static variables (that can be accessed directly through the PSO) are counted.
		//            Mutable and dynamic variables are accessed through Shader Resource Binding object.
		virtual uint32 GetStaticVariableCount(SHADER_TYPE ShaderType) const = 0;

		// Initializes static resources in the shader binding object.

		// If static shader resources were not initialized when the SRB was created,
		// this method must be called to initialize them before the SRB can be used.
		// The method should be called after all static variables have been initialized
		// in the signature.
		//
		// \param [in] pShaderResourceBinding - Shader resource binding object to initialize.
		//                                      The pipeline resource signature must be compatible
		//                                      with the shader resource binding object.
		//
		// If static resources have already been initialized in the SRB and the method
		// is called again, it will have no effect and a warning message will be displayed.
		virtual void InitializeStaticSRBResources(struct IShaderResourceBinding* pShaderResourceBinding) const = 0;

		// Copies static resource bindings to the destination signature.

		// \param [in] pDstSignature - Destination pipeline resource signature.
		//
		// \note   Destination signature must be compatible with this signature.
		virtual void CopyStaticResources(IPipelineResourceSignature* pDstSignature) const = 0;

		// Returns true if the signature is compatible with another one.

		// Two signatures are compatible if they contain identical resources and immutabke samplers,
		// defined in the same order disregarding their names.
		virtual bool IsCompatibleWith(const struct IPipelineResourceSignature* pPRS) const = 0;
	};


}
