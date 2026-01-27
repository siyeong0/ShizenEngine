#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"
#include "Engine/GraphicsUtils/Public/GraphicsUtils.hpp"

namespace shz
{
	struct TextureMip final
	{
		uint32 Width = 0;
		uint32 Height = 0;

		// Tightly packed for the mip, layout depends on Texture::GetFormat().
		// For uncompressed formats: size = Width * Height * BytesPerPixel
		// For compressed formats: (not supported in this system-memory container unless you add block logic)
		std::vector<uint8> Data = {};
	};

	class Texture final
	{
	public:
		Texture() = default;

		void SetFormat(TEXTURE_FORMAT fmt) noexcept { m_Format = fmt; }
		TEXTURE_FORMAT GetFormat() const noexcept { return m_Format; }

		std::vector<TextureMip>& GetMips() noexcept { return m_Mips; }
		const std::vector<TextureMip>& GetMips() const noexcept { return m_Mips; }

		uint32 GetWidth()  const noexcept { return m_Mips.empty() ? 0 : m_Mips[0].Width; }
		uint32 GetHeight() const noexcept { return m_Mips.empty() ? 0 : m_Mips[0].Height; }

		const uint8* GetData() const noexcept
		{
			ASSERT(!m_Mips.empty(), "Texture is not initialized");
			return m_Mips[0].Data.data();
		}

		uint32 GetDataByteSize() const noexcept
		{
			ASSERT(!m_Mips.empty(), "Texture is not initialized");
			return static_cast<uint32>(m_Mips[0].Data.size());
		}

		bool IsValid() const noexcept { return GetWidth() > 0 && GetHeight() > 0 && (!m_Mips.empty()) && (m_Mips[0].Data.data() != nullptr); }

		void Clear() { m_Mips.clear(); m_Format = TEX_FORMAT_UNKNOWN; }

	public:
		static Texture ConvertGrayScale(const Texture& src);

	private:
		TEXTURE_FORMAT m_Format = TEX_FORMAT_RGBA8_UNORM;
		std::vector<TextureMip> m_Mips = {};
	};
}
