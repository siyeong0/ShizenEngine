#include "pch.h"
#include "RenderScene.h"
#include "StaticMeshRenderData.h"

namespace shz
{
    void RenderScene::Reset()
    {
        m_NextObjectId = 1;
        m_NextLightId = 1;

        m_Objects.clear();
        m_Lights.clear();
    }


    Handle<RenderScene::RenderObject> RenderScene::AddObject(const Handle<StaticMeshRenderData>& meshHandle, const Matrix4x4& transform)
    {
        Handle<RenderScene::RenderObject> id;
        RenderObject obj;
        obj.meshHandle = meshHandle;
        obj.transform = transform;
        m_Objects.emplace(id, obj);
        return id;
    }

    void RenderScene::RemoveObject(Handle<RenderScene::RenderObject>  id)
    {
        m_Objects.erase(id);
    }

    void RenderScene::SetObjectTransform(Handle<RenderScene::RenderObject>  id, const Matrix4x4& world)
    {
        auto it = m_Objects.find(id);
        if (it != m_Objects.end())
            it->second.transform = world;
    }

    void RenderScene::SetObjectMesh(Handle<RenderScene::RenderObject>  id, Handle<StaticMeshRenderData> mesh)
    {
        auto it = m_Objects.find(id);
        if (it != m_Objects.end())
            it->second.meshHandle = mesh;
    }

    Handle<RenderScene::LightObject> RenderScene::AddLight(const LightObject& light)
    {
        Handle<RenderScene::LightObject> id{ m_NextLightId++ };
        m_Lights.emplace(id, light);
        return id;
    }

    void RenderScene::RemoveLight(Handle<RenderScene::LightObject> id)
    {
        m_Lights.erase(id);
    }

    void RenderScene::UpdateLight(Handle<RenderScene::LightObject> id, const LightObject& light)
    {
        auto it = m_Lights.find(id);
        if (it != m_Lights.end())
            it->second = light;
    }
} // namespace shz
