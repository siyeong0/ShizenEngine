#pragma once
#include <string>

#include "Primitives/BasicTypes.h"

// NOTE:
// Using current Tools/Image path. You may move it under Engine later.
#include "Tools/Image/Public/TextureLoader.h"

namespace shz
{
	// ------------------------------------------------------------
	// TextureAsset
	// - CPU-side texture asset (no GPU resource ownership).
	// - Holds source path + loading options (sRGB, mips, compression, etc.).
	// - Renderer uses these options to create GPU texture (TextureRenderData).
	// ------------------------------------------------------------
	class TextureAsset final
	{
	public:
		// Minimal identity / metadata
		TextureAsset() = default;
		TextureAsset(const TextureAsset&) = default;
		TextureAsset(TextureAsset&&) noexcept = default;
		TextureAsset& operator=(const TextureAsset&) = default;
		TextureAsset& operator=(TextureAsset&&) noexcept = default;
		~TextureAsset() = default;

		// ------------------------------------------------------------
		// Identity
		// ------------------------------------------------------------
		void SetName(const std::string& name) { m_Name = name; }
		const std::string& GetName() const noexcept { return m_Name; }

		void SetSourcePath(const std::string& path) { m_SourcePath = path; }
		const std::string& GetSourcePath() const noexcept { return m_SourcePath; }

		// ------------------------------------------------------------
		// Load options (mirrors TextureLoadInfo conceptually)
		// ------------------------------------------------------------
		void SetIsSRGB(bool value) noexcept { m_IsSRGB = value; }
		bool GetIsSRGB() const noexcept { return m_IsSRGB; }

		void SetGenerateMips(bool value) noexcept { m_GenerateMips = value; }
		bool GetGenerateMips() const noexcept { return m_GenerateMips; }

		void SetFlipVertically(bool value) noexcept { m_FlipVertically = value; }
		bool GetFlipVertically() const noexcept { return m_FlipVertically; }

		void SetPremultiplyAlpha(bool value) noexcept { m_PremultiplyAlpha = value; }
		bool GetPremultiplyAlpha() const noexcept { return m_PremultiplyAlpha; }

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

		// Usage/bind policy (optional; default fits typical material textures)
		void SetUsage(USAGE usage) noexcept { m_Usage = usage; }
		USAGE GetUsage() const noexcept { return m_Usage; }

		void SetBindFlags(BIND_FLAGS flags) noexcept { m_BindFlags = flags; }
		BIND_FLAGS GetBindFlags() const noexcept { return m_BindFlags; }

		void SetMipLevels(uint32 mips) noexcept { m_MipLevels = mips; }
		uint32 GetMipLevels() const noexcept { return m_MipLevels; }

		// ------------------------------------------------------------
		// Derived
		// ------------------------------------------------------------
		bool IsValid() const noexcept;

		// Builds TextureLoadInfo used by Tools/Image loader.
		// If Name is empty, loader can still work, but naming helps debugging.
		TextureLoadInfo BuildTextureLoadInfo() const noexcept;

		// Clears all metadata and resets options to defaults.
		void Clear();

	private:
		std::string m_Name;
		std::string m_SourcePath;

		// Options
		USAGE      m_Usage = USAGE_IMMUTABLE;
		BIND_FLAGS m_BindFlags = BIND_SHADER_RESOURCE;
		uint32     m_MipLevels = 0;

		bool m_IsSRGB = false;
		bool m_GenerateMips = true;
		bool m_FlipVertically = false;
		bool m_PremultiplyAlpha = false;

		TEXTURE_FORMAT m_Format = TEX_FORMAT_UNKNOWN;

		float m_AlphaCutoff = 0.0f;
		TEXTURE_LOAD_MIP_FILTER     m_MipFilter = TEXTURE_LOAD_MIP_FILTER_DEFAULT;
		TEXTURE_LOAD_COMPRESS_MODE  m_CompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;

		TextureComponentMapping m_Swizzle = TextureComponentMapping::Identity();
		uint32 m_UniformImageClipDim = 0;
	};
} // namespace shz
