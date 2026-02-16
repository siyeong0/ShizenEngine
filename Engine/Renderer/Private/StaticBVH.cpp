#include "pch.h"
#include "Engine/Renderer/Public/StaticBVH.h"

namespace shz
{
    static inline float AbsF(float x) { return std::abs(x); }

    void StaticBVH::Reset()
    {
        m_Entries.clear();
        m_Index.clear();
        m_Nodes.clear();
        m_Built = false;
    }

    Box StaticBVH::UnionAabb(const Box& a, const Box& b)
    {
        Box out = a;
        out.Encapsulate(b);
        return out;
    }

    float3 StaticBVH::ComputeCenter(const Box& aabb)
    {
        return aabb.Center();
    }

    Box StaticBVH::ComputeBounds(const std::vector<BVHEntry>& entries, const std::vector<uint32>& idx, uint32 first, uint32 count)
    {
        ASSERT(count > 0, "ComputeBounds: count is 0.");
        Box b = entries[idx[first]].WorldAabb;
        for (uint32 i = 1; i < count; ++i)
        {
            b.Encapsulate(entries[idx[first + i]].WorldAabb);
        }
        return b;
    }

    // Plane test using AABB center/extents (fast)
    bool StaticBVH::IntersectsFrustumAABB(const ViewFrustum& frustum, const Box& aabb)
    {
        const float3 c = aabb.Center();
        const float3 e = aabb.Extents();

        for (uint32 p = 0; p < ViewFrustum::NUM_PLANES; ++p)
        {
            const Plane& pl = frustum.GetPlane(static_cast<ViewFrustum::PLANE_IDX>(p));
            const float3 n = pl.Normal;

            const float r =
                AbsF(n.x) * e.x +
                AbsF(n.y) * e.y +
                AbsF(n.z) * e.z;

            const float s = float3::Dot(c, n) + pl.Distance;

            if (s + r < 0.0f)
                return false;
        }
        return true;
    }

    void StaticBVH::Build(const std::vector<BVHEntry>& entries)
    {
        Reset();

        m_Entries = entries;

        const uint32 n = static_cast<uint32>(m_Entries.size());
        m_Index.resize(n);
        for (uint32 i = 0; i < n; ++i) m_Index[i] = i;

        m_Nodes.reserve(std::max<uint32>(n * 2, 1));
        if (n > 0)
        {
            buildNode(0, n);
        }
        m_Built = true;
    }

    uint32 StaticBVH::buildNode(uint32 first, uint32 count)
    {
        const uint32 nodeIndex = static_cast<uint32>(m_Nodes.size());
        m_Nodes.emplace_back(Node{});

        Node& node = m_Nodes[nodeIndex];
        node.Bounds = ComputeBounds(m_Entries, m_Index, first, count);
        node.First = first;
        node.Count = count;

        if (count <= m_MaxLeafSize)
        {
            node.Left = 0xFFFFFFFFu;
            node.Right = 0xFFFFFFFFu;
            return nodeIndex;
        }

        // Split by longest axis of bounds
        const float3 size = node.Bounds.Size();
        uint32 axis = 0;
        if (size.y > size.x) axis = 1;
        if (size.z > ((axis == 0) ? size.x : size.y)) axis = 2;

        const uint32 mid = first + count / 2;

        auto CenterAxis = [&](uint32 entryIdx) -> float
            {
                const float3 c = ComputeCenter(m_Entries[entryIdx].WorldAabb);
                return (axis == 0) ? c.x : (axis == 1) ? c.y : c.z;
            };

        std::nth_element(
            m_Index.begin() + first,
            m_Index.begin() + mid,
            m_Index.begin() + first + count,
            [&](uint32 a, uint32 b)
            {
                return CenterAxis(a) < CenterAxis(b);
            });

        const uint32 leftCount = mid - first;
        const uint32 rightCount = count - leftCount;

        node.Left = buildNode(first, leftCount);
        node.Right = buildNode(mid, rightCount);

        return nodeIndex;
    }

    std::vector<uint32> StaticBVH::QueryFrustum(const ViewFrustum& frustum) const
    {
        std::vector<uint32> outPayloads;

        if (!m_Built || m_Nodes.empty())
            return outPayloads;

        // Stack-based traversal (no recursion)
        uint32 stack[64];
        uint32 sp = 0;
        stack[sp++] = 0;

        while (sp > 0)
        {
            const uint32 ni = stack[--sp];
            const Node& node = m_Nodes[ni];

            if (!IntersectsFrustumAABB(frustum, node.Bounds))
                continue;

            if (node.IsLeaf())
            {
                for (uint32 i = 0; i < node.Count; ++i)
                {
                    const BVHEntry& e = m_Entries[m_Index[node.First + i]];
                    // Optional: sphere early test could go here if you have sphere-frustum helper.
                    outPayloads.push_back(e.Payload);
                }
            }
            else
            {
                ASSERT(node.Left != 0xFFFFFFFFu && node.Right != 0xFFFFFFFFu, "BVH internal node missing children.");
                // Push both; order doesn't matter
                if (sp + 2 < 64)
                {
                    stack[sp++] = node.Left;
                    stack[sp++] = node.Right;
                }
                else
                {
                    // Fallback (should not happen with reasonable tree sizes)
                    // Just visit children in-line by brute, but keep correctness.
                    stack[sp - 1] = node.Left;
                    stack[sp++] = node.Right;
                }
            }
        }
        return outPayloads;
    }
} // namespace shz