#include "pch.h"
#include "Engine/Renderer/Public/RenderResourceCache.h"

#include <cstring>
#include <algorithm>

#include "Engine/RHI/Interface/IBuffer.h"
#include "Tools/Image/Public/TextureUtilities.h"

namespace shz
{
	// ------------------------------------------------------------
	// Local helper: SoA -> interleaved vertex
	// ------------------------------------------------------------
	struct PackedStaticVertex final
	{
		float3 Pos;
		float2 UV;
		float3 Normal;
		float3 Tangent;
	};

	static void buildPackedVertices(const StaticMeshAsset& mesh, std::vector<PackedStaticVertex>& outVerts)
	{
		const uint32 vtxCount = mesh.GetVertexCount();
		outVerts.resize(vtxCount);

		const auto& P = mesh.GetPositions();
		const auto& N = mesh.GetNormals();
		const auto& T = mesh.GetTangents();
		const auto& UV = mesh.GetTexCoords();

		const bool hasN = (!N.empty() && N.size() == P.size());
		const bool hasT = (!T.empty() && T.size() == P.size());
		const bool hasUV = (!UV.empty() && UV.size() == P.size());

		for (uint32 i = 0; i < vtxCount; ++i)
		{
			PackedStaticVertex v{};
			v.Pos = P[i];
			v.Normal = hasN ? N[i] : float3(0.0f, 1.0f, 0.0f);
			v.Tangent = hasT ? T[i] : float3(1.0f, 0.0f, 0.0f);
			v.UV = hasUV ? UV[i] : float2(0.0f, 0.0f);
			outVerts[i] = v;
		}
	}

	static RefCntAutoPtr<IBuffer> createImmutableBuffer(
		IRenderDevice* pDevice,
		const char* name,
		BIND_FLAGS bindFlags,
		const void* pData,
		uint32 dataSizeBytes)
	{
		if (!pDevice || !pData || dataSizeBytes == 0)
			return {};

		BufferDesc desc = {};
		desc.Name = name;
		desc.Usage = USAGE_IMMUTABLE;
		desc.BindFlags = bindFlags;
		desc.Size = dataSizeBytes;

		BufferData init = {};
		init.pData = pData;
		init.DataSize = dataSizeBytes;

		RefCntAutoPtr<IBuffer> pBuf;
		pDevice->CreateBuffer(desc, &init, &pBuf);
		return pBuf;
	}

	// ------------------------------------------------------------
	// RenderResourceCache
	// ------------------------------------------------------------
	bool RenderResourceCache::Initialize(IRenderDevice* pDevice)
	{
		m_pDevice = pDevice;
		Clear();
		return (m_pDevice != nullptr);
	}

	void RenderResourceCache::Shutdown()
	{
		Clear();
		m_pDevice = nullptr;
		m_pAssetManager = nullptr;
	}

	void RenderResourceCache::Clear()
	{
		m_TexAssetToRD.clear();
		m_TexIDToRD.clear();
		m_MeshAssetToRD.clear();
		m_MaterialInstToRD.clear();

		for (auto& s : m_TexRDSlots) { s.Value.reset(); s.Owner.Reset(); }
		for (auto& s : m_MeshRDSlots) { s.Value.reset(); s.Owner.Reset(); }
		for (auto& s : m_MaterialRDSlots) { s.Value.reset(); s.Owner.Reset(); }

		m_TexRDSlots.clear();
		m_MeshRDSlots.clear();
		m_MaterialRDSlots.clear();
	}

	// ------------------------------------------------------------
	// Texture
	// ------------------------------------------------------------
	bool RenderResourceCache::createTextureFromAsset(const TextureAsset& asset, TextureRenderData* outRD)
	{
		if (!outRD || !m_pDevice)
			return false;

		// Prefer using the asset helper (less bug-prone).
		TextureLoadInfo loadInfo = asset.BuildTextureLoadInfo();

		ITexture* outTex = nullptr;
		CreateTextureFromFile(asset.GetSourcePath().c_str(), loadInfo, m_pDevice, &outTex);

		if (!outTex)
		{
			ASSERT(false, "Failed to create texture from asset.");
			return false;
		}

		outRD->SetTexture(outTex);
		return true;
	}

