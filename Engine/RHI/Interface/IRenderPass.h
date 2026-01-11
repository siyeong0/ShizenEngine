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
 // Definition of the shz::IRenderPass interface and related data structures

#include "IDeviceObject.h"

namespace shz
{

	// {B818DEC7-174D-447A-A8E4-94D21C57B40A}
	static constexpr struct INTERFACE_ID IID_RenderPass =
	{ 0xb818dec7, 0x174d, 0x447a, { 0xa8, 0xe4, 0x94, 0xd2, 0x1c, 0x57, 0xb4, 0xa } };


	// Render pass attachment load operation
	// Vulkan counterpart: [VkAttachmentLoadOp](https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#VkAttachmentLoadOp).
	// D3D12 counterpart: [D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE](https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_render_pass_beginning_access_type).
	enum ATTACHMENT_LOAD_OP : uint8
	{
		// The previous contents of the texture within the render area will be preserved.
		// Vulkan counterpart: VK_ATTACHMENT_LOAD_OP_LOAD.
		// D3D12 counterpart: D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE.
		ATTACHMENT_LOAD_OP_LOAD = 0,

		// The contents within the render area will be cleared to a uniform value, which is
		// specified when a render pass instance is begun.
		// Vulkan counterpart: VK_ATTACHMENT_LOAD_OP_CLEAR.
		// D3D12 counterpart: D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR.
		ATTACHMENT_LOAD_OP_CLEAR,

		// The previous contents within the area need not be preserved; the contents of
		// the attachment will be undefined inside the render area.
		// Vulkan counterpart: VK_ATTACHMENT_LOAD_OP_DONT_CARE.
		// D3D12 counterpart: D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD.
		ATTACHMENT_LOAD_OP_DISCARD,

		// Special value indicating the number of load operations in the enumeration.
		ATTACHMENT_LOAD_OP_COUNT
	};


	// Render pass attachment store operation
	// Vulkan counterpart: [VkAttachmentStoreOp](https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#VkAttachmentStoreOp).
	// D3D12 counterpart: [D3D12_RENDER_PASS_ENDING_ACCESS_TYPE](https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_render_pass_ending_access_type).
	enum ATTACHMENT_STORE_OP : uint8
	{
		// The contents generated during the render pass and within the render area are written to memory.
		// Vulkan counterpart: VK_ATTACHMENT_STORE_OP_STORE.
		// D3D12 counterpart: D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE.
		ATTACHMENT_STORE_OP_STORE = 0,

		// The contents within the render area are not needed after rendering, and may be discarded;
		// the contents of the attachment will be undefined inside the render area.
		// Vulkan counterpart: VK_ATTACHMENT_STORE_OP_DONT_CARE.
		// D3D12 counterpart: D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD.
		ATTACHMENT_STORE_OP_DISCARD,

		// Special value indicating the number of store operations in the enumeration.
		ATTACHMENT_STORE_OP_COUNT
	};



	// Render pass attachment description.
	struct RenderPassAttachmentDesc
	{
		// The format of the texture view that will be used for the attachment.
		TEXTURE_FORMAT          Format = TEX_FORMAT_UNKNOWN;

		// The number of samples in the texture.
		uint8                   SampleCount = 1;

		// Load operation.

		// Specifies how the contents of color and depth components of
		// the attachment are treated at the beginning of the subpass where it is first used.
		ATTACHMENT_LOAD_OP      LoadOp = ATTACHMENT_LOAD_OP_LOAD;

		// Store operation.

		// Defines how the contents of color and depth components of the attachment
		// are treated at the end of the subpass where it is last used.
		ATTACHMENT_STORE_OP     StoreOp = ATTACHMENT_STORE_OP_STORE;

		// Stencil load operation.

		// Specifies how the contents of the stencil component of the
		// attachment is treated at the beginning of the subpass where it is first used.
		// This value is ignored when the format does not have stencil component.
		ATTACHMENT_LOAD_OP      StencilLoadOp = ATTACHMENT_LOAD_OP_LOAD;

		// Stencil store operation.

		// Defines how the contents of the stencil component of the attachment
		// is treated at the end of the subpass where it is last used.
		// This value is ignored when the format does not have stencil component.
		ATTACHMENT_STORE_OP     StencilStoreOp = ATTACHMENT_STORE_OP_STORE;

		// The state the attachment texture subresource will be in when a render pass instance begins.
		RESOURCE_STATE          InitialState = RESOURCE_STATE_UNKNOWN;

