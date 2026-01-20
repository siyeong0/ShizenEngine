#pragma once
#include <string>

#include "Primitives/BasicTypes.h"

#include "Tools/Image/Public/TextureLoader.h"

namespace shz
{
	class TextureAsset final
	{
	public:
		TextureAsset() = default;
		TextureAsset(const TextureAsset&) = default;
		TextureAsset(TextureAsset&&) noexcept = default;
		TextureAsset& operator=(const TextureAsset&) = default;
		TextureAsset& operator=(TextureAsset&&) noexcept = default;
		~TextureAsset() = default;

		void SetName(const std::string& name) { m_Name = name; }
		const std::string& GetName() const noexcept { return m_Name; }

		void SetSourcePath(const std::string& path) { m_SourcePath = path; }
		const std::string& GetSourcePath() const noexcept { return m_SourcePath; }

		// ------------------------------------------------------------
		// Load options
		// ------------------------------------------------------------
		void SetIsSRGB(bool value) noexcept { m_bSRGB = value; }
		bool GetIsSRGB() const noexcept { return m_bSRGB; }

		void SetGenerateMips(bool value) noexcept { m_bGenerateMips = value; }
		bool GetGenerateMips() const noexcept { return m_bGenerateMips; }

		void SetFlipVertically(bool value) noexcept { m_bFlipVertically = value; }
		bool GetFlipVertically() const noexcept { return m_bFlipVertically; }

		void SetPremultiplyAlpha(bool value) noexcept { m_bPremultiplyAlpha = value; }
		bool GetPremultiplyAlpha() const noexcept { return m_bPremultiplyAlpha; }

		void SetMipFilter(TEXTURE_LOAD_MIP_FILTER value) noexcept { m_MipFilter = value; }
		TEXTURE_LOAD_MIP_FILTER GetMipFilter() const noexcept { return m_MipFilter; }

		void SetCompressMode(TEXTURE_LOAD_COMPRESS_MODE value) noexcept { m_CompressMode = value; }
		TEXTURE_LOAD_COMPRESS_MODE GetCompressMode() const noexcept { return m_CompressMode; }

		void SetFormat(TEXTURE_FORMAT fmt) noexcept { m_Format = fmt; }
		TEXTURE_FORMAT GetFormat() const noexcept { return m_Format; }

		void SetAlphaCutoff(float value) noexcept { m_AlphaCutoff = value; }
		float GetAlphaCutoff() const noexcept { return m_AlphaCutoff; }

		void SetUniformImageClipDim(uint32 value) noexcept { m_UniformImageClipDim = value; }
		uint32 GetUniformImageClipDim() const noexcept { return m_UniformImageClipDim; }

		void SetSwizzle(const TextureComponentMapping& swizzle) noexcept { m_Swizzle = swizzle; }
		const TextureComponentMapping& GetSwizzle() const noexcept { return m_Swizzle; }

		void SetUsage(USAGE usage) noexcept { m_Usage = usage; }
		USAGE GetUsage() const noexcept { return m_Usage; }

		void SetBindFlags(BIND_FLAGS flags) noexcept { m_BindFlags = flags; }
		BIND_FLAGS GetBindFlags() const noexcept { return m_BindFlags; }

		void SetMipLevels(uint32 mips) noexcept { m_MipLevels = mips; }
		uint32 GetMipLevels() const noexcept { return m_MipLevels; }

		// ------------------------------------------------------------
		// Derived / utilities
		// ------------------------------------------------------------
		// Minimal "loadable" validity check.
		bool IsValid() const noexcept;

		// Optional: checks option consistency (debug/authoring validation).
		bool ValidateOptions() const noexcept;

		// Builds TextureLoadInfo used by Tools/Image loader.
		// WARNING: Name points into this object's internal string.
		// Use only when the loader consumes the struct immediately.
		TextureLoadInfo BuildTextureLoadInfo() const noexcept;

		// Clears all metadata and resets options to defaults.
		void Clear();

	private:
		std::string m_Name;
		std::string m_SourcePath;

		USAGE m_Usage = USAGE_IMMUTABLE;
		BIND_FLAGS m_BindFlags = BIND_SHADER_RESOURCE;
		uint32 m_MipLevels = 0;

		bool m_bSRGB = false;
		bool m_bGenerateMips = true;
		bool m_bFlipVertically = false;
		bool m_bPremultiplyAlpha = false;

		TEXTURE_FORMAT m_Format = TEX_FORMAT_UNKNOWN;

		float m_AlphaCutoff = 0.0f;
		TEXTURE_LOAD_MIP_FILTER    m_MipFilter = TEXTURE_LOAD_MIP_FILTER_DEFAULT;
		TEXTURE_LOAD_COMPRESS_MODE m_CompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;

		TextureComponentMapping m_Swizzle = TextureComponentMapping::Identity();
		uint32 m_UniformImageClipDim = 0;
	};
} // namespace shz
