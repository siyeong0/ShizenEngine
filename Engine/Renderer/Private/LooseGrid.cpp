#include "pch.h"
#include "Engine/Renderer/Public/LooseGrid.h"

namespace shz
{
    void LooseGrid::Initialize(const CreateInfo& ci)
    {
        m_CellSize = std::max(ci.CellSize, 1.0f);
        m_InvCellSize = 1.0f / m_CellSize;
        m_LooseFactor = std::max(ci.LooseFactor, 1.0f);

        m_Cells.clear();
        m_PayloadMap.clear();
        m_Cells.reserve(ci.ReserveCells);
    }

    void LooseGrid::Reset()
    {
        m_Cells.clear();
        m_PayloadMap.clear();
    }

    LooseGrid::CellCoord LooseGrid::toCell(const float3& p) const
    {
        // Floor for negative coordinates too.
        const int32 cx = static_cast<int32>(std::floor(p.x * m_InvCellSize));
        const int32 cy = static_cast<int32>(std::floor(p.y * m_InvCellSize));
        const int32 cz = static_cast<int32>(std::floor(p.z * m_InvCellSize));
        return { cx, cy, cz };
    }

    void LooseGrid::computeCoveredCellsLoose(const Box& aabb, std::vector<CellCoord>& out) const
    {
        out.clear();

        const float3 c = aabb.Center();
        const float3 e = aabb.Extents();

        // Expand extents by loose factor (reduce update churn)
        const float3 le = e * m_LooseFactor;

        const float3 mn = c - le;
        const float3 mx = c + le;

        const CellCoord c0 = toCell(mn);
        const CellCoord c1 = toCell(mx);

        const int32 dx = c1.x - c0.x;
        const int32 dy = c1.y - c0.y;
        const int32 dz = c1.z - c0.z;

        const int32 maxSpan = 8; // safety clamp (dynamic objects shouldn't cover huge regions)
        const int32 sx0 = c0.x;
        const int32 sy0 = c0.y;
        const int32 sz0 = c0.z;

        const int32 sx1 = (dx > maxSpan) ? (sx0 + maxSpan) : c1.x;
        const int32 sy1 = (dy > maxSpan) ? (sy0 + maxSpan) : c1.y;
        const int32 sz1 = (dz > maxSpan) ? (sz0 + maxSpan) : c1.z;

        for (int32 z = sz0; z <= sz1; ++z)
        {
            for (int32 y = sy0; y <= sy1; ++y)
            {
                for (int32 x = sx0; x <= sx1; ++x)
                {
                    out.push_back({ x, y, z });
                }
            }
        }
    }

    void LooseGrid::InsertOrUpdate(uint32 payload, const Box& worldAabb)
    {
        // Remove previous cells if any
        auto it = m_PayloadMap.find(payload);
        if (it != m_PayloadMap.end())
        {
            // erase payload from old cells
            for (const CellCoord& cc : it->second.Cells)
            {
                auto cellIt = m_Cells.find(cc);
                if (cellIt == m_Cells.end())
                    continue;

                auto& vec = cellIt->second;
                vec.erase(std::remove(vec.begin(), vec.end(), payload), vec.end());

                if (vec.empty())
                {
                    m_Cells.erase(cellIt);
                }
            }
            it->second.Cells.clear();
        }

        // Compute new cells
        std::vector<CellCoord> newCells;
        computeCoveredCellsLoose(worldAabb, newCells);

        PayloadCells pc = {};
        pc.Cells = newCells;

        // Insert into cell lists
        for (const CellCoord& cc : newCells)
        {
            auto& vec = m_Cells[cc];
            vec.push_back(payload);
        }

        m_PayloadMap[payload] = std::move(pc);
    }

    void LooseGrid::Remove(uint32 payload)
    {
        auto it = m_PayloadMap.find(payload);
        if (it == m_PayloadMap.end())
            return;

        for (const CellCoord& cc : it->second.Cells)
        {
            auto cellIt = m_Cells.find(cc);
            if (cellIt == m_Cells.end())
                continue;

            auto& vec = cellIt->second;
            vec.erase(std::remove(vec.begin(), vec.end(), payload), vec.end());

            if (vec.empty())
            {
                m_Cells.erase(cellIt);
            }
        }

        m_PayloadMap.erase(it);
    }

    Box LooseGrid::FrustumWorldAabb(const ViewFrustumExt& frustum)
    {
        Box b;
        for (uint32 i = 0; i < 8; ++i)
        {
            b.Encapsulate(frustum.FrustumCorners[i]);
        }
        return b;
    }

    void LooseGrid::dedupAppend(std::vector<uint32>& out, const std::vector<uint32>& in)
    {
        // Cheap dedup for small lists: sort+unique
        if (in.empty())
            return;

        std::vector<uint32> tmp = in;
        std::sort(tmp.begin(), tmp.end());
        tmp.erase(std::unique(tmp.begin(), tmp.end()), tmp.end());
        out.insert(out.end(), tmp.begin(), tmp.end());
    }

    std::vector<uint32> LooseGrid::QueryFrustum(const ViewFrustumExt& frustum) const
    {
        std::vector<uint32> outPayloads;

        if (m_Cells.empty())
            return outPayloads;

        const Box frAabb = FrustumWorldAabb(frustum);

        const CellCoord c0 = toCell(frAabb.Min());
        const CellCoord c1 = toCell(frAabb.Max());

        // Iterate covered cells (dynamic objects are few, so this stays cheap).
        std::vector<uint32> gathered;
        gathered.reserve(256);

        for (int32 z = c0.z; z <= c1.z; ++z)
        {
            for (int32 y = c0.y; y <= c1.y; ++y)
            {
                for (int32 x = c0.x; x <= c1.x; ++x)
                {
                    CellCoord cc{ x,y,z };
                    auto it = m_Cells.find(cc);
                    if (it == m_Cells.end())
                        continue;

                    const auto& vec = it->second;
                    gathered.insert(gathered.end(), vec.begin(), vec.end());
                }
            }
        }

        dedupAppend(outPayloads, gathered);

		return outPayloads;
    }
} // namespace shz