	Handle<TextureRenderData> RenderResourceCache::GetOrCreateTextureRenderData(const TextureAsset& asset)
	{
		if (!m_pDevice || !asset.IsValid())
			return {};

		const uint64 key = ptrKey(&asset);

		if (auto it = m_TexAssetToRD.find(key); it != m_TexAssetToRD.end())
		{
			const Handle<TextureRenderData> cached = it->second;
			if (FindSlot<TextureRenderData>(cached, m_TexRDSlots) != nullptr)
				return cached;

			m_TexAssetToRD.erase(it);
		}

		TextureRenderData rd = {};
		if (!createTextureFromAsset(asset, &rd))
		{
			ASSERT(false, "Failed to create TextureRenderData.");
			return {};
		}

		UniqueHandle<TextureRenderData> owner = UniqueHandle<TextureRenderData>::Make();
		const Handle<TextureRenderData> hRD = owner.Get();

		EnsureSlotCapacity<TextureRenderData>(hRD.GetIndex(), m_TexRDSlots);

		auto& slot = m_TexRDSlots[hRD.GetIndex()];
		ASSERT(!slot.Value.has_value() && !slot.Owner.Get().IsValid(), "TextureRenderData slot already occupied.");

		slot.Owner = std::move(owner);
		slot.Value.emplace(std::move(rd));

		m_TexAssetToRD.emplace(key, hRD);
		return hRD;
	}

	Handle<TextureRenderData> RenderResourceCache::GetOrCreateTextureRenderData(const AssetRef<TextureAsset>& texRef, EAssetLoadFlags flags)
	{
		if (!m_pDevice || !texRef)
			return {};

		// AssetID-based cache first (stable across loads)
		const uint64 idKey = assetIDKey(texRef.GetID());

		if (auto it = m_TexIDToRD.find(idKey); it != m_TexIDToRD.end())
		{
			const Handle<TextureRenderData> cached = it->second;
			if (FindSlot<TextureRenderData>(cached, m_TexRDSlots) != nullptr)
				return cached;

			m_TexIDToRD.erase(it);
		}

		// Resolve CPU asset through new asset manager
		if (!m_pAssetManager)
		{
			ASSERT(false, "AssetManagerImpl is null (required for AssetRef-based textures).");
			return {};
		}

		AssetPtr<TextureAsset> texPtr = m_pAssetManager->LoadBlocking(texRef, flags);
		const TextureAsset* pTex = texPtr.Get();
		if (!pTex)
		{
			ASSERT(false, "Failed to load TextureAsset from AssetRef.");
			return {};
		}

		// Route to pointer-key path (also fills pointer cache)
		const Handle<TextureRenderData> hRD = GetOrCreateTextureRenderData(*pTex);
		if (hRD.IsValid())
		{
			m_TexIDToRD.emplace(idKey, hRD);
		}
		return hRD;
	}

	const TextureRenderData* RenderResourceCache::TryGetTextureRenderData(Handle<TextureRenderData> h) const noexcept
	{
		const Slot<TextureRenderData>* slot = FindSlot<TextureRenderData>(h, m_TexRDSlots);
		return slot ? &slot->Value.value() : nullptr;
	}

	TextureRenderData* RenderResourceCache::TryGetTextureRenderData(Handle<TextureRenderData> h) noexcept
	{
		Slot<TextureRenderData>* slot = FindSlot<TextureRenderData>(h, m_TexRDSlots);
		return slot ? &slot->Value.value() : nullptr;
	}

	bool RenderResourceCache::DestroyTextureRenderData(Handle<TextureRenderData> h)
	{
		Slot<TextureRenderData>* slot = FindSlot<TextureRenderData>(h, m_TexRDSlots);
		if (!slot)
			return false;

		for (auto it = m_TexAssetToRD.begin(); it != m_TexAssetToRD.end(); )
		{
			if (it->second == h) { it = m_TexAssetToRD.erase(it); break; }
			else { ++it; }
		}

		for (auto it = m_TexIDToRD.begin(); it != m_TexIDToRD.end(); )
		{
			if (it->second == h) { it = m_TexIDToRD.erase(it); break; }
			else { ++it; }
		}

		slot->Value.reset();
		slot->Owner.Reset();
		return true;
	}

	void RenderResourceCache::InvalidateTextureByAsset(const TextureAsset& asset)
	{
		const uint64 key = ptrKey(&asset);

		auto it = m_TexAssetToRD.find(key);
		if (it == m_TexAssetToRD.end())
			return;

		const Handle<TextureRenderData> hRD = it->second;
		m_TexAssetToRD.erase(it);
		DestroyTextureRenderData(hRD);
	}

	void RenderResourceCache::InvalidateTextureByRef(const AssetRef<TextureAsset>& texRef)
	{
		if (!texRef)
			return;

		const uint64 idKey = assetIDKey(texRef.GetID());

		auto it = m_TexIDToRD.find(idKey);
		if (it == m_TexIDToRD.end())
			return;

		const Handle<TextureRenderData> hRD = it->second;
		m_TexIDToRD.erase(it);
		DestroyTextureRenderData(hRD);
	}

