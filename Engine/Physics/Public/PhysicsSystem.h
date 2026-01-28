#pragma once
#include "Engine/Physics/Public/Physics.h"

#include "Engine/ECS/Public/CTransform.h"
#include "Engine/ECS/Public/CRigidbody.h"
#include "Engine/ECS/Public/CBoxCollider.h"
#include "Engine/ECS/Public/CSphereCollider.h"
#include "Engine/ECS/Public/CHeightFieldCollider.h"

#include <flecs.h>

namespace shz
{
    class PhysicsSystem final
    {
    public:
        struct CreateInfo final
        {
            Physics::CreateInfo PhysicsCI = {};
        };

        struct EcsHandles final
        {
            flecs::entity CreateBodies_Box = {};
            flecs::entity CreateBodies_Sphere = {};
            flecs::entity CreateBodies_HeightField = {};

            flecs::entity PushTransform = {};
            flecs::entity WriteBack = {};
            flecs::entity OnRemoveRigidBody = {};
        };

    public:
        void Initialize(const CreateInfo& ci = {});
        void Shutdown();

        Physics& GetPhysics() { return m_Physics; }
        const Physics& GetPhysics() const { return m_Physics; }

        // fixed-step driver에서 호출
        void Step(float dt) { m_Physics.Step(dt); }

        void InstallEcsSystems(flecs::world& ecs);
        EcsHandles InstallEcsSystemsAndGetHandles(flecs::world& ecs);

    private:
        void EnsureShapeCreated_Box(CBoxCollider& box);
        void EnsureShapeCreated_Sphere(CSphereCollider& sph);
        void EnsureShapeCreated_HeightField(CHeightFieldCollider& hf);

        void EnsureBodyCreated(
            CTransform& tr,
            CRigidbody& rb,
            CBoxCollider* box,
            CSphereCollider* sphere,
            CHeightFieldCollider* hf);

        void DestroyBodyAndShapes(CRigidbody* rb, CBoxCollider* box, CSphereCollider* sphere, CHeightFieldCollider* hf);

    private:
        Physics m_Physics = {};
        bool m_bInstalled = false;
    };
}
