#pragma once
#include <string>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

#include "Engine/AssetRuntime/Public/TextureAsset.h"
#include "Engine/AssetRuntime/Public/AssetObject.h"

namespace shz
{
	enum MATERIAL_ALPHA_MODE : uint8
	{
		MATERIAL_ALPHA_OPAQUE = 0,
		MATERIAL_ALPHA_MASK,
		MATERIAL_ALPHA_BLEND,
	};

	enum MATERIAL_BLEND_MODE : uint8
	{
		MATERIAL_BLEND_OPAQUE = 0,
		MATERIAL_BLEND_MASKED,
		MATERIAL_BLEND_TRANSLUCENT,
	};

	enum MATERIAL_SHADING_MODEL : uint8
	{
		MATERIAL_SHADING_DEFAULT_LIT = 0,
		MATERIAL_SHADING_UNLIT,
	};

	enum MATERIAL_TEXTURE_SLOT : uint8
	{
		MATERIAL_TEX_ALBEDO = 0,
		MATERIAL_TEX_NORMAL,
		MATERIAL_TEX_ORM,      // Occlusion(R), Roughness(G), Metallic(B)
		MATERIAL_TEX_EMISSIVE,

		MATERIAL_TEX_COUNT
	};

	// ------------------------------------------------------------
	// MaterialAsset
	// - CPU-side material asset (no GPU/RHI dependency).
	// - Holds texture assets + scalar/vector parameters + render options.
	// - Consumed by Renderer to create MaterialRenderData / MaterialInstance.
	// ------------------------------------------------------------
	class MaterialAsset final : public AssetObject
	{
	public:

		struct Parameters final
		{
			// Base Color (Albedo)
			float4 BaseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

			// PBR
			float Roughness = 0.5f; // [0..1]
			float Metallic = 0.0f; // [0..1]

			// Ambient occlusion multiplier (when no AO texture).
			float Occlusion = 1.0f; // [0..1]

			// Emissive
			float3 EmissiveColor = float3(0.0f, 0.0f, 0.0f);
			float  EmissiveIntensity = 1.0f;

			// Masked
			float AlphaCutoff = 0.5f; // Used when ALPHA_MASK / BLEND_MASKED

			// Normal strength
			float NormalScale = 1.0f;
		};

		struct Options final
		{
			// NOTE:
			// BlendMode is a render pipeline policy (what the renderer actually does).
			// AlphaMode is authoring/source intent (glTF alphaMode etc.).
			MATERIAL_BLEND_MODE BlendMode = MATERIAL_BLEND_OPAQUE;
			MATERIAL_ALPHA_MODE AlphaMode = MATERIAL_ALPHA_OPAQUE;
			MATERIAL_SHADING_MODEL ShadingModel = MATERIAL_SHADING_DEFAULT_LIT;

			bool TwoSided = false;
			bool CastShadow = true;
		};

	public:
		MaterialAsset() = default;
		MaterialAsset(const MaterialAsset&) = default;
		MaterialAsset(MaterialAsset&&) noexcept = default;
		MaterialAsset& operator=(const MaterialAsset&) = default;
		MaterialAsset& operator=(MaterialAsset&&) noexcept = default;
		~MaterialAsset() = default;

		// ------------------------------------------------------------
		// Metadata
		// ------------------------------------------------------------
		void SetName(const std::string& name) { m_Name = name; }
		const std::string& GetName() const noexcept { return m_Name; }

		void SetSourcePath(const std::string& path) { m_SourcePath = path; }
		const std::string& GetSourcePath() const noexcept { return m_SourcePath; }

		// Optional: shader/material template key (e.g. "GBufferPBR", "Unlit")
		void SetShaderKey(const std::string& key) { m_ShaderKey = key; }
		const std::string& GetShaderKey() const noexcept { return m_ShaderKey; }

		// ------------------------------------------------------------
		// Textures
		// ------------------------------------------------------------
		// Sets texture source path and color-space hint.
		// Typical: Albedo/Emissive are sRGB, Normal/ORM are linear.
		void SetTexture(MATERIAL_TEXTURE_SLOT slot, const std::string& path, bool isSRGB);
		void ClearTexture(MATERIAL_TEXTURE_SLOT slot);

		TextureAsset& GetTexture(MATERIAL_TEXTURE_SLOT slot) noexcept;
		const TextureAsset& GetTexture(MATERIAL_TEXTURE_SLOT slot) const noexcept;

		bool HasTexture(MATERIAL_TEXTURE_SLOT slot) const noexcept
		{
			return GetTexture(slot).IsValid();
		}

		bool HasAlbedoTexture() const noexcept { return HasTexture(MATERIAL_TEX_ALBEDO); }
		bool HasNormalTexture() const noexcept { return HasTexture(MATERIAL_TEX_NORMAL); }
		bool HasORMTexture() const noexcept { return HasTexture(MATERIAL_TEX_ORM); }
		bool HasEmissiveTexture() const noexcept { return HasTexture(MATERIAL_TEX_EMISSIVE); }

		// ------------------------------------------------------------
		// Parameters / Options
		// ------------------------------------------------------------
		Parameters& GetParams() noexcept { return m_Params; }
		const Parameters& GetParams() const noexcept { return m_Params; }

		Options& GetOptions() noexcept { return m_Options; }
		const Options& GetOptions() const noexcept { return m_Options; }

		// Maps AlphaMode -> BlendMode.
		// Importers can call this after parsing alpha mode.
		void ApplyAlphaModeToBlendMode() noexcept;

		// ------------------------------------------------------------
		// Reset / Validation
		// ------------------------------------------------------------
		// Clears authoring data and resets parameters/options.
		// Note: AssetObject identity (AssetId) is not changed.
		void Clear();

		// Minimal validation for authoring parameters.
		// Materials can be valid even without any textures.
		bool IsValid() const noexcept;

	private:
		static uint32 SlotToIndex(MATERIAL_TEXTURE_SLOT slot) noexcept;

	private:
		std::string m_Name;
		std::string m_SourcePath;
		std::string m_ShaderKey;

		TextureAsset m_Textures[MATERIAL_TEX_COUNT];

		Parameters m_Params = {};
		Options    m_Options = {};
	};
} // namespace shz
