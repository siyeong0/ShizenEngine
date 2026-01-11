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
 // Definition of shz::IShaderBindingTable interface and related data structures

#include "Primitives/Object.h"
#include "Primitives/FlagEnum.h"
#include "GraphicsTypes.h"
#include "Constants.h"
#include "IBuffer.h"
#include "IPipelineState.h"
#include "ITopLevelAS.h"

namespace shz
{

	// {1EE12101-7010-4825-AA8E-AC6BB9858BD6}
	static constexpr INTERFACE_ID IID_ShaderBindingTable =
	{ 0x1ee12101, 0x7010, 0x4825, {0xaa, 0x8e, 0xac, 0x6b, 0xb9, 0x85, 0x8b, 0xd6} };

	// Shader binding table description.
	struct ShaderBindingTableDesc : public DeviceObjectAttribs
	{
		// Ray tracing pipeline state object from which shaders will be taken.
		IPipelineState* pPSO = nullptr;
	};


	// Defines shader binding table validation flags, see IShaderBindingTable::Verify().
	enum VERIFY_SBT_FLAGS : uint32
	{
		// Check that all shaders are bound or inactive.
		VERIFY_SBT_FLAG_SHADER_ONLY = 0x1,

		// Check that shader record data are initialized.
		VERIFY_SBT_FLAG_SHADER_RECORD = 0x2,

		// Check that all TLASes that were used in the SBT are alive and
		// shader binding indices have not changed.
		VERIFY_SBT_FLAG_TLAS = 0x4,

		// Enable all validations.
		VERIFY_SBT_FLAG_ALL = VERIFY_SBT_FLAG_SHADER_ONLY |
		VERIFY_SBT_FLAG_SHADER_RECORD |
		VERIFY_SBT_FLAG_TLAS
	};
	DEFINE_FLAG_ENUM_OPERATORS(VERIFY_SBT_FLAGS);


	// Shader binding table interface

	// Defines the methods to manipulate an SBT object
	struct SHZ_INTERFACE IShaderBindingTable : public IDeviceObject
	{
		// Returns the shader binding table description that was used to create the object
		virtual const ShaderBindingTableDesc& SHZ_CALL_TYPE GetDesc() const override = 0;

		// Checks that all shaders are bound, instances and geometries have not changed, shader record data are initialized.

		// \param [in] Flags - Flags that specify which type of validation to perform.
		// \return     true if SBT content is valid, and false otherwise.
		//
		// The function does not modify the data used by IDeviceContext::TraceRays() and
		// IDeviceContext::TraceRaysIndirect() commands, so they can run in parallel.
		//
		// This method is only implemented in development build and has no effect in release build.
		//
		// \note Access to the SBT must be externally synchronized.
		virtual bool Verify(VERIFY_SBT_FLAGS Flags) const = 0;


		// Resets the SBT with the new pipeline state. This is more efficient than creating a new SBT.

		// The function does not modify the data used by IDeviceContext::TraceRays() and
		// IDeviceContext::TraceRaysIndirect() commands, so they can run in parallel.
		//
		// \note Access to the SBT must be externally synchronized.
		virtual void Reset(IPipelineState* pPSO) = 0;


		// Resets hit groups in the SBT.

		// After TLAS or BLAS was rebuilt or updated, hit group shader bindings may have become invalid,
		// you can reset hit groups only and keep ray-gen, miss and callable shader bindings intact.
		//
		// The function does not modify the data used by IDeviceContext::TraceRays() and
		// IDeviceContext::TraceRaysIndirect() commands, so they can run in parallel.
		//
		// \note Access to the SBT must be externally synchronized.
		virtual void ResetHitGroups() = 0;


		// Binds a ray-generation shader.

		// \param [in] pShaderGroupName - Ray-generation shader name that was specified in RayTracingGeneralShaderGroup::Name
		//                                when the pipeline state was created.
		// \param [in] pData            - Shader record data, can be null.
		// \param [in] DataSize         - Shader record data size, should be equal to RayTracingPipelineDesc::ShaderRecordSize.
		//
		// The function does not modify the data used by IDeviceContext::TraceRays() and
		// IDeviceContext::TraceRaysIndirect() commands, so they can run in parallel.
		//
		// \note Access to the SBT must be externally synchronized.
		virtual void BindRayGenShader(
			const Char* pShaderGroupName,
			const void* pData = nullptr,
			uint32      DataSize = 0) = 0;