	// ------------------------------------------------------------
	// StaticMesh
	// ------------------------------------------------------------
	bool RenderResourceCache::createStaticMeshFromAsset(const StaticMeshAsset& asset, StaticMeshRenderData& outRD, IDeviceContext* /*pCtx*/)
	{
		if (!m_pDevice)
			return false;

		if (!asset.IsValid() || !asset.HasCPUData())
			return false;

		std::vector<PackedStaticVertex> packed;
		buildPackedVertices(asset, packed);

		const uint32 vbBytes = static_cast<uint32>(packed.size() * sizeof(PackedStaticVertex));
		RefCntAutoPtr<IBuffer> pVB = createImmutableBuffer(m_pDevice, "StaticMesh_VB", BIND_VERTEX_BUFFER, packed.data(), vbBytes);
		if (!pVB)
			return false;

		const void* pIndexData = asset.GetIndexData();
		const uint32 ibBytes = asset.GetIndexDataSizeBytes();
		if (!pIndexData || ibBytes == 0)
			return false;

		RefCntAutoPtr<IBuffer> pIB = createImmutableBuffer(m_pDevice, "StaticMesh_IB", BIND_INDEX_BUFFER, pIndexData, ibBytes);
		if (!pIB)
			return false;

		outRD = {};
		outRD.SetVertexBuffer(pVB);
		outRD.SetIndexBuffer(pIB);
		outRD.SetVertexStride(static_cast<uint32>(sizeof(PackedStaticVertex)));
		outRD.SetVertexCount(asset.GetVertexCount());
		outRD.SetIndexCount(asset.GetIndexCount());
		outRD.SetIndexType(asset.GetIndexType());

		std::vector<StaticMeshRenderData::Section> secs;
		const auto& srcSecs = asset.GetSections();
		secs.reserve(srcSecs.size());

		for (const auto& s : srcSecs)
		{
			StaticMeshRenderData::Section d{};
			d.FirstIndex = s.FirstIndex;
			d.IndexCount = s.IndexCount;
			d.BaseVertex = s.BaseVertex;
			d.MaterialSlot = s.MaterialSlot;
			secs.push_back(d);
		}

		outRD.SetSections(std::move(secs));
		return outRD.IsValid();
	}

	Handle<StaticMeshRenderData> RenderResourceCache::GetOrCreateStaticMeshRenderData(const StaticMeshAsset& asset, IDeviceContext* pCtx)
	{
		if (!m_pDevice || !asset.IsValid())
			return {};

		const uint64 key = ptrKey(&asset);

		if (auto it = m_MeshAssetToRD.find(key); it != m_MeshAssetToRD.end())
		{
			const Handle<StaticMeshRenderData> cached = it->second;
			if (FindSlot<StaticMeshRenderData>(cached, m_MeshRDSlots) != nullptr)
				return cached;

			m_MeshAssetToRD.erase(it);
		}

		StaticMeshRenderData rd = {};
		if (!createStaticMeshFromAsset(asset, rd, pCtx))
		{
			ASSERT(false, "Failed to create StaticMeshRenderData.");
			return {};
		}

		UniqueHandle<StaticMeshRenderData> owner = UniqueHandle<StaticMeshRenderData>::Make();
		const Handle<StaticMeshRenderData> hRD = owner.Get();

		EnsureSlotCapacity<StaticMeshRenderData>(hRD.GetIndex(), m_MeshRDSlots);

		auto& slot = m_MeshRDSlots[hRD.GetIndex()];
		ASSERT(!slot.Value.has_value() && !slot.Owner.Get().IsValid(), "Slot collision detected for StaticMeshRenderData.");

		slot.Owner = std::move(owner);
		slot.Value.emplace(std::move(rd));

		m_MeshAssetToRD.emplace(key, hRD);
		return hRD;
	}

	const StaticMeshRenderData* RenderResourceCache::TryGetStaticMeshRenderData(Handle<StaticMeshRenderData> h) const noexcept
	{
		const Slot<StaticMeshRenderData>* slot = FindSlot<StaticMeshRenderData>(h, m_MeshRDSlots);
		return slot ? &slot->Value.value() : nullptr;
	}

	StaticMeshRenderData* RenderResourceCache::TryGetStaticMeshRenderData(Handle<StaticMeshRenderData> h) noexcept
	{
		Slot<StaticMeshRenderData>* slot = FindSlot<StaticMeshRenderData>(h, m_MeshRDSlots);
		return slot ? &slot->Value.value() : nullptr;
	}

	bool RenderResourceCache::DestroyStaticMeshRenderData(Handle<StaticMeshRenderData> h)
	{
		Slot<StaticMeshRenderData>* slot = FindSlot<StaticMeshRenderData>(h, m_MeshRDSlots);
		if (!slot)
			return false;

		for (auto it = m_MeshAssetToRD.begin(); it != m_MeshAssetToRD.end(); )
		{
			if (it->second == h) { it = m_MeshAssetToRD.erase(it); break; }
			else { ++it; }
		}

		slot->Value.reset();
		slot->Owner.Reset();
		return true;
	}

