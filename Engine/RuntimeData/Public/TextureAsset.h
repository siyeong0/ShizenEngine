#pragma once
#include <vector>

#include "Primitives/BasicTypes.h"

namespace shz
{
	struct TextureMip final
	{
		uint32 Width = 0;
		uint32 Height = 0;

		// RGBA8 tightly packed
		std::vector<uint8> RGBA = {};
	};

	class TextureAsset final
	{
	public:
		TextureAsset() = default;

		// System memory data
		std::vector<TextureMip>& GetMips() noexcept { return m_Mips; }
		const std::vector<TextureMip>& GetMips() const noexcept { return m_Mips; }

		uint32 GetWidth() const noexcept { return m_Mips.empty() ? 0 : m_Mips[0].Width; }
		uint32 GetHeight() const noexcept { return m_Mips.empty() ? 0 : m_Mips[0].Height; }

		const uint8* GetRGBA() const noexcept
		{
			ASSERT(!m_Mips.empty(), "Texture asset is not initilized");
			return m_Mips[0].RGBA.data();
		}

		uint32 GetRGBAByteSize() const noexcept
		{
			ASSERT(!m_Mips.empty(), "Texture asset is not initilized");
			return static_cast<uint32>(m_Mips[0].RGBA.size());
		}

		bool IsValid() const noexcept
		{
			return GetWidth() > 0 && GetHeight() > 0 && GetRGBA() != nullptr;
		}

		void Clear()
		{
			m_Mips.clear();
		}

	private:
		std::vector<TextureMip> m_Mips = {};
	};
}
