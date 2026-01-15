#include "pch.h"
#include "MaterialAsset.h"

namespace shz
{
	// ------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------
	uint32 MaterialAsset::SlotToIndex(MATERIAL_TEXTURE_SLOT slot) noexcept
	{
		const uint32 idx = static_cast<uint32>(slot);

		// Returning 0 on invalid input can silently overwrite TEX_ALBEDO.
		// We assert and clamp to 0 to keep release builds safe.
		ASSERT(idx < static_cast<uint32>(MATERIAL_TEX_COUNT), "Invalid MATERIAL_TEXTURE_SLOT.");
		if (idx >= static_cast<uint32>(MATERIAL_TEX_COUNT))
		{
			return 0u;
		}

		return idx;
	}

	// ------------------------------------------------------------
	// Textures
	// ------------------------------------------------------------
	void MaterialAsset::SetTexture(MATERIAL_TEXTURE_SLOT slot, const std::string& path, bool isSRGB)
	{
		const uint32 idx = SlotToIndex(slot);

		// Treat empty path as "clear texture" to avoid accidentally marking a texture valid.
		if (path.empty())
		{
			m_Textures[idx].Clear();
			return;
		}

		TextureAsset& tex = m_Textures[idx];
		tex.SetSourcePath(path);
		tex.SetIsSRGB(isSRGB);
	}

	void MaterialAsset::ClearTexture(MATERIAL_TEXTURE_SLOT slot)
	{
		const uint32 idx = SlotToIndex(slot);
		m_Textures[idx].Clear();
	}

	TextureAsset& MaterialAsset::GetTexture(MATERIAL_TEXTURE_SLOT slot) noexcept
	{
		const uint32 idx = SlotToIndex(slot);
		return m_Textures[idx];
	}

	const TextureAsset& MaterialAsset::GetTexture(MATERIAL_TEXTURE_SLOT slot) const noexcept
	{
		const uint32 idx = SlotToIndex(slot);
		return m_Textures[idx];
	}

	// ------------------------------------------------------------
	// Alpha helpers
	// ------------------------------------------------------------
	void MaterialAsset::ApplyAlphaModeToBlendMode() noexcept
	{
		switch (m_Options.AlphaMode)
		{
		case MATERIAL_ALPHA_MASK:
		{
			m_Options.BlendMode = MATERIAL_BLEND_MASKED;
			break;
		}
		case MATERIAL_ALPHA_BLEND:
		{
			m_Options.BlendMode = MATERIAL_BLEND_TRANSLUCENT;
			break;
		}
		case MATERIAL_ALPHA_OPAQUE:
		default:
		{
			m_Options.BlendMode = MATERIAL_BLEND_OPAQUE;
			break;
		}
		}
	}

	// ------------------------------------------------------------
	// Reset / Validation
	// ------------------------------------------------------------
	void MaterialAsset::Clear()
	{
		m_Name.clear();
		m_SourcePath.clear();
		m_ShaderKey.clear();

		for (uint32 i = 0; i < static_cast<uint32>(MATERIAL_TEX_COUNT); ++i)
		{
			m_Textures[i].Clear();
		}

		m_Params = Parameters{};
		m_Options = Options{};
	}

	bool MaterialAsset::IsValid() const noexcept
	{
		// Materials can be valid even without textures.
		// This function checks parameter ranges and basic consistency.

		if (m_Params.Roughness < 0.0f || m_Params.Roughness > 1.0f)
		{
			return false;
		}

		if (m_Params.Metallic < 0.0f || m_Params.Metallic > 1.0f)
		{
			return false;
		}

		if (m_Params.Occlusion < 0.0f || m_Params.Occlusion > 1.0f)
		{
			return false;
		}

		if (m_Options.BlendMode == MATERIAL_BLEND_MASKED)
		{
			// AlphaCutoff is expected in [0..1] for masked materials.
			if (m_Params.AlphaCutoff < 0.0f || m_Params.AlphaCutoff > 1.0f)
			{
				return false;
			}
		}

		// NormalScale is typically >= 0.0f.
		if (m_Params.NormalScale < 0.0f)
		{
			return false;
		}

		return true;
	}
} // namespace shz
