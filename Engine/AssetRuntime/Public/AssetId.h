#include <functional>

namespace shz
{
    struct AssetId final
    {
    public:
        AssetId() : m_Id(m_sNextId++) {}

        friend bool operator==(const AssetId& a, const AssetId& b) noexcept { return a.m_Id == b.m_Id; }
        friend bool operator!=(const AssetId& a, const AssetId& b) noexcept { return a.m_Id != b.m_Id; }

    private:
        uint32_t m_Id = 0;
        inline static uint32_t m_sNextId = 1;

        friend struct std::hash<shz::AssetId>;
    };
}

namespace std
{
    template<>
    struct hash<shz::AssetId>
    {
        size_t operator()(const shz::AssetId& h) const noexcept
        {
            return std::hash<uint32_t>{}(h.m_Id); 
        }
    };
}
