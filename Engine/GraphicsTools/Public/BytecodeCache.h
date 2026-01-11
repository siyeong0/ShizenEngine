/*
 *  Copyright 2019-2025 Diligent Graphics LLC
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
 // Defines shz::IBytecodeCache interface
#include "Engine/RHI/Interface/IRenderDevice.h"

namespace shz
{

	struct BytecodeCacheCreateInfo
	{
		enum RENDER_DEVICE_TYPE DeviceType = RENDER_DEVICE_TYPE_UNDEFINED;
	};

	// {D1F8295F-F9D7-4CD4-9D13-D950FE7572C1}
	static constexpr INTERFACE_ID IID_BytecodeCache =
	{ 0xD1F8295F, 0xF9D7, 0x4CD4, {0x9D, 0x13, 0xD9, 0x50, 0xFE, 0x75, 0x72, 0xC1} };

	// Byte code cache interface
	struct SHZ_INTERFACE IBytecodeCache : public IObject
	{
		// Loads the cache data from the binary blob

		// \param [in] pData - A pointer to the cache data.
		// \return     true if the data was loaded successfully, and false otherwise.
		virtual bool Load(IDataBlob* pData) = 0;

		// Returns the byte code for the requested shader create parameters.

		// \param [in]  ShaderCI   - Shader create info to find the byte code for.
		// \param [out] ppByteCode - Address of the memory location where a pointer to the
		//                           data blob containing the byte code will be written.
		//                           The function calls AddRef(), so that the new object will have
		//                           one reference.
		virtual void GetBytecode(const ShaderCreateInfo& ShaderCI, IDataBlob** ppByteCode) = 0;

		// Adds the byte code to the cache.

		// \param [in] ShaderCI  - Shader create parameters for the byte code to add.
		// \param [in] pByteCode - A pointer to the byte code to add to the cache.
		//
		// \remarks    If the byte code for the given shader create parameters is already present
		//             in the cache, it is replaced.
		virtual void AddBytecode(const ShaderCreateInfo& ShaderCI, IDataBlob* pByteCode) = 0;

		// Removes the byte code from the cache.

		// \param [in] ShaderCI - Shader create information for the byte code to remove.
		virtual void RemoveBytecode(const ShaderCreateInfo& ShaderCI) = 0;

		// Writes the cache data to the binary data blob.

		// \param [out] ppDataBlob - Address of the memory location where a pointer to the
		//                           data blob containing the cache data will be written.
		//                           The function calls AddRef(), so that the new object will have
		//                           one reference.
		//
		// \remarks    The data produced by this method is intended to be used by the Load method.
		virtual void Store(IDataBlob** ppDataBlob) = 0;


		// Clears the cache and resets it to default state.
		virtual void Clear() = 0;
	};


	void CreateBytecodeCache(const BytecodeCacheCreateInfo& CreateInfo, IBytecodeCache** ppCache);


} // namespace shz
