#pragma once
#include "Engine/Physics/Public/Physics.h"

#include "Engine/ECS/Public/Components.h"
#include "Engine/ECS/Public/EcsWorld.h"

namespace shz
{
	class PhysicsSystem final
	{
	public:
		struct CreateInfo final
		{
			Physics::CreateInfo PhysicsCI = {};
		};

	public:
		void Initialize(const CreateInfo& ci = {});
		void Shutdown();

		Physics& GetPhysics() { return m_Physics; }
		const Physics& GetPhysics() const { return m_Physics; }

		void Step(float dt) { m_Physics.Step(dt); }

		void InstallEcsSystems(EcsWorld& ecs);

	private:
		void ensureShapeCreated_Box(CBoxCollider& box);
		void ensureShapeCreated_Sphere(CSphereCollider& sph);
		void ensureShapeCreated_HeightField(CHeightFieldCollider& hf);

		void ensureBodyCreated(
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