	void RenderResourceCache::InvalidateStaticMeshByAsset(const StaticMeshAsset& asset)
	{
		const uint64 key = ptrKey(&asset);

		auto it = m_MeshAssetToRD.find(key);
		if (it == m_MeshAssetToRD.end())
			return;

		const Handle<StaticMeshRenderData> hRD = it->second;
		m_MeshAssetToRD.erase(it);
		DestroyStaticMeshRenderData(hRD);
	}

	// ------------------------------------------------------------
	// MaterialRenderData (instance-based caching)
	// ------------------------------------------------------------
	Handle<MaterialRenderData> RenderResourceCache::GetOrCreateMaterialRenderData(
		const MaterialInstance* pInstance,
		IPipelineState* pPSO,
		const MaterialTemplate* pTemplate,
		IBuffer* pObjectCB,
		IBuffer* pFrameCB)
	{
		if (!pInstance || !pPSO || !pTemplate || !m_pDevice)
			return {};

		const uint64 key = ptrKey(pInstance);

		if (auto it = m_MaterialInstToRD.find(key); it != m_MaterialInstToRD.end())
		{
			const Handle<MaterialRenderData> cached = it->second;
			if (FindSlot<MaterialRenderData>(cached, m_MaterialRDSlots) != nullptr)
				return cached;

			m_MaterialInstToRD.erase(it);
		}

		MaterialRenderData rd = {};
		if (!rd.Initialize(m_pDevice, pPSO, pTemplate))
			return {};

		if (auto* var = rd.GetSRB()->GetVariableByName(SHADER_TYPE_VERTEX, "FRAME_CONSTANTS"))
			var->Set(pFrameCB);
		if (auto* var = rd.GetSRB()->GetVariableByName(SHADER_TYPE_PIXEL, "FRAME_CONSTANTS"))
			var->Set(pFrameCB);

		if (auto* var = rd.GetSRB()->GetVariableByName(SHADER_TYPE_VERTEX, "OBJECT_CONSTANTS"))
			var->Set(pObjectCB);
		if (auto* var = rd.GetSRB()->GetVariableByName(SHADER_TYPE_PIXEL, "OBJECT_CONSTANTS"))
			var->Set(pObjectCB);

		UniqueHandle<MaterialRenderData> owner = UniqueHandle<MaterialRenderData>::Make();
		const Handle<MaterialRenderData> hRD = owner.Get();

		EnsureSlotCapacity<MaterialRenderData>(hRD.GetIndex(), m_MaterialRDSlots);

		auto& slot = m_MaterialRDSlots[hRD.GetIndex()];
		ASSERT(!slot.Value.has_value() && !slot.Owner.Get().IsValid(), "Slot collision detected for MaterialRenderData.");

		slot.Owner = std::move(owner);
		slot.Value.emplace(std::move(rd));

		m_MaterialInstToRD.emplace(key, hRD);
		return hRD;
	}

	const MaterialRenderData* RenderResourceCache::TryGetMaterialRenderData(Handle<MaterialRenderData> h) const noexcept
	{
		const Slot<MaterialRenderData>* slot = FindSlot<MaterialRenderData>(h, m_MaterialRDSlots);
		return slot ? &slot->Value.value() : nullptr;
	}

	MaterialRenderData* RenderResourceCache::TryGetMaterialRenderData(Handle<MaterialRenderData> h) noexcept
	{
		Slot<MaterialRenderData>* slot = FindSlot<MaterialRenderData>(h, m_MaterialRDSlots);
		return slot ? &slot->Value.value() : nullptr;
	}

	bool RenderResourceCache::DestroyMaterialRenderData(Handle<MaterialRenderData> h)
	{
		Slot<MaterialRenderData>* slot = FindSlot<MaterialRenderData>(h, m_MaterialRDSlots);
		if (!slot)
			return false;

		for (auto it = m_MaterialInstToRD.begin(); it != m_MaterialInstToRD.end(); )
		{
			if (it->second == h) { it = m_MaterialInstToRD.erase(it); break; }
			else { ++it; }
		}

		slot->Value.reset();
		slot->Owner.Reset();
		return true;
	}

	void RenderResourceCache::InvalidateMaterialByInstance(const MaterialInstance* pInstance)
	{
		if (!pInstance)
			return;

		const uint64 key = ptrKey(pInstance);

		auto it = m_MaterialInstToRD.find(key);
		if (it == m_MaterialInstToRD.end())
			return;

		const Handle<MaterialRenderData> hRD = it->second;
		m_MaterialInstToRD.erase(it);
		DestroyMaterialRenderData(hRD);
	}

} // namespace shz