		// The state the attachment texture subresource will be transitioned to when a render pass instance ends.
		RESOURCE_STATE          FinalState = RESOURCE_STATE_UNKNOWN;


		// Tests if two structures are equivalent

		// \param [in] rhs - reference to the structure to perform comparison with
		// \return
		// - true if all members of the two structures are equal.
		// - false otherwise
		constexpr bool operator == (const RenderPassAttachmentDesc& rhs) const
		{
			static_assert(sizeof(*this) == 16, "Did you add new members? Please handle them here");
			return  Format == rhs.Format &&
				SampleCount == rhs.SampleCount &&
				LoadOp == rhs.LoadOp &&
				StoreOp == rhs.StoreOp &&
				StencilLoadOp == rhs.StencilLoadOp &&
				StencilStoreOp == rhs.StencilStoreOp &&
				InitialState == rhs.InitialState &&
				FinalState == rhs.FinalState;
		}
		constexpr bool operator != (const RenderPassAttachmentDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};

	// Special constant indicating that the render pass attachment is not used.
	static constexpr uint32 ATTACHMENT_UNUSED = 0xFFFFFFFFU;

	// Attachment reference description.
	struct AttachmentReference
	{
		// Attachment index in the render pass attachment array.

		// Either an integer value identifying an attachment at the corresponding index in RenderPassDesc::pAttachments,
		// or ATTACHMENT_UNUSED to signify that this attachment is not used.
		uint32          AttachmentIndex = 0;

		// The state of the attachment during the subpass.
		RESOURCE_STATE  State = RESOURCE_STATE_UNKNOWN;

		constexpr AttachmentReference() noexcept {}

		constexpr AttachmentReference(uint32 _AttachmentIndex, RESOURCE_STATE  _State)noexcept
			: AttachmentIndex{ _AttachmentIndex }
			, State{ _State }
		{
		}

		// Tests if two structures are equivalent

		// \param [in] rhs - reference to the structure to perform comparison with
		// \return
		// - true if all members of the two structures are equal.
		// - false otherwise
		constexpr bool operator == (const AttachmentReference& rhs) const
		{
			static_assert(sizeof(*this) == 8, "Did you add new members? Please handle them here");
			return  AttachmentIndex == rhs.AttachmentIndex &&
				State == rhs.State;
		}

		constexpr bool operator != (const AttachmentReference& rhs) const
		{
			return !(*this == rhs);
		}
	};


	// Shading rate attachment description.
	struct ShadingRateAttachment
	{
		// Shading rate attachment reference, see shz::AttachmentReference.
		AttachmentReference Attachment = {};

		// The size of the shading rate tile in pixels.

		// Each texel in the attachment contains shading rate for the whole tile.
		// The size must be a power-of-two value between ShadingRateProperties::MinTileSize and ShadingRateProperties::MaxTileSize.
		// Keep zero to use the default tile size.
		uint32              TileSize[2] = {};

		constexpr ShadingRateAttachment() noexcept {}

		constexpr ShadingRateAttachment(
			const AttachmentReference& _Attachment,
			uint32 TileWidth,
			uint32 TileHeight) noexcept
			: Attachment{ _Attachment }
			, TileSize{ TileWidth, TileHeight }
		{
		}

		constexpr bool operator == (const ShadingRateAttachment& rhs) const
		{
			static_assert(sizeof(*this) == 16, "Did you add new members? Please handle them here");
			return  Attachment == rhs.Attachment &&
				TileSize[0] == rhs.TileSize[0] &&
				TileSize[1] == rhs.TileSize[1];
		}

		constexpr bool operator != (const ShadingRateAttachment& rhs) const
		{
			return !(*this == rhs);
		}
	};


	// Render pass subpass description.
	struct SubpassDesc
	{
		// The number of input attachments the subpass uses.
		uint32                      InputAttachmentCount = 0;

		// Pointer to the array of input attachments, see shz::AttachmentReference.
		const AttachmentReference* pInputAttachments = nullptr;

		// The number of color render target attachments.
		uint32                      RenderTargetAttachmentCount = 0;

		// Pointer to the array of color render target attachments, see shz::AttachmentReference.