		// Binds a ray-miss shader.

		// \param [in] pShaderGroupName - Ray-miss shader name that was specified in RayTracingGeneralShaderGroup::Name
		//                                when the pipeline state was created. Can be null to make the shader inactive.
		// \param [in] MissIndex        - Miss shader offset in the shader binding table (aka ray type). This offset will
		//                                correspond to 'MissShaderIndex' argument of TraceRay() function in HLSL,
		//                                and 'missIndex' argument of traceRay() function in GLSL.
		// \param [in] pData            - Shader record data, can be null.
		// \param [in] DataSize         - Shader record data size, should be equal to RayTracingPipelineDesc::ShaderRecordSize.
		//
		// The function does not modify the data used by IDeviceContext::TraceRays() and
		// IDeviceContext::TraceRaysIndirect() commands, so they can run in parallel.
		//
		// \note Access to the SBT must be externally synchronized.
		virtual void BindMissShader(
			const Char* pShaderGroupName,
			uint32      MissIndex,
			const void* pData = nullptr,
			uint32      DataSize = 0) = 0;


		// Binds a hit group for the the specified geometry in the instance.

		// \param [in] pTLAS                    - Top-level AS that contains the given instance.
		// \param [in] pInstanceName            - Instance name that contains the geometry. This is the name that was used
		//                                        when the TLAS was created, see TLASBuildInstanceData::InstanceName.
		// \param [in] pGeometryName            - Geometry name in the instance, for which to bind the hit group.
		//                                        This is the name that was given to geometry when BLAS was created,
		//                                        see BLASBuildTriangleData::GeometryName and BLASBuildBoundingBoxData::GeometryName.
		// \param [in] RayOffsetInHitGroupIndex - Ray offset in the shader binding table (aka ray type). This offset will correspond
		//                                        to 'RayContributionToHitGroupIndex' argument of TraceRay() function in HLSL, and
		//                                        'sbtRecordOffset' argument of traceRay() function in GLSL.
		//                                        Must be less than HitShadersPerInstance.
		// \param [in] pShaderGroupName         - Hit group name that was specified in RayTracingTriangleHitShaderGroup::Name or
		//                                        RayTracingProceduralHitShaderGroup::Name when the pipeline state was created.
		//                                        Can be null to make the shader group inactive.
		// \param [in] pData                    - Shader record data, can be null.
		// \param [in] DataSize                 - Shader record data size, should be equal to RayTracingPipelineDesc::ShaderRecordSize.
		//
		// The function does not modify the data used by IDeviceContext::TraceRays() and
		// IDeviceContext::TraceRaysIndirect() commands, so they can run in parallel.
		//
		// \note
		//     Access to the BLAS that was used in the TLAS instance with name pInstanceName must be externally synchronized.\n
		//     Access to the TLAS must be externally synchronized.\n
		//     Access to the SBT must be externally synchronized.
		virtual void BindHitGroupForGeometry(
			ITopLevelAS* pTLAS,
			const Char* pInstanceName,
			const Char* pGeometryName,
			uint32       RayOffsetInHitGroupIndex,
			const Char* pShaderGroupName,
			const void* pData = nullptr,
			uint32       DataSize = 0) = 0;


		// Binds a hit group to the specified location in the table.

		// \param [in] BindingIndex     - Location of the hit group in the table.
		// \param [in] pShaderGroupName - Hit group name that was specified in RayTracingTriangleHitShaderGroup::Name or
		//                                RayTracingProceduralHitShaderGroup::Name when the pipeline state was created.
		//                                Can be null to make the shader group inactive.
		// \param [in] pData            - Shader record data, can be null.
		// \param [in] DataSize         - Shader record data size, should equal to RayTracingPipelineDesc::ShaderRecordSize.
		//
		// The function does not modify the data used by IDeviceContext::TraceRays() and
		// IDeviceContext::TraceRaysIndirect() commands, so they can run in parallel.
		//
		// \remarks    Use IBottomLevelAS::GetGeometryIndex(), ITopLevelAS::GetBuildInfo(),
		//             ITopLevelAS::GetInstanceDesc().ContributionToHitGroupIndex to calculate the binding index.
		//
		// \note Access to the SBT must be externally synchronized.
		virtual void BindHitGroupByIndex(
			uint32      BindingIndex,
			const Char* pShaderGroupName,
			const void* pData = nullptr,
			uint32      DataSize = 0) = 0;


