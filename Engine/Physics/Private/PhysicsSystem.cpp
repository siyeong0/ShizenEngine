#include "pch.h"
#include "Engine/Physics/Public/PhysicsSystem.h"

namespace shz
{
	// Lifecycle
	void PhysicsSystem::Initialize(const CreateInfo& ci)
	{
		m_Physics.Initialize(ci.PhysicsCI);
		m_bInstalled = false;
	}

	void PhysicsSystem::Shutdown()
	{
		m_Physics.Shutdown();
		m_bInstalled = false;
	}

	void PhysicsSystem::DestroyBodyAndShapes(CRigidbody* rb, CBoxCollider* box, CSphereCollider* sphere, CHeightFieldCollider* hf)
	{
		if (rb && rb->BodyHandle != 0)
		{
			PhysicsBodyHandle bh = {};
			bh.Value = rb->BodyHandle;
			m_Physics.DestroyBody(bh);
			rb->BodyHandle = 0;
		}

		// Shapes are safe to keep shared, but for simplicity we release if component is removed.
		if (box && box->ShapeHandle != 0)
		{
			PhysicsShapeHandle sh = {};
			sh.Value = box->ShapeHandle;
			m_Physics.ReleaseShape(sh);
			box->ShapeHandle = 0;
		}

		if (sphere && sphere->ShapeHandle != 0)
		{
			PhysicsShapeHandle sh = {};
			sh.Value = sphere->ShapeHandle;
			m_Physics.ReleaseShape(sh);
			sphere->ShapeHandle = 0;
		}

		if (hf && hf->ShapeHandle != 0)
		{
			PhysicsShapeHandle sh = {};
			sh.Value = hf->ShapeHandle;
			m_Physics.ReleaseShape(sh);
			hf->ShapeHandle = 0;
		}
	}

	// Install Flecs systems
	void PhysicsSystem::InstallEcsSystems(EcsWorld& ecs)
	{
		ASSERT(!m_bInstalled, "ECS systems already installed.");

		// Fixed: Physics step
		auto fixedStep = ecs.World().system<>("Physics.Step")
			.each([this, &ecs]()
				{
					const float dtFixed = ecs.GetFixedDeltaTime();
					Step(dtFixed);
				});
		ecs.RegisterFixedSystem(fixedStep);

		// Create bodies when (Transform + Rigidbody + any collider)
		auto createBoxBody = ecs.World().system<CTransform, CRigidbody, CBoxCollider>("Physics.CreateBody.Box")
			.kind(flecs::OnUpdate)
			.each([this](CTransform& tr, CRigidbody& rb, CBoxCollider& box)
				{
					ensureBodyCreated(tr, rb, &box, nullptr, nullptr);
				});

		auto createSphereBody = ecs.World().system<CTransform, CRigidbody, CSphereCollider>("Physics.CreateBody.Sphere")
			.kind(flecs::OnUpdate)
			.each([this](CTransform& tr, CRigidbody& rb, CSphereCollider& sph)
				{
					ensureBodyCreated(tr, rb, nullptr, &sph, nullptr);
				});

		auto createHeightFieldBody = ecs.World().system<CTransform, CRigidbody, CHeightFieldCollider>("Physics.CreateBody.HeightField")
			.kind(flecs::OnUpdate)
			.each([this](CTransform& tr, CRigidbody& rb, CHeightFieldCollider& hf)
				{
					ensureBodyCreated(tr, rb, nullptr, nullptr, &hf);
				});

		ecs.RegisterUpdateSystem(createBoxBody);
		ecs.RegisterUpdateSystem(createSphereBody);
		ecs.RegisterUpdateSystem(createHeightFieldBody);

		// Push transform -> physics for Static/Kinematic
		auto pushTransform = ecs.World().system<CTransform, CRigidbody>()
			.kind(flecs::OnUpdate)
			.each([this](CTransform& tr, CRigidbody& rb)
				{
					ASSERT(rb.BodyHandle != 0, "Invalid body handle.");

					if (rb.Motion == ERigidBodyMotion::Dynamic)
					{
						return; // dynamic driven by physics
					}

					PhysicsBodyHandle bh = {};
					bh.Value = rb.BodyHandle;

					const bool bActivate = (rb.Motion == ERigidBodyMotion::Kinematic);
					m_Physics.SetBodyTransform(bh, tr.Position, tr.Rotation, bActivate);
				});
		ecs.RegisterUpdateSystem(pushTransform);


		// Write physics -> transform for Dynamic
		auto writeBack = ecs.World().system<CTransform, CRigidbody>()
			.kind(flecs::OnUpdate)
			.each([this](CTransform& tr, CRigidbody& rb)
				{
					ASSERT(rb.BodyHandle != 0, "Invalid body handle.");

					if (rb.Motion != ERigidBodyMotion::Dynamic)
					{
						return;
					}

					PhysicsBodyHandle bh = {};
					bh.Value = rb.BodyHandle;

					float3 pos = tr.Position;
					float3 rot = tr.Rotation;

					m_Physics.GetBodyTransform(bh, &pos, &rot);

					tr.Position = pos;
					tr.Rotation = rot;
				});
		ecs.RegisterFixedSystem(writeBack);

		// Cleanup when Rigidbody is removed
		auto onRemoveRigidbody = ecs.World().observer<CRigidbody>()
			.event(flecs::OnRemove)
			.each([this](flecs::entity e, CRigidbody& rb)
				{
					// Fetch colliders if present
					CBoxCollider& box = e.get_mut<CBoxCollider>();
					CSphereCollider& sph = e.get_mut<CSphereCollider>();
					CHeightFieldCollider& hf = e.get_mut<CHeightFieldCollider>();

					DestroyBodyAndShapes(&rb, &box, &sph, &hf);
				});

		m_bInstalled = true;
	}