		// Each element of the `pRenderTargetAttachments` array corresponds to an output in the pixel shader,
		// i.e. if the shader declares an output variable decorated with a render target index X, then it uses
		// the attachment provided in `pRenderTargetAttachments[X]`. If the attachment index is ATTACHMENT_UNUSED,
		// writes to this render target are ignored.
		const AttachmentReference* pRenderTargetAttachments = nullptr;

		// Pointer to the array of resolve attachments, see shz::AttachmentReference.

		// If `pResolveAttachments` is not `NULL`, each of its elements corresponds to a render target attachment
		// (the element in pRenderTargetAttachments at the same index), and a multisample resolve operation is
		// defined for each attachment. At the end of each subpass, multisample resolve operations read the subpass's
		// color attachments, and resolve the samples for each pixel within the render area to the same pixel location
		// in the corresponding resolve attachments, unless the resolve attachment index is ATTACHMENT_UNUSED.
		const AttachmentReference* pResolveAttachments = nullptr;

		// Pointer to the depth-stencil attachment, see shz::AttachmentReference.
		const AttachmentReference* pDepthStencilAttachment = nullptr;

		// The number of preserve attachments.
		uint32                      PreserveAttachmentCount = 0;

		// Pointer to the array of preserve attachments, see shz::AttachmentReference.
		const uint32* pPreserveAttachments = nullptr;

		// Pointer to the shading rate attachment, see shz::ShadingRateAttachment.
		const ShadingRateAttachment* pShadingRateAttachment = nullptr;

		// Tests if two structures are equivalent

		// \param [in] rhs - reference to the structure to perform comparison with
		// \return
		// - true if all members of the two structures are equal.
		// - false otherwise
		constexpr bool operator == (const SubpassDesc& rhs) const
		{
			if (InputAttachmentCount != rhs.InputAttachmentCount ||
				RenderTargetAttachmentCount != rhs.RenderTargetAttachmentCount ||
				PreserveAttachmentCount != rhs.PreserveAttachmentCount)
				return false;

			for (uint32 i = 0; i < InputAttachmentCount; ++i)
			{
				if (pInputAttachments[i] != rhs.pInputAttachments[i])
					return false;
			}

			for (uint32 i = 0; i < RenderTargetAttachmentCount; ++i)
			{
				if (pRenderTargetAttachments[i] != rhs.pRenderTargetAttachments[i])
					return false;
			}

			if ((pResolveAttachments == nullptr && rhs.pResolveAttachments != nullptr) ||
				(pResolveAttachments != nullptr && rhs.pResolveAttachments == nullptr))
				return false;

			if (pResolveAttachments != nullptr && rhs.pResolveAttachments != nullptr)
			{
				for (uint32 i = 0; i < RenderTargetAttachmentCount; ++i)
				{
					if (pResolveAttachments[i] != rhs.pResolveAttachments[i])
						return false;
				}
			}

			if ((pDepthStencilAttachment == nullptr) != (rhs.pDepthStencilAttachment == nullptr))
				return false;

			if (pDepthStencilAttachment != nullptr && rhs.pDepthStencilAttachment != nullptr)
			{
				if (*pDepthStencilAttachment != *rhs.pDepthStencilAttachment)
					return false;
			}

			if ((pPreserveAttachments == nullptr && rhs.pPreserveAttachments != nullptr) ||
				(pPreserveAttachments != nullptr && rhs.pPreserveAttachments == nullptr))
				return false;

			if (pPreserveAttachments != nullptr && rhs.pPreserveAttachments != nullptr)
			{
				for (uint32 i = 0; i < PreserveAttachmentCount; ++i)
				{
					if (pPreserveAttachments[i] != rhs.pPreserveAttachments[i])
						return false;
				}
			}

			if ((pShadingRateAttachment == nullptr) != (rhs.pShadingRateAttachment == nullptr))
				return false;

			if (pShadingRateAttachment != nullptr && rhs.pShadingRateAttachment != nullptr)
			{
				if (*pShadingRateAttachment != *rhs.pShadingRateAttachment)
					return false;
			}

			return true;
		}

		constexpr bool operator != (const SubpassDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};

	// Special subpass index value expanding synchronization scope outside a subpass.
	static constexpr uint32 SUBPASS_EXTERNAL = 0xFFFFFFFFU;

	// Subpass dependency description
	struct SubpassDependencyDesc
	{
		// The subpass index of the first subpass in the dependency, or SUBPASS_EXTERNAL.
		uint32                SrcSubpass = 0;

