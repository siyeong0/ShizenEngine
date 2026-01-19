#include "pch.h"
#include "TextureAsset.h"

namespace shz
{
	bool TextureAsset::IsValid() const noexcept
	{
		// Source path is required to load from disk.
		if (m_SourcePath.empty())
		{
			return false;
		}

		return true;
	}

	bool TextureAsset::ValidateOptions() const noexcept
	{
		// Basic sanity checks for authoring-time validation.

		// Bind flags are expected to include SRV for typical material textures.
		// We allow 0 for special cases, but it is often a mistake.
		// If you want strict behavior, change this to "return false".
		if (m_BindFlags == 0)
		{
			return true;
		}

		// Alpha cutoff should not be negative.
		if (m_AlphaCutoff < 0.0f)
		{
			return false;
		}

		// If explicit mip levels are requested, generating mips should usually be enabled.
		// This is policy-dependent, so keep it permissive.
		if (m_MipLevels > 1 && !m_GenerateMips)
		{
			return true;
		}

		return true;
	}

	TextureLoadInfo TextureAsset::BuildTextureLoadInfo() const noexcept
	{
		TextureLoadInfo info{};

		// WARNING:
		// This pointer is only valid while TextureAsset is alive and m_Name is unchanged.
		info.Name = m_Name.empty() ? nullptr : m_Name.c_str();

		info.Usage = m_Usage;
		info.BindFlags = m_BindFlags;
		info.MipLevels = m_MipLevels;

		info.IsSRGB = m_IsSRGB;
		info.GenerateMips = m_GenerateMips;
		info.FlipVertically = m_FlipVertically;

		info.PremultiplyAlpha = m_PremultiplyAlpha;

		info.Format = m_Format;

		info.AlphaCutoff = m_AlphaCutoff;
		info.MipFilter = m_MipFilter;
		info.CompressMode = m_CompressMode;

		info.Swizzle = m_Swizzle;
		info.UniformImageClipDim = m_UniformImageClipDim;

		// info.CPUAccessFlags, info.pAllocator etc. are left as defaults by the asset.
		return info;
	}

	void TextureAsset::Clear()
	{
		m_Name.clear();
		m_SourcePath.clear();

		m_Usage = USAGE_IMMUTABLE;
		m_BindFlags = BIND_SHADER_RESOURCE;
		m_MipLevels = 0;

		m_IsSRGB = false;
		m_GenerateMips = true;
		m_FlipVertically = false;
		m_PremultiplyAlpha = false;

		m_Format = TEX_FORMAT_UNKNOWN;

		m_AlphaCutoff = 0.0f;
		m_MipFilter = TEXTURE_LOAD_MIP_FILTER_DEFAULT;
		m_CompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;

		m_Swizzle = TextureComponentMapping::Identity();
		m_UniformImageClipDim = 0;
	}
} // namespace shz
