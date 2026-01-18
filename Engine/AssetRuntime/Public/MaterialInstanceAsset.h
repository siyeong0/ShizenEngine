#pragma once
#include <string>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

#include "Engine/AssetRuntime/Public/TextureAsset.h"

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
		MATERIAL_SHADING_MODE_LIT = 0,
		MATERIAL_SHADING_MODE_UNLIT,
	};

	enum MATERIAL_TEXTURE_SLOT : uint8
	{
		MATERIAL_TEX_ALBEDO = 0,
		MATERIAL_TEX_NORMAL,
		MATERIAL_TEX_ORM,      // Occlusion(R), Roughness(G), Metallic(B)
		MATERIAL_TEX_EMISSIVE,
		MATERIAL_TEX_AO,
		MATERIAL_TEX_HEIGHT,

		MATERIAL_TEX_COUNT
	};

	// ------------------------------------------------------------
	// MaterialInstanceAsset
	// - CPU-side "instance" data only.
	// - MaterialTemplate is referenced by TemplateKey (string).
	// - Keeps your current PBR fixed layout for now (simple).
	// ------------------------------------------------------------
	class MaterialInstanceAsset final
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

			float OcclusionStrength = 1.0f; // For ORM texture
		};

		struct Options final
		{
			// BlendMode is what renderer actually does.
			// AlphaMode is authoring/source intent (glTF alphaMode etc.).
			MATERIAL_BLEND_MODE   BlendMode = MATERIAL_BLEND_OPAQUE;
			MATERIAL_ALPHA_MODE   AlphaMode = MATERIAL_ALPHA_OPAQUE;
			MATERIAL_SHADING_MODEL ShadingModel = MATERIAL_SHADING_MODE_LIT;

			bool TwoSided = false;
			bool CastShadow = true;
		};

	public:
		MaterialInstanceAsset() = default;
		MaterialInstanceAsset(const MaterialInstanceAsset&) = default;
		MaterialInstanceAsset(MaterialInstanceAsset&&) noexcept = default;
		MaterialInstanceAsset& operator=(const MaterialInstanceAsset&) = default;
		MaterialInstanceAsset& operator=(MaterialInstanceAsset&&) noexcept = default;
		~MaterialInstanceAsset() = default;

		// ------------------------------------------------------------
		// Metadata
		// ------------------------------------------------------------
		void SetName(const std::string& name) { m_Name = name; }
		const std::string& GetName() const noexcept { return m_Name; }

		void SetSourcePath(const std::string& path) { m_SourcePath = path; }
		const std::string& GetSourcePath() const noexcept { return m_SourcePath; }

		// MaterialTemplate key (string only, no handle).
		void SetTemplateKey(const std::string& key) { m_TemplateKey = key; }
		const std::string& GetTemplateKey() const noexcept { return m_TemplateKey; }

		// ------------------------------------------------------------
		// Textures
		// ------------------------------------------------------------
		void SetTexture(MATERIAL_TEXTURE_SLOT slot, const std::string& path, bool isSRGB)
		{
			TextureAsset& t = m_Textures[SlotToIndex(slot)];
			t.Clear();
			t.SetSourcePath(path);
			t.SetIsSRGB(isSRGB);

			// sensible defaults
			t.SetGenerateMips(true);
			t.SetFlipVertically(false);
			t.SetPremultiplyAlpha(false);
		}

		void ClearTexture(MATERIAL_TEXTURE_SLOT slot)
		{
			m_Textures[SlotToIndex(slot)].Clear();
		}

		TextureAsset& GetTexture(MATERIAL_TEXTURE_SLOT slot) noexcept
		{
			return m_Textures[SlotToIndex(slot)];
		}

		const TextureAsset& GetTexture(MATERIAL_TEXTURE_SLOT slot) const noexcept
		{
			return m_Textures[SlotToIndex(slot)];
		}

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

		// Maps AlphaMode -> BlendMode (simple policy).
		void ApplyAlphaModeToBlendMode() noexcept
		{
			switch (m_Options.AlphaMode)
			{
			default:
			case MATERIAL_ALPHA_OPAQUE: m_Options.BlendMode = MATERIAL_BLEND_OPAQUE; break;
			case MATERIAL_ALPHA_MASK:   m_Options.BlendMode = MATERIAL_BLEND_MASKED; break;
			case MATERIAL_ALPHA_BLEND:  m_Options.BlendMode = MATERIAL_BLEND_TRANSLUCENT; break;
			}
		}

		// ------------------------------------------------------------
		// Reset / Validation
		// ------------------------------------------------------------
		void Clear()
		{
			m_Name.clear();
			m_SourcePath.clear();
			m_TemplateKey.clear();

			for (uint32 i = 0; i < MATERIAL_TEX_COUNT; ++i)
			{
				m_Textures[i].Clear();
			}

			m_Params = {};
			m_Options = {};
		}

		bool IsValid() const noexcept
		{
			// Instance can be valid without textures.
			// Minimal checks: nothing fatal.
			return true;
		}

	private:
		static uint32 SlotToIndex(MATERIAL_TEXTURE_SLOT slot) noexcept
		{
			return static_cast<uint32>(slot);
		}

	private:
		std::string m_Name;
		std::string m_SourcePath;
		std::string m_TemplateKey;

		TextureAsset m_Textures[MATERIAL_TEX_COUNT];

		Parameters m_Params = {};
		Options    m_Options = {};
	};
} // namespace shz
