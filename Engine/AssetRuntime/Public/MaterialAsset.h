#pragma once
#include <string>
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

#include "Engine/AssetRuntime/Public/TextureAsset.h"

namespace shz
{
	// ------------------------------------------------------------
	// MaterialAsset
	// - CPU-side material asset (no GPU/RHI dependency).
	// - Holds texture assets + scalar/vector parameters + render options.
	// - Consumed by Renderer to create MaterialRenderData.
	// ------------------------------------------------------------
	class MaterialAsset final // TODO: Inherit IAssetObject?
	{
	public:
		enum MATERIAL_BLEND_MODE : uint8
		{
			BLEND_OPAQUE = 0,
			BLEND_MASKED,
			BLEND_TRANSLUCENT,
		};

		// glTF alphaMode-equivalent.
		// - OPAQUE: regular opaque rendering
		// - MASK  : alpha test (cutout)
		// - BLEND : alpha blending (translucent)
		enum MATERIAL_ALPHA_MODE : uint8
		{
			ALPHA_OPAQUE = 0,
			ALPHA_MASK,
			ALPHA_BLEND,
		};

		enum MATERIAL_SHADING_MODEL : uint8
		{
			SHADING_DEFAULT_LIT = 0,
			SHADING_UNLIT,
		};

		enum MATERIAL_TEXTURE_SLOT : uint8
		{
			TEX_ALBEDO = 0,
			TEX_NORMAL,
			TEX_ORM,       // Occlusion(R), Roughness(G), Metallic(B)
			TEX_EMISSIVE,

			TEX_COUNT
		};

		struct Parameters final
		{
			// Base Color (Albedo)
			float4 BaseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

			// PBR
			float Roughness = 0.5f; // [0..1]
			float Metallic = 0.0f; // [0..1]

			// Ambient Occlusion multiplier (when no AO texture).
			float Occlusion = 1.0f; // [0..1]

			// Emissive
			float3 EmissiveColor = float3(0.0f, 0.0f, 0.0f);
			float  EmissiveIntensity = 1.0f;

			// Masked
			float AlphaCutoff = 0.5f; // Used when ALPHA_MASK / BLEND_MASKED

			// Normal strength (optional)
			float NormalScale = 1.0f;
		};

		struct Options final
		{
			// NOTE:
			// BlendMode is "render pipeline policy" (what the renderer will actually do).
			// AlphaMode is "authoring/source intent" (glTF alphaMode etc.).
			MATERIAL_BLEND_MODE   BlendMode = BLEND_OPAQUE;
			MATERIAL_ALPHA_MODE   AlphaMode = ALPHA_OPAQUE;

			MATERIAL_SHADING_MODEL ShadingModel = SHADING_DEFAULT_LIT;

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

		// Optional: shader/material template key (ex: "GBufferPBR", "Unlit")
		void SetShaderKey(const std::string& key) { m_ShaderKey = key; }
		const std::string& GetShaderKey() const noexcept { return m_ShaderKey; }

		// ------------------------------------------------------------
		// Textures
		// ------------------------------------------------------------
		// Sets texture source path and common color-space hint.
		// Typical: Albedo/Emissive are sRGB, Normal/ORM are linear.
		void SetTexture(MATERIAL_TEXTURE_SLOT slot, const std::string& path, bool isSRGB);
		void ClearTexture(MATERIAL_TEXTURE_SLOT slot);

		TextureAsset& GetTexture(MATERIAL_TEXTURE_SLOT slot) noexcept;
		const TextureAsset& GetTexture(MATERIAL_TEXTURE_SLOT slot) const noexcept;

		bool HasTexture(MATERIAL_TEXTURE_SLOT slot) const noexcept { return GetTexture(slot).IsValid(); }

		// Convenience
		bool HasAlbedoTexture() const noexcept { return HasTexture(TEX_ALBEDO); }
		bool HasNormalTexture() const noexcept { return HasTexture(TEX_NORMAL); }
		bool HasORMTexture() const noexcept { return HasTexture(TEX_ORM); }
		bool HasEmissiveTexture() const noexcept { return HasTexture(TEX_EMISSIVE); }

		// ------------------------------------------------------------
		// Parameters / Options
		// ------------------------------------------------------------
		Parameters& GetParams() noexcept { return m_Params; }
		const Parameters& GetParams() const noexcept { return m_Params; }

		Options& GetOptions() noexcept { return m_Options; }
		const Options& GetOptions() const noexcept { return m_Options; }

		// Optional helper:
		// Maps AlphaMode -> BlendMode, so importer can call this after parsing alpha mode.
		void ApplyAlphaModeToBlendMode() noexcept;

		// ------------------------------------------------------------
		// Validation / Reset
		// ------------------------------------------------------------
		void Clear();
		bool IsValid() const noexcept;

	private:
		static uint32 slotToIndex(MATERIAL_TEXTURE_SLOT slot) noexcept;

	private:
		std::string m_Name;
		std::string m_SourcePath;

		// "Which shader/material template to use"
		std::string m_ShaderKey;

		// NOTE:
		// Currently stored by value (path + load options).
		// Later you can replace this with TextureAsset* and manage ownership via AssetManager.
		TextureAsset m_Textures[TEX_COUNT];

		Parameters  m_Params = {};
		Options     m_Options = {};
	};
} // namespace shz