	// Internal helpers: Shape
	void PhysicsSystem::ensureShapeCreated_Box(CBoxCollider& box)
	{
		if (box.ShapeHandle != 0)
		{
			return;
		}

		const PhysicsShapeHandle shape = m_Physics.CreateBoxShape(box.Box.Extents());
		box.ShapeHandle = shape.Value;
	}

	void PhysicsSystem::ensureShapeCreated_Sphere(CSphereCollider& sph)
	{
		if (sph.ShapeHandle != 0)
		{
			return;
		}

		const PhysicsShapeHandle shape = m_Physics.CreateSphereShape(sph.Radius);
		sph.ShapeHandle = shape.Value;
	}

	void PhysicsSystem::ensureShapeCreated_HeightField(CHeightFieldCollider& hf)
	{
		if (hf.ShapeHandle != 0)
		{
			return;
		}

		ASSERT(hf.Width > 1 && hf.Height > 1, "Invalid height field resolution.");
		ASSERT(hf.Heights.size() == size_t(hf.Width) * size_t(hf.Height), "Height field size mismatch.");

		Physics::HeightFieldCreateInfo hci = {};
		hci.pHeights = hf.Heights.data();
		hci.Width = hf.Width;
		hci.Height = hf.Height;
		hci.CellSizeX = hf.CellSizeX;
		hci.CellSizeZ = hf.CellSizeZ;
		hci.HeightScale = hf.HeightScale;
		hci.HeightOffset = hf.HeightOffset;

		const PhysicsShapeHandle shape = m_Physics.CreateHeightFieldShape(hci);
		hf.ShapeHandle = shape.Value;
	}

	// Internal helpers: Body creation/destruction
	void PhysicsSystem::ensureBodyCreated(
		CTransform& tr,
		CRigidbody& rb,
		CBoxCollider* box,
		CSphereCollider* sphere,
		CHeightFieldCollider* hf)
	{
		if (rb.BodyHandle != 0)
		{
			return;
		}

		// Ensure shape exists (pick exactly one collider for now)
		PhysicsShapeHandle shape = {};

		bool bSensor = false;

		if (box)
		{
			ensureShapeCreated_Box(*box);
			shape.Value = box->ShapeHandle;
			bSensor = box->bIsSensor;
		}
		else if (sphere)
		{
			ensureShapeCreated_Sphere(*sphere);
			shape.Value = sphere->ShapeHandle;
			bSensor = sphere->bIsSensor;
		}
		else if (hf)
		{
			ensureShapeCreated_HeightField(*hf);
			shape.Value = hf->ShapeHandle;
			bSensor = hf->bIsSensor;
		}
		else
		{
			// No collider => no body
			return;
		}

		ASSERT(shape.IsValid(), "Shape is invalid.");

		Physics::BodyCreateInfo bci = {};
		bci.Shape = shape;
		bci.Position = tr.Position;
		bci.RotationEulerRad = tr.Rotation;

		// Motion
		switch (rb.Motion)
		{
		case ERigidBodyMotion::Static:    bci.Type = ERigidBodyType::Static;    break;
		case ERigidBodyMotion::Dynamic:   bci.Type = ERigidBodyType::Dynamic;   break;
		case ERigidBodyMotion::Kinematic: bci.Type = ERigidBodyType::Kinematic; break;
		default:                          bci.Type = ERigidBodyType::Static;    break;
		}

		// Layer
		bci.Layer = (rb.Layer == 0) ? EPhysicsObjectLayer::NonMoving : EPhysicsObjectLayer::Moving;

		bci.Mass = rb.Mass;
		bci.LinearDamping = rb.LinearDamping;
		bci.AngularDamping = rb.AngularDamping;
		bci.bAllowSleeping = rb.bAllowSleeping;
		bci.bEnableGravity = rb.bEnableGravity;
		bci.bStartActive = rb.bStartActive;
		bci.bIsSensor = bSensor;

		const PhysicsBodyHandle body = m_Physics.CreateBody(bci);
		rb.BodyHandle = body.Value;
	}
} // namespace shz
