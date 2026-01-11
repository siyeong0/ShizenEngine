/*
 *  Copyright 2019-2022 Diligent Graphics LLC
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

#include <mutex>
#include <vector>
#include <deque>

#include "Engine/RHI/Interface/ISwapChain.h"
#include "Engine/RHI/Interface/IRenderDevice.h"
#include "Engine/RHI/Interface/IDeviceContext.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

namespace shz
{

	class ScreenCapture
	{
	public:
		ScreenCapture(IRenderDevice* pDevice);

		void Capture(ISwapChain* pSwapChain, IDeviceContext* pContext, uint32 FrameId);

		struct CaptureInfo
		{
			RefCntAutoPtr<ITexture> pTexture;
			uint32                  Id = 0;

			explicit operator bool() const
			{
				return pTexture != nullptr;
			}
		};

		CaptureInfo GetCapture();
		bool        HasCapture();

		void RecycleStagingTexture(RefCntAutoPtr<ITexture>&& pTexture);

		size_t GetNumPendingCaptures()
		{
			std::lock_guard<std::mutex> Lock{ m_PendingTexturesMtx };
			return m_PendingTextures.size();
		}

	private:
		RefCntAutoPtr<IFence>        m_pFence;
		RefCntAutoPtr<IRenderDevice> m_pDevice;

		std::mutex                           m_AvailableTexturesMtx;
		std::vector<RefCntAutoPtr<ITexture>> m_AvailableTextures;

		std::mutex m_PendingTexturesMtx;
		struct PendingTextureInfo
		{
			PendingTextureInfo(RefCntAutoPtr<ITexture>&& _pTex, uint32 _Id, uint64 _Fence) 
				: pTex{ std::move(_pTex) }
				, Id{ _Id }
				, Fence{ _Fence }

			{
			}

			RefCntAutoPtr<ITexture> pTex;
			const uint32            Id;
			const uint64            Fence;
		};
		std::deque<PendingTextureInfo> m_PendingTextures;

		uint64 m_CurrentFenceValue = 1;
	};

} // namespace shz