		// The subpass index of the second subpass in the dependency, or SUBPASS_EXTERNAL.
		uint32                DstSubpass = 0;

		// A bitmask of PIPELINE_STAGE_FLAGS specifying the source stage mask.
		PIPELINE_STAGE_FLAGS  SrcStageMask = PIPELINE_STAGE_FLAG_UNDEFINED;

		// A bitmask of PIPELINE_STAGE_FLAGS specifying the destination stage mask.
		PIPELINE_STAGE_FLAGS  DstStageMask = PIPELINE_STAGE_FLAG_UNDEFINED;

		// A bitmask of ACCESS_FLAGS specifying a source access mask.
		ACCESS_FLAGS          SrcAccessMask = ACCESS_FLAG_NONE;

		// A bitmask of ACCESS_FLAGS specifying a destination access mask.
		ACCESS_FLAGS          DstAccessMask = ACCESS_FLAG_NONE;

		// Tests if two structures are equivalent

		// \param [in] rhs - reference to the structure to perform comparison with
		// \return
		// - true if all members of the two structures are equal.
		// - false otherwise
		constexpr bool operator == (const SubpassDependencyDesc& rhs) const
		{
			static_assert(sizeof(*this) == 24, "Did you add new members? Please handle them here");
			return  SrcSubpass == rhs.SrcSubpass &&
				DstSubpass == rhs.DstSubpass &&
				SrcStageMask == rhs.SrcStageMask &&
				DstStageMask == rhs.DstStageMask &&
				SrcAccessMask == rhs.SrcAccessMask &&
				DstAccessMask == rhs.DstAccessMask;
		}

		constexpr bool operator != (const SubpassDependencyDesc& rhs) const
		{
			return !(*this == rhs);
		}
	};

	// Render pass description
	struct RenderPassDesc : public DeviceObjectAttribs
	{
		// The number of attachments used by the render pass.
		uint32                           AttachmentCount = 0;

		// Pointer to the array of subpass attachments, see shz::RenderPassAttachmentDesc.
		const RenderPassAttachmentDesc* pAttachments = nullptr;

		// The number of subpasses in the render pass.
		uint32                           SubpassCount = 0;

		// Pointer to the array of subpass descriptions, see shz::SubpassDesc.
		const SubpassDesc* pSubpasses = nullptr;

		// The number of memory dependencies between pairs of subpasses.
		uint32                           DependencyCount = 0;

		// Pointer to the array of subpass dependencies, see shz::SubpassDependencyDesc.
		const SubpassDependencyDesc* pDependencies = nullptr;

		// Tests if two render pass descriptions are equal.

		// \param [in] rhs - reference to the structure to compare with.
		//
		// \return     true if all members of the two structures *except for the Name* are equal,
		//             and false otherwise.
		//
		// \note   The operator ignores the Name field as it is used for debug purposes and
		//         doesn't affect the render pass properties.
		bool operator==(const RenderPassDesc& rhs) const noexcept
		{
			// Ignore Name. This is consistent with the hasher (HashCombiner<HasherType, RenderPassDesc>).
			if (AttachmentCount != rhs.AttachmentCount ||
				SubpassCount != rhs.SubpassCount ||
				DependencyCount != rhs.DependencyCount)
				return false;

			for (uint32 i = 0; i < AttachmentCount; ++i)
			{
				const auto& LhsAttach = pAttachments[i];
				const auto& rhsAttach = rhs.pAttachments[i];
				if (!(LhsAttach == rhsAttach))
					return false;
			}

			for (uint32 i = 0; i < SubpassCount; ++i)
			{
				const auto& LhsSubpass = pSubpasses[i];
				const auto& rhsSubpass = rhs.pSubpasses[i];
				if (!(LhsSubpass == rhsSubpass))
					return false;
			}

			for (uint32 i = 0; i < DependencyCount; ++i)
			{
				const auto& LhsDep = pDependencies[i];
				const auto& rhsDep = rhs.pDependencies[i];
				if (!(LhsDep == rhsDep))
					return false;
			}
			return true;
		}
		bool operator!=(const RenderPassDesc& rhs) const noexcept
		{
			return !(*this == rhs);
		}
	};

	// Render pass interface

	// Render pass  has no methods.
	class IRenderPass : public IDeviceObject
	{
	public:
		// Returns the render pass description.
		virtual const RenderPassDesc& SHZ_CALL_TYPE GetDesc() const override = 0;
	};

} // namespace shz
