#include "pch.h"
#include "RenderResourceCache.h"

#include "Engine/AssetRuntime/Public/AssetManager.h"

#include "Engine/GraphicsTools/Public/GraphicsUtilities.h"
#include "Engine/GraphicsTools/Public/MapHelper.hpp"

#include "Tools/Image/Public/TextureUtilities.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	// ------------------------------------------------------------
	// Slot helpers (StaticMeshRenderData)
	// ------------------------------------------------------------

	RenderResourceCache::Slot<StaticMeshRenderData>* RenderResourceCache::findSlot(
		Handle<StaticMeshRenderData> h,
		std::vector<Slot<StaticMeshRenderData>>& slots) noexcept
	{
		if (!h.IsValid())
			return nullptr;

		if (!Handle<StaticMeshRenderData>::IsAlive(h))
			return nullptr;

		const uint32 index = h.GetIndex();
		if (index == 0 || index >= static_cast<uint32>(slots.size()))
			return nullptr;

		auto& slot = slots[index];
		if (!slot.Owner.Get().IsValid())
			return nullptr;

		if (!slot.Value.has_value())
			return nullptr;

		return &slot;
	}

	const RenderResourceCache::Slot<StaticMeshRenderData>* RenderResourceCache::findSlot(
		Handle<StaticMeshRenderData> h,
		const std::vector<Slot<StaticMeshRenderData>>& slots) noexcept
	{
		if (!h.IsValid())
			return nullptr;

		if (!Handle<StaticMeshRenderData>::IsAlive(h))
			return nullptr;

		const uint32 index = h.GetIndex();
		if (index == 0 || index >= static_cast<uint32>(slots.size()))
			return nullptr;

		const auto& slot = slots[index];
		if (!slot.Owner.Get().IsValid())
			return nullptr;

		if (!slot.Value.has_value())
			return nullptr;

		return &slot;
	}

	// ------------------------------------------------------------
	// Slot helpers (MaterialInstance)
	// ------------------------------------------------------------

	RenderResourceCache::Slot<MaterialInstance>* RenderResourceCache::findSlot(
		Handle<MaterialInstance> h,
		std::vector<Slot<MaterialInstance>>& slots) noexcept
	{
		if (!h.IsValid())
			return nullptr;

		if (!Handle<MaterialInstance>::IsAlive(h))
			return nullptr;

		const uint32 index = h.GetIndex();
		if (index == 0 || index >= static_cast<uint32>(slots.size()))
			return nullptr;

		auto& slot = slots[index];
		if (!slot.Owner.Get().IsValid())
			return nullptr;

		if (!slot.Value.has_value())
			return nullptr;

		return &slot;
	}

	const RenderResourceCache::Slot<MaterialInstance>* RenderResourceCache::findSlot(
		Handle<MaterialInstance> h,
		const std::vector<Slot<MaterialInstance>>& slots) noexcept
	{
		if (!h.IsValid())
			return nullptr;

		if (!Handle<MaterialInstance>::IsAlive(h))
			return nullptr;

		const uint32 index = h.GetIndex();
		if (index == 0 || index >= static_cast<uint32>(slots.size()))
			return nullptr;

		const auto& slot = slots[index];
		if (!slot.Owner.Get().IsValid())
			return nullptr;

		if (!slot.Value.has_value())
			return nullptr;

		return &slot;
	}

	// ------------------------------------------------------------
	// Slot helpers (Texture)
	// ------------------------------------------------------------

	RenderResourceCache::SlotHV<ITexture, RefCntAutoPtr<ITexture>>* RenderResourceCache::findTexSlot(
		Handle<ITexture> h,
		std::vector<SlotHV<ITexture, RefCntAutoPtr<ITexture>>>& slots) noexcept
	{
		if (!h.IsValid())
			return nullptr;

		if (!Handle<ITexture>::IsAlive(h))
			return nullptr;

		const uint32 index = h.GetIndex();
		if (index == 0 || index >= static_cast<uint32>(slots.size()))
			return nullptr;

		auto& slot = slots[index];
		if (!slot.Owner.Get().IsValid())
			return nullptr;

		if (!slot.Value.has_value())
			return nullptr;

		return &slot;
	}

	const RenderResourceCache::SlotHV<ITexture, RefCntAutoPtr<ITexture>>* RenderResourceCache::findTexSlot(
		Handle<ITexture> h,
		const std::vector<SlotHV<ITexture, RefCntAutoPtr<ITexture>>>& slots) noexcept
	{
		if (!h.IsValid())
			return nullptr;

		if (!Handle<ITexture>::IsAlive(h))
			return nullptr;

		const uint32 index = h.GetIndex();
		if (index == 0 || index >= static_cast<uint32>(slots.size()))
			return nullptr;

		const auto& slot = slots[index];
		if (!slot.Owner.Get().IsValid())
			return nullptr;

		if (!slot.Value.has_value())
			return nullptr;

		return &slot;
	}

	// ============================================================
	// Lifecycle
	// ============================================================

	bool RenderResourceCache::Initialize(const RenderResourceCacheCreateInfo& createInfo)
	{
		ASSERT(m_CreateInfo.pDevice, "Device is null");
		ASSERT(m_CreateInfo.pAssetManager, "AssetManager is null.");

		m_CreateInfo = createInfo;
		m_pAssetManager = m_CreateInfo.pAssetManager;

		// Default material instance (cache-owned)
		{
			MaterialInstance mat = {};
			mat.OverrideBaseColorFactor(float3(1.0f, 1.0f, 1.0f));
			mat.OverrideOpacity(1.0f);
			mat.OverrideAlphaMode(MATERIAL_ALPHA_OPAQUE);
			mat.OverrideRoughness(0.5f);
			mat.OverrideMetallic(0.0f);

			UniqueHandle<MaterialInstance> owner = UniqueHandle<MaterialInstance>::Make();
			const Handle<MaterialInstance> h = owner.Get();
			const uint32 index = h.GetIndex();

			ensureSlotCapacity(index, m_MaterialSlots);

			Slot<MaterialInstance>& slot = m_MaterialSlots[index];
			ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "Default material slot already occupied.");

			slot.Owner = std::move(owner);
			slot.Value.emplace(std::move(mat));

			m_DefaultMaterial = h;
		}

		return true;
	}

	void RenderResourceCache::Cleanup()
	{
		for (Slot<StaticMeshRenderData>& s : m_MeshSlots)
		{
			s.Value.reset();
			s.Owner.Reset();
		}

		for (SlotHV<ITexture, RefCntAutoPtr<ITexture>>& s : m_TextureSlots)
		{
			s.Value.reset();
			s.Owner.Reset();
		}

		for (Slot<MaterialInstance>& s : m_MaterialSlots)
		{
			s.Value.reset();
			s.Owner.Reset();
		}

		m_MeshSlots.clear();
		m_TextureSlots.clear();
		m_MaterialSlots.clear();

		m_TexAssetToGpuHandle.clear();
		m_MatRenderDataTable.clear();

		m_DefaultMaterial = {};
		m_pAssetManager = nullptr;

		m_CreateInfo = {};
	}

	// ============================================================
	// Explicit destroy helpers
	// ============================================================

	bool RenderResourceCache::DestroyStaticMesh(Handle<StaticMeshRenderData> h)
	{
		auto* slot = findSlot(h, m_MeshSlots);
		if (!slot)
			return false;

		slot->Value.reset();
		slot->Owner.Reset();
		return true;
	}

	bool RenderResourceCache::DestroyMaterialInstance(Handle<MaterialInstance> h)
	{
		auto* slot = findSlot(h, m_MaterialSlots);
		if (!slot)
			return false;

		if (auto it = m_MatRenderDataTable.find(h); it != m_MatRenderDataTable.end())
			m_MatRenderDataTable.erase(it);

		slot->Value.reset();
		slot->Owner.Reset();
		return true;
	}

	bool RenderResourceCache::DestroyTextureGPU(Handle<ITexture> h)
	{
		auto* slot = findTexSlot(h, m_TextureSlots);
		if (!slot)
			return false;

		slot->Value.reset();
		slot->Owner.Reset();
		return true;
	}

	// ============================================================
	// TryGetMesh
	// ============================================================

	const StaticMeshRenderData* RenderResourceCache::TryGetMesh(Handle<StaticMeshRenderData> h) const noexcept
	{
		const auto* slot = findSlot(h, m_MeshSlots);
		if (!slot)
			return nullptr;

		return &slot->Value.value();
	}

	// ============================================================
	// GPU resource creation (from AssetManager-owned CPU assets)
	// ============================================================

	Handle<ITexture> RenderResourceCache::createTextureGPU(Handle<TextureAsset> h)
	{
		if (!h.IsValid())
			return {};

		// Cache hit
		if (auto it = m_TexAssetToGpuHandle.find(h); it != m_TexAssetToGpuHandle.end())
		{
			const Handle<ITexture> cached = it->second;

			if (Handle<ITexture>::IsAlive(cached))
			{
				const auto* slot = findTexSlot(cached, m_TextureSlots);
				if (slot)
					return cached;
			}

			// stale map
			m_TexAssetToGpuHandle.erase(it);
		}

		const TextureAsset& texAsset = m_pAssetManager->GetTexture(h);
		TextureLoadInfo loadInfo = texAsset.BuildTextureLoadInfo();

		RefCntAutoPtr<ITexture> pTex;
		CreateTextureFromFile(texAsset.GetSourcePath().c_str(), loadInfo, m_CreateInfo.pDevice, &pTex);
		if (!pTex)
			return {};

		UniqueHandle<ITexture> owner = UniqueHandle<ITexture>::Make();
		const Handle<ITexture> gpuHandle = owner.Get();
		const uint32 index = gpuHandle.GetIndex();

		ensureSlotCapacity(index, m_TextureSlots);

		auto& slot = m_TextureSlots[index];
		ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "Texture slot already occupied.");

		slot.Owner = std::move(owner);
		slot.Value.emplace(pTex);

		m_TexAssetToGpuHandle.emplace(h, gpuHandle);
		return gpuHandle;
	}

	Handle<MaterialInstance> RenderResourceCache::createMaterialInstance(Handle<MaterialAsset> h)
	{
		if (!h.IsValid())
			return m_DefaultMaterial;

		const MaterialAsset& matAsset = m_pAssetManager->GetMaterial(h);

		MaterialInstance inst = {};

		const auto& P = matAsset.GetParams();

		inst.OverrideBaseColorFactor(float3(P.BaseColor.x, P.BaseColor.y, P.BaseColor.z));
		inst.OverrideOpacity(P.BaseColor.w);

		inst.OverrideMetallic(P.Metallic);
		inst.OverrideRoughness(P.Roughness);

		inst.OverrideNormalScale(P.NormalScale);
		inst.OverrideOcclusionStrength(P.Occlusion);

		inst.OverrideEmissiveFactor(P.EmissiveColor * P.EmissiveIntensity);
		inst.OverrideAlphaCutoff(P.AlphaCutoff);

		switch (matAsset.GetOptions().AlphaMode)
		{
		case MATERIAL_ALPHA_OPAQUE: inst.OverrideAlphaMode(MATERIAL_ALPHA_OPAQUE); break;
		case MATERIAL_ALPHA_MASK:   inst.OverrideAlphaMode(MATERIAL_ALPHA_MASK);   break;
		case MATERIAL_ALPHA_BLEND:  inst.OverrideAlphaMode(MATERIAL_ALPHA_BLEND);  break;
		default:                    inst.OverrideAlphaMode(MATERIAL_ALPHA_OPAQUE); break;
		}

		// Textures (MaterialAsset stores TextureAsset by value currently)
		if (matAsset.HasTexture(MATERIAL_TEX_ALBEDO))
		{
			Handle<TextureAsset> hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MATERIAL_TEX_ALBEDO));
			inst.OverrideBaseColorTexture(hTex);
		}

		if (matAsset.HasTexture(MATERIAL_TEX_NORMAL))
		{
			Handle<TextureAsset> hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MATERIAL_TEX_NORMAL));
			inst.OverrideNormalTexture(hTex);
		}

		if (matAsset.HasTexture(MATERIAL_TEX_ORM))
		{
			Handle<TextureAsset> hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MATERIAL_TEX_ORM));
			inst.OverrideMetallicRoughnessTexture(hTex);
		}

		if (matAsset.HasTexture(MATERIAL_TEX_EMISSIVE))
		{
			Handle<TextureAsset> hTex = m_pAssetManager->RegisterTexture(matAsset.GetTexture(MATERIAL_TEX_EMISSIVE));
			inst.OverrideEmissiveTexture(hTex);
		}

		UniqueHandle<MaterialInstance> owner = UniqueHandle<MaterialInstance>::Make();
		const Handle<MaterialInstance> hInst = owner.Get();
		const uint32 index = hInst.GetIndex();

		ensureSlotCapacity(index, m_MaterialSlots);

		auto& slot = m_MaterialSlots[index];
		ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "MaterialInstance slot already occupied.");

		slot.Owner = std::move(owner);
		slot.Value.emplace(std::move(inst));

		return hInst;
	}

	Handle<StaticMeshRenderData> RenderResourceCache::CreateStaticMesh(Handle<StaticMeshAsset> h)
	{
		if (!h.IsValid())
			return {};

		const StaticMeshAsset& meshAsset = m_pAssetManager->GetStaticMesh(h);

		UniqueHandle<StaticMeshRenderData> owner = UniqueHandle<StaticMeshRenderData>::Make();
		const Handle<StaticMeshRenderData> handle = owner.Get();
		const uint32 index = handle.GetIndex();

		ensureSlotCapacity(index, m_MeshSlots);

		auto& slot = m_MeshSlots[index];
		ASSERT(!slot.Owner.Get().IsValid() && !slot.Value.has_value(), "StaticMeshRenderData slot already occupied.");

		StaticMeshRenderData outMesh = {};

		struct SimpleVertex
		{
			float3 Pos;
			float2 UV;
			float3 Normal;
			float3 Tangent;
		};

		const uint32 vtxCount = meshAsset.GetVertexCount();
		outMesh.NumVertices = vtxCount;
		outMesh.VertexStride = sizeof(SimpleVertex);
		outMesh.LocalBounds = meshAsset.GetBounds();

		const auto& positions = meshAsset.GetPositions();
		const auto& normals = meshAsset.GetNormals();
		const auto& tangents = meshAsset.GetTangents();
		const auto& uvs = meshAsset.GetTexCoords();

		std::vector<SimpleVertex> vbCPU;
		vbCPU.resize(vtxCount);

		for (uint32 i = 0; i < vtxCount; ++i)
		{
			SimpleVertex v = {};
			v.Pos = positions[i];
			v.UV = uvs[i];
			v.Normal = normals[i];
			v.Tangent = tangents[i];
			vbCPU[i] = v;
		}

		// VB
		{
			BufferDesc VBDesc = {};
			VBDesc.Name = "StaticMesh VB";
			VBDesc.Usage = USAGE_IMMUTABLE;
			VBDesc.BindFlags = BIND_VERTEX_BUFFER;
			VBDesc.Size = static_cast<uint64>(vbCPU.size() * sizeof(SimpleVertex));

			BufferData VBData = {};
			VBData.pData = vbCPU.data();
			VBData.DataSize = static_cast<uint64>(vbCPU.size() * sizeof(SimpleVertex));

			m_CreateInfo.pDevice->CreateBuffer(VBDesc, &VBData, &outMesh.VertexBuffer);
			if (!outMesh.VertexBuffer)
				return {};
		}

		outMesh.Sections.clear();

		const bool useU32 = (meshAsset.GetIndexType() == VT_UINT32);
		const auto& idx32 = meshAsset.GetIndicesU32();
		const auto& idx16 = meshAsset.GetIndicesU16();

		auto CreateSectionIB = [&](MeshSection& sec, const void* pIndexData, uint64 indexDataBytes) -> bool
		{
			if (!pIndexData || indexDataBytes == 0)
				return false;

			BufferDesc IBDesc = {};
			IBDesc.Name = "StaticMesh IB";
			IBDesc.Usage = USAGE_IMMUTABLE;
			IBDesc.BindFlags = BIND_INDEX_BUFFER;
			IBDesc.Size = indexDataBytes;

			BufferData IBData = {};
			IBData.pData = pIndexData;
			IBData.DataSize = indexDataBytes;

			m_CreateInfo.pDevice->CreateBuffer(IBDesc, &IBData, &sec.IndexBuffer);
			return (sec.IndexBuffer != nullptr);
		};

		const auto& assetSections = meshAsset.GetSections();
		if (!assetSections.empty())
		{
			outMesh.Sections.reserve(assetSections.size());

			for (const StaticMeshAsset::Section& asec : assetSections)
			{
				if (asec.IndexCount == 0)
					continue;

				MeshSection sec = {};
				sec.NumIndices = asec.IndexCount;
				sec.IndexType = useU32 ? VT_UINT32 : VT_UINT16;
				sec.LocalBounds = asec.LocalBounds;
				sec.StartIndex = 0;

				const uint32 first = asec.FirstIndex;
				const uint32 count = asec.IndexCount;

				const void* pData = nullptr;
				uint64 bytes = 0;

				if (useU32)
				{
					pData = (idx32.empty()) ? nullptr : static_cast<const void*>(idx32.data() + first);
					bytes = static_cast<uint64>(count) * sizeof(uint32);
				}
				else
				{
					pData = (idx16.empty()) ? nullptr : static_cast<const void*>(idx16.data() + first);
					bytes = static_cast<uint64>(count) * sizeof(uint16);
				}

				if (!CreateSectionIB(sec, pData, bytes))
					return {};

				if (meshAsset.HasMaterial())
				{
					const MaterialAsset& slotMat = meshAsset.GetMaterialSlot(asec.MaterialSlot);
					Handle<MaterialAsset> hMatAsset = m_pAssetManager->RegisterMaterial(slotMat);
					sec.Material = createMaterialInstance(hMatAsset);
				}
				else
				{
					sec.Material = m_DefaultMaterial;
				}
				outMesh.Sections.push_back(sec);
			}
		}
		else
		{
			MeshSection sec = {};
			sec.NumIndices = meshAsset.GetIndexCount();
			sec.IndexType = useU32 ? VT_UINT32 : VT_UINT16;
			sec.StartIndex = 0;

			if (!CreateSectionIB(sec, meshAsset.GetIndexData(), meshAsset.GetIndexDataSizeBytes()))
				return {};

			sec.Material = m_DefaultMaterial;
			outMesh.Sections.push_back(sec);
		}

		slot.Owner = std::move(owner);
		slot.Value.emplace(std::move(outMesh));
		return handle;
	}

	// ============================================================
	// MaterialRenderData (SRB/PSO bindings)
	// ============================================================

	MaterialRenderData* RenderResourceCache::GetOrCreateMaterialRenderData(
		Handle<MaterialInstance> h,
		const RefCntAutoPtr<IPipelineState>& pPSO,
		const RefCntAutoPtr<IBuffer>& pFrameCB,
		const RefCntAutoPtr<IBuffer>& pObjectCB,
		IDeviceContext* pCtx)
	{
		if (!h.IsValid())
		{
			return nullptr;
		}

		const auto* InstSlot = findSlot(h, m_MaterialSlots);
		if (!InstSlot)
			return nullptr;

		if (!pPSO || !pFrameCB || !pObjectCB || !pCtx)
			return nullptr;

		// cache hit
		if (auto It = m_MatRenderDataTable.find(h); It != m_MatRenderDataTable.end())
			return &It->second;

		const MaterialInstance& MatInst = InstSlot->Value.value();

		// ------------------------------------------------------------
		// Default 1x1 SRVs (created once)
		// ------------------------------------------------------------
		auto PackRGBA8 = [](uint8 r, uint8 g, uint8 b, uint8 a) -> uint32
		{
			return (uint32(r) << 0) | (uint32(g) << 8) | (uint32(b) << 16) | (uint32(a) << 24);
		};
		auto Create1x1Texture = [&](const char* name, uint32 rgba, RefCntAutoPtr<ITexture>& outTex) -> bool
		{
			if (outTex)
				return true;

			auto* pDevice = m_CreateInfo.pDevice.RawPtr();
			if (!pDevice)
				return false;

			TextureDesc desc = {};
			desc.Name = name;
			desc.Type = RESOURCE_DIM_TEX_2D;
			desc.Width = 1;
			desc.Height = 1;
			desc.MipLevels = 1;
			desc.Format = TEX_FORMAT_RGBA8_UNORM;
			desc.Usage = USAGE_IMMUTABLE;
			desc.BindFlags = BIND_SHADER_RESOURCE;

			TextureSubResData sub = {};
			sub.pData = &rgba;
			sub.Stride = sizeof(uint32);

			TextureData data = {};
			data.pSubResources = &sub;
			data.NumSubresources = 1;

			pDevice->CreateTexture(desc, &data, &outTex);
			return (outTex != nullptr);
		};

		// Create defaults once (per RenderResourceCache instance)
		if (!m_DefaultTextures.White)
		{
			// BaseColor default: white
			if (!Create1x1Texture("DefaultWhite1x1", PackRGBA8(255, 255, 255, 255), m_DefaultTextures.White))
				return nullptr;

			// Normal default: (0.5, 0.5, 1.0) in UNORM => (128, 128, 255)
			if (!Create1x1Texture("DefaultNormal1x1", PackRGBA8(128, 128, 255, 255), m_DefaultTextures.Normal))
				return nullptr;

			// MetallicRoughness default (glTF): roughness=1 (G=255), metallic=0 (B=0)
			// R is unused in your shader, but keep 0. A=255
			if (!Create1x1Texture("DefaultMR1x1", PackRGBA8(0, 255, 0, 255), m_DefaultTextures.MetallicRoughness))
				return nullptr;

			// AO default: 1
			if (!Create1x1Texture("DefaultAO1x1", PackRGBA8(255, 255, 255, 255), m_DefaultTextures.AO))
				return nullptr;

			// Emissive default: 0
			if (!Create1x1Texture("DefaultEmissive1x1", PackRGBA8(0, 0, 0, 255), m_DefaultTextures.Emissive))
				return nullptr;
		}

		// Default SRVs (raw pointers are OK as long as textures are owned by m_DefaultTextures)
		ITextureView* pDefaultBaseColorSRV = m_DefaultTextures.White->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		ITextureView* pDefaultNormalSRV = m_DefaultTextures.Normal->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		ITextureView* pDefaultMRSRV = m_DefaultTextures.MetallicRoughness->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		ITextureView* pDefaultAOSRV = m_DefaultTextures.AO->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
		ITextureView* pDefaultEmissiveSRV = m_DefaultTextures.Emissive->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

		if (!pDefaultBaseColorSRV || !pDefaultNormalSRV || !pDefaultMRSRV || !pDefaultAOSRV || !pDefaultEmissiveSRV)
		{
			ASSERT(false);
			return nullptr;
		}

		// ------------------------------------------------------------
		// Read instance parameters (fallbacks)
		// ------------------------------------------------------------
		const float3 BaseColor = MatInst.GetBaseColorFactor(float3(1, 1, 1));
		const float  Opacity = MatInst.GetOpacity(1.0f);
		const float  Metallic = MatInst.GetMetallic(0.0f);
		const float  Roughness = MatInst.GetRoughness(0.5f);
		const float  NormalScale = MatInst.GetNormalScale(1.0f);
		const float  Occlusion = MatInst.GetOcclusionStrength(1.0f);
		const float3 Emissive = MatInst.GetEmissiveFactor(float3(0, 0, 0));
		const auto   AlphaMode = MatInst.GetAlphaMode(MATERIAL_ALPHA_OPAQUE);
		const float  AlphaCutoff = MatInst.GetAlphaCutoff(0.5f);

		MaterialRenderData RD = {};
		RD.InstanceHandle = h;
		RD.RenderQueue = MaterialRenderData::GetQueueFromAlphaMode(AlphaMode);

		RD.BaseColor = float4(BaseColor.x, BaseColor.y, BaseColor.z, Opacity);
		RD.Metallic = Metallic;
		RD.Roughness = Roughness;
		RD.NormalScale = NormalScale;
		RD.OcclusionStrength = Occlusion;
		RD.Emissive = Emissive;
		RD.AlphaCutoff = AlphaCutoff;

		RD.pPSO = pPSO;

		RD.pPSO->CreateShaderResourceBinding(&RD.pSRB, true);
		if (!RD.pSRB)
			return nullptr;

		// ------------------------------------------------------------
		// Bind constant buffers
		// ------------------------------------------------------------
		ASSERT(pObjectCB);
		if (auto* Var = RD.pSRB->GetVariableByName(SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS"))
		{
			Var->Set(pObjectCB);
		}

		// ------------------------------------------------------------
		// Resolve texture SRVs (override -> GPU texture -> SRV; else default)
		// ------------------------------------------------------------
		auto ResolveSRV = [&](Handle<TextureAsset> TexAsset,
			ITextureView* pDefaultSRV,
			uint32 FlagBit,
			uint32& InOutFlags) -> ITextureView*
		{
			if (!TexAsset.IsValid())
				return pDefaultSRV;

			const Handle<ITexture> TexGPUHandle = createTextureGPU(TexAsset);
			if (!TexGPUHandle.IsValid())
				return pDefaultSRV;

			const auto* TexSlot = findTexSlot(TexGPUHandle, m_TextureSlots);
			if (!TexSlot || !TexSlot->Value.has_value())
				return pDefaultSRV;

			const RefCntAutoPtr<ITexture>& pTex = TexSlot->Value.value();
			if (!pTex)
				return pDefaultSRV;

			if (ITextureView* pSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))
			{
				InOutFlags |= FlagBit;
				return pSRV;
			}

			return pDefaultSRV;
		};

		uint32 Flags = 0;

		ITextureView* pBaseColorSRV =
			ResolveSRV(MatInst.GetBaseColorTextureOverride(), pDefaultBaseColorSRV, hlsl::MAT_HAS_BASECOLOR, Flags);

		ITextureView* pNormalSRV =
			ResolveSRV(MatInst.GetNormalTextureOverride(), pDefaultNormalSRV, hlsl::MAT_HAS_NORMAL, Flags);

		ITextureView* pMRSRV =
			ResolveSRV(MatInst.GetMetallicRoughnessTextureOverride(), pDefaultMRSRV, hlsl::MAT_HAS_MR, Flags);

		ITextureView* pAOSRV =
			ResolveSRV(MatInst.GetAmbientOcclusionTextureOverride(), pDefaultAOSRV, hlsl::MAT_HAS_AO, Flags);

		ITextureView* pEmissiveSRV =
			ResolveSRV(MatInst.GetEmissiveTextureOverride(), pDefaultEmissiveSRV, hlsl::MAT_HAS_EMISSIVE, Flags);

		RD.MaterialFlags = Flags;

		// ------------------------------------------------------------
		// Bind PS textures
		// ------------------------------------------------------------
		auto BindPS_Tex = [&](const char* Name, ITextureView* pSRV) -> bool
		{
			if (auto* Var = RD.pSRB->GetVariableByName(SHADER_TYPE_PIXEL, Name))
			{
				Var->Set(pSRV);
				return true;
			}
			std::cerr << "PS SRB texture variable\"" << Name << "\"not found." << std::endl;
			return false;
		};

		BindPS_Tex("g_BaseColorTex", pBaseColorSRV);
		BindPS_Tex("g_NormalTex", pNormalSRV);
		BindPS_Tex("g_MetallicRoughnessTex", pMRSRV);
		BindPS_Tex("g_AOTex", pAOSRV);
		BindPS_Tex("g_EmissiveTex", pEmissiveSRV);

		// ------------------------------------------------------------
		// Create + bind material constant buffer
		// ------------------------------------------------------------
		CreateUniformBuffer(m_CreateInfo.pDevice, sizeof(hlsl::MaterialConstants), "Material constants CB", &RD.pMaterialCB);
		if (!RD.pMaterialCB)
			return nullptr;

		if (auto* MatCBVar = RD.pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "MATERIAL_CONSTANTS"))
		{
			MatCBVar->Set(RD.pMaterialCB);
		}
		else
		{
			ASSERT(false, "PS SRB variable 'MATERIAL_CONSTANTS' not found.");
			return nullptr;
		}

		auto [InsIt, Ok] = m_MatRenderDataTable.emplace(h, std::move(RD));
		(void)Ok;
		return &InsIt->second;
	}

} // namespace shz
