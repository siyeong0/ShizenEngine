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
 // Declaration of functions that initialize Direct3D12-based engine implementation

#include "Engine/RHI/Interface/IEngineFactory.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/RHI/Interface/ISwapChain.h"

#if SHZ_D3D12_SHARED
#    include "Engine/Interface/LoadEngineDll.h"
#endif

namespace shz
{

	struct ICommandQueueD3D12;

	// {72BD38B0-684A-4889-9C68-0A80EC802DDE}
	static constexpr INTERFACE_ID IID_EngineFactoryD3D12 =
	{ 0x72bd38b0, 0x684a, 0x4889, {0x9c, 0x68, 0xa, 0x80, 0xec, 0x80, 0x2d, 0xde} };

	// Engine factory for Direct3D12 rendering backend
	struct SHZ_INTERFACE IEngineFactoryD3D12 : public IEngineFactory
	{
		// Loads D3D12 DLL and entry points.

		// \param [in] DllName - D3D12 dll name.
		// \return               true if the library and entry points are loaded successfully and false otherwise.
		//
		// IEngineFactoryD3D12::CreateDeviceAndContextsD3D12() and
		// IEngineFactoryD3D12::AttachToD3D12Device() functions will automatically
		// load the DLL if it has not be loaded already.
		//
		// This method has no effect on UWP.
		virtual bool LoadD3D12(const char* DllName = "d3d12.dll") = 0;

		// Creates a render device and device contexts for Direct3D12-based engine implementation.

		// \param [in] EngineCI    - Engine creation info.
		// \param [out] ppDevice   - Address of the memory location where pointer to
		//                           the created device will be written.
		// \param [out] ppContexts - Address of the memory location where pointers to
		//                           the contexts will be written. Immediate context goes at
		//                           position 0. If `EngineCI.NumDeferredContexts > 0`,
		//                           pointers to the deferred contexts are written afterwards.
		virtual void CreateDeviceAndContextsD3D12(
			const EngineD3D12CreateInfo& EngineCI,
			IRenderDevice** ppDevice,
			IDeviceContext** ppContexts) = 0;

		// Creates a command queue from Direct3D12 native command queue.

		// \param [in]  pd3d12NativeDevice - Pointer to the native Direct3D12 device.
		// \param [in]  pd3d12NativeDevice - Pointer to the native Direct3D12 command queue.
		// \param [in]  pRawMemAllocator   - Pointer to the raw memory allocator.
		//                                   Must be the same as EngineCreateInfo::pRawMemAllocator in the following AttachToD3D12Device() call.
		// \param [out] ppCommandQueue     - Address of the memory location where pointer to the command queue will be written.
		virtual void CreateCommandQueueD3D12(
			void* pd3d12NativeDevice,
			void* pd3d12NativeCommandQueue,
			struct IMemoryAllocator* pRawMemAllocator,
			struct ICommandQueueD3D12** ppCommandQueue) = 0;

		// Attaches to existing Direct3D12 device.

		// \param [in] pd3d12NativeDevice - Pointer to the native Direct3D12 device.
		// \param [in] CommandQueueCount  - Number of command queues.
		// \param [in] ppCommandQueues    - Pointer to the array of command queues.
		//                                  Must be created from existing command queue using CreateCommandQueueD3D12().
		// \param [in] EngineCI           - Engine creation info.
		// \param [out] ppDevice          - Address of the memory location where pointer to
		//                                  the created device will be written
		// \param [out] ppContexts - Address of the memory location where pointers to
		//                           the contexts will be written. Immediate context goes at
		//                           position 0. If `EngineCI.NumDeferredContexts > 0`,
		//                           pointers to the deferred contexts are written afterwards.
		virtual void AttachToD3D12Device(
			void* pd3d12NativeDevice,
			uint32 CommandQueueCount,
			struct ICommandQueueD3D12** ppCommandQueues,
			const EngineD3D12CreateInfo& EngineCI,
			IRenderDevice** ppDevice,
			IDeviceContext** ppContexts) = 0;


		// Creates a swap chain for Direct3D12-based engine implementation.

		// \param [in] pDevice           - Pointer to the render device.
		// \param [in] pImmediateContext - Pointer to the immediate device context.
		//                                 Only graphics contexts are supported.
		// \param [in] SCDesc            - Swap chain description.
		// \param [in] FSDesc            - Fullscreen mode description.
		// \param [in] Window            - Platform-specific native window description that
		//                                 the swap chain will be associated with:
		//                                 * On Win32 platform, this is the window handle (`HWND`)
		//                                 * On Universal Windows Platform, this is the reference
		//                                   to the core window (`Windows::UI::Core::CoreWindow`)
		//
		// \param [out] ppSwapChain    - Address of the memory location where pointer to the new
		//                               swap chain will be written
		virtual void CreateSwapChainD3D12(
			IRenderDevice* pDevice,
			IDeviceContext* pImmediateContext,
			const SwapChainDesc& SwapChainDesc,
			const FullScreenModeDesc& FSDesc,
			const NativeWindow& Window,
			ISwapChain** ppSwapChain) = 0;


		// Enumerates available display modes for the specified output of the specified adapter.

		// \param [in] MinFeatureLevel - Minimum feature level of the adapter that was given to EnumerateAdapters().
		// \param [in] AdapterId       - Id of the adapter enumerated by EnumerateAdapters().
		// \param [in] OutputId        - Adapter output id.
		// \param [in] Format          - Display mode format.
		// \param [in, out] NumDisplayModes - Number of display modes. If DisplayModes is null, this
		//                                    value is overwritten with the number of display modes
		//                                    available for this output. If DisplayModes is not null,
		//                                    this value should contain the maximum number of elements
		//                                    to be written to DisplayModes array. It is overwritten with
		//                                    the actual number of display modes written.
		//
		// \note           D3D12 must be loaded before this method can be called, see IEngineFactoryD3D12::LoadD3D12.
		virtual void EnumerateDisplayModes(
			Version MinFeatureLevel,
			uint32 AdapterId,
			uint32 OutputId,
			TEXTURE_FORMAT Format,
			uint32& NumDisplayModes,
			DisplayModeAttribs* DisplayModes) = 0;
	};


	typedef struct IEngineFactoryD3D12* (*GetEngineFactoryD3D12Type)();

#if SHZ_D3D12_SHARED

	inline GetEngineFactoryD3D12Type LoadGraphicsEngineD3D12()
	{
		static GetEngineFactoryD3D12Type GetFactoryFunc = NULL;
		if (GetFactoryFunc == NULL)
		{
			GetFactoryFunc = (GetEngineFactoryD3D12Type)LoadEngineDll("GraphicsEngineD3D12", "GetEngineFactoryD3D12");
		}
		return GetFactoryFunc;
	}

#else

	struct IEngineFactoryD3D12* GetEngineFactoryD3D12();

#endif

	// Loads the graphics engine D3D12 implementation DLL if necessary and returns the engine factory.
	inline struct IEngineFactoryD3D12* LoadAndGetEngineFactoryD3D12()
	{
		GetEngineFactoryD3D12Type GetFactoryFunc = NULL;
#if SHZ_D3D12_SHARED
		GetFactoryFunc = LoadGraphicsEngineD3D12();
		if (GetFactoryFunc == NULL)
		{
			return NULL;
		}
#else
		GetFactoryFunc = GetEngineFactoryD3D12;
#endif
		return GetFactoryFunc();
	}


} // namespace shz