		// Binds a hit group for all geometries in the specified instance.

		// \param [in] pTLAS                    - Top-level AS that contains the given instance.
		// \param [in] pInstanceName            - Instance name, for which to bind the hit group. This is the name that was used
		//                                        when the TLAS was created, see TLASBuildInstanceData::InstanceName.
		// \param [in] RayOffsetInHitGroupIndex - Ray offset in the shader binding table (aka ray type). This offset will
		//                                        correspond to 'RayContributionToHitGroupIndex' argument of TraceRay() function
		//                                        in HLSL, and 'sbtRecordOffset' argument of traceRay() function in GLSL.
		//                                        Must be less than HitShadersPerInstance.
		// \param [in] pShaderGroupName         - Hit group name that was specified in RayTracingTriangleHitShaderGroup::Name or
		//                                        RayTracingProceduralHitShaderGroup::Name when the pipeline state was created.
		//                                        Can be null to make the shader group inactive.
		// \param [in] pData                    - Shader record data, can be null.
		// \param [in] DataSize                 - Shader record data size, should be equal to RayTracingPipelineDesc::ShaderRecordSize.
		//
		// The function does not modify the data used by IDeviceContext::TraceRays() and
		// IDeviceContext::TraceRaysIndirect() commands, so they can run in parallel.
		//
		// \note Access to the SBT and TLAS must be externally synchronized.
		virtual void BindHitGroupForInstance(
			ITopLevelAS* pTLAS,
			const Char* pInstanceName,
			uint32       RayOffsetInHitGroupIndex,
			const Char* pShaderGroupName,
			const void* pData = nullptr,
			uint32       DataSize = 0) = 0;


		// Binds a hit group for all instances in the given top-level AS.

		// \param [in] pTLAS                    - Top-level AS, for which to bind the hit group.
		// \param [in] RayOffsetInHitGroupIndex - Ray offset in the shader binding table (aka ray type). This offset will
		//                                        correspond to 'RayContributionToHitGroupIndex' argument of TraceRay()
		//                                        function in HLSL, and 'sbtRecordOffset' argument of traceRay() function in GLSL.
		//                                        Must be less than HitShadersPerInstance.
		// \param [in] pShaderGroupName         - Hit group name that was specified in RayTracingTriangleHitShaderGroup::Name or
		//                                        RayTracingProceduralHitShaderGroup::Name when the pipeline state was created.
		//                                        Can be null to make the shader group inactive.
		// \param [in] pData                    - Shader record data, can be null.
		// \param [in] DataSize                 - Shader record data size, should be equal to RayTracingPipelineDesc::ShaderRecordSize.
		//
		// The function does not modify the data used by IDeviceContext::TraceRays() and
		// IDeviceContext::TraceRaysIndirect() commands, so they can run in parallel.
		//
		// \note Access to the SBT and TLAS must be externally synchronized. 
		virtual void BindHitGroupForTLAS(
			ITopLevelAS* pTLAS,
			uint32       RayOffsetInHitGroupIndex,
			const Char* pShaderGroupName,
			const void* pData = nullptr,
			uint32       DataSize = 0) = 0;


		// Binds a callable shader.

		// \param [in] pShaderGroupName - Callable shader name that was specified in RayTracingGeneralShaderGroup::Name
		//                                when the pipeline state was created. Can be null to make the shader inactive.
		// \param [in] CallableIndex    - Callable shader offset in the shader binding table. This offset will correspond to
		//                                'ShaderIndex' argument of CallShader() function in HLSL,
		//                                and 'callable' argument of executeCallable() function in GLSL.
		// \param [in] pData            - Shader record data, can be null.
		// \param [in] DataSize         - Shader record data size, should be equal to RayTracingPipelineDesc::ShaderRecordSize.
		//
		// The function does not modify the data used by IDeviceContext::TraceRays() and
		// IDeviceContext::TraceRaysIndirect() commands, so they can run in parallel.
		//
		// \note Access to the SBT must be externally synchronized.
		virtual void BindCallableShader(
			const Char* pShaderGroupName,
			uint32      CallableIndex,
			const void* pData = nullptr,
			uint32      DataSize = 0) = 0;
	};


} // namespace shz
