#include "pch.h"
#include "RenderScene.h"

namespace shz
{
    void RenderScene::Reset()
    {
        m_NextObjectId = 1;
        m_NextLightId = 1;

        m_Objects.clear();
        m_Lights.clear();
    }


    RenderObjectId RenderScene::AddObject(const MeshHandle& meshHandle, const Matrix4x4& transform)
    {
        RenderObjectId id{ m_NextObjectId++ };
        RenderObject obj;
        obj.meshHandle = meshHandle;
        obj.transform = transform;
        m_Objects.emplace(id, obj);
        return id;
    }

    void RenderScene::RemoveObject(RenderObjectId id)
    {
        m_Objects.erase(id);
    }

    void RenderScene::SetObjectTransform(RenderObjectId id, const Matrix4x4& world)
    {
        auto it = m_Objects.find(id);
        if (it != m_Objects.end())
            it->second.transform = world;
    }

    void RenderScene::SetObjectMesh(RenderObjectId id, MeshHandle mesh)
    {
        auto it = m_Objects.find(id);
        if (it != m_Objects.end())
            it->second.meshHandle = mesh;
    }

    LightId RenderScene::AddLight(const LightDesc& desc)
    {
        LightId id{ m_NextLightId++ };
        m_Lights.emplace(id, desc);
        return id;
    }

    void RenderScene::RemoveLight(LightId id)
    {
        m_Lights.erase(id);
    }

    void RenderScene::UpdateLight(LightId id, const LightDesc& desc)
    {
        auto it = m_Lights.find(id);
        if (it != m_Lights.end())
            it->second = desc;
    }
} // namespace shz
