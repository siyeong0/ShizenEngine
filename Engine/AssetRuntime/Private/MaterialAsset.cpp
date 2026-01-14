#include "pch.h"
#include "MaterialAsset.h"

namespace shz
{
	// ------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------

	uint32 MaterialAsset::slotToIndex(MATERIAL_TEXTURE_SLOT slot) noexcept
	{
		const uint32 idx = static_cast<uint32>(slot);
		return (idx < static_cast<uint32>(TEX_COUNT)) ? idx : 0u;
	}

	// ------------------------------------------------------------
	// Textures
	// ------------------------------------------------------------

	void MaterialAsset::SetTexture(MATERIAL_TEXTURE_SLOT slot, const std::string& path, bool isSRGB)
	{
		const uint32 idx = slotToIndex(slot);

		TextureAsset& tex = m_Textures[idx];
		tex.SetSourcePath(path);
		tex.SetIsSRGB(isSRGB);
	}

	void MaterialAsset::ClearTexture(MATERIAL_TEXTURE_SLOT slot)
	{
		const uint32 idx = slotToIndex(slot);
		m_Textures[idx].Clear();
	}

	TextureAsset& MaterialAsset::GetTexture(MATERIAL_TEXTURE_SLOT slot) noexcept
	{
		const uint32 idx = slotToIndex(slot);
		return m_Textures[idx];
	}

	const TextureAsset& MaterialAsset::GetTexture(MATERIAL_TEXTURE_SLOT slot) const noexcept
	{
		const uint32 idx = slotToIndex(slot);
		return m_Textures[idx];
	}

	// ------------------------------------------------------------
	// Alpha helpers
	// ------------------------------------------------------------

	void MaterialAsset::ApplyAlphaModeToBlendMode() noexcept
	{
		switch (m_Options.AlphaMode)
		{
		case ALPHA_MASK:
			m_Options.BlendMode = BLEND_MASKED;
			break;

		case ALPHA_BLEND:
			m_Options.BlendMode = BLEND_TRANSLUCENT;
			break;

		case ALPHA_OPAQUE:
		default:
			m_Options.BlendMode = BLEND_OPAQUE;
			break;
		}
	}

	// ------------------------------------------------------------
	// Validation / Reset
	// ------------------------------------------------------------

	void MaterialAsset::Clear()
	{
		m_Name.clear();
		m_SourcePath.clear();
		m_ShaderKey.clear();

		for (uint32 i = 0; i < static_cast<uint32>(TEX_COUNT); ++i)
		{
			m_Textures[i].Clear();
		}

		m_Params = Parameters{};
		m_Options = Options{};
	}

	bool MaterialAsset::IsValid() const noexcept
	{
		// Material can be valid even without textures.

		if (m_Options.BlendMode == BLEND_MASKED)
		{
			// AlphaCutoff should be in [0..1], but we keep it permissive here.
			// Renderer may clamp if needed.
		}

		return true;
	}
} // namespace shz
