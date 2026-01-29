#include "pch.h"
#include "Engine/ECSIntegration/Public/PhysicsSystem.h"

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

	void PhysicsSystem::Step(float dt) 
	{
		m_Physics.Step(dt); 

		m_FrameContactEvents.clear();
		m_Physics.ConsumeContactEvents(&m_FrameContactEvents);
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
		auto createBoxBody = ecs.World().observer<CTransform, CRigidbody, CBoxCollider>("Physics.CreateBody.Box")
			.event(flecs::OnSet)
			.each([this](CTransform& tr, CRigidbody& rb, CBoxCollider& box)
				{
					ensureBodyCreated(tr, rb, &box, nullptr, nullptr);
				});

		auto createSphereBody = ecs.World().observer<CTransform, CRigidbody, CSphereCollider>("Physics.CreateBody.Sphere")
			.event(flecs::OnSet)
			.each([this](CTransform& tr, CRigidbody& rb, CSphereCollider& sph)
				{
					ensureBodyCreated(tr, rb, nullptr, &sph, nullptr);
				});

		auto createHeightFieldBody = ecs.World().observer<CTransform, CRigidbody, CHeightFieldCollider>("Physics.CreateBody.HeightField")
			.event(flecs::OnSet)
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

					if (rb.BodyType == ERigidbodyType::Dynamic)
					{
						return; // dynamic driven by physics
					}

					PhysicsBodyHandle bh = {};
					bh.Value = rb.BodyHandle;

					const bool bActivate = (rb.BodyType == ERigidbodyType::Kinematic);
					m_Physics.SetBodyTransform(bh, tr.Position, tr.Rotation, bActivate);
				});
		ecs.RegisterUpdateSystem(pushTransform);

		// Write physics -> transform for Dynamic
		auto writeBack = ecs.World().system<CTransform, CRigidbody>()
			.kind(flecs::OnUpdate)
			.each([this](CTransform& tr, CRigidbody& rb)
				{
					ASSERT(rb.BodyHandle != 0, "Invalid body handle.");

					if (rb.BodyType != ERigidbodyType::Dynamic)
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

					destroyBodyAndShapes(&rb, &box, &sph, &hf);
				});

		m_bInstalled = true;
	}

	// Internal helpers: Shape
	void PhysicsSystem::ensureShapeCreated_Box(CBoxCollider& box)
	{
		ASSERT(box.ShapeHandle == 0, "Shape already created");

		const PhysicsShapeHandle shape = m_Physics.CreateBoxShape(box.Box.Extents());
		box.ShapeHandle = shape.Value;
	}

	void PhysicsSystem::ensureShapeCreated_Sphere(CSphereCollider& sphere)
	{
		ASSERT(sphere.ShapeHandle == 0, "Shape already created");

		const PhysicsShapeHandle shape = m_Physics.CreateSphereShape(sphere.Radius);
		sphere.ShapeHandle = shape.Value;
	}

	void PhysicsSystem::ensureShapeCreated_HeightField(CHeightFieldCollider& heightField)
	{
		ASSERT(heightField.ShapeHandle == 0, "Shape already created");
		ASSERT(heightField.Width > 1 && heightField.Height > 1, "Invalid height field resolution.");
		ASSERT(heightField.Heights.size() == size_t(heightField.Width) * size_t(heightField.Height), "Height field size mismatch.");

		Physics::HeightFieldCreateInfo hci = {};
		hci.pHeights = heightField.Heights.data();
		hci.Width = heightField.Width;
		hci.Height = heightField.Height;
		hci.CellSizeX = heightField.CellSizeX;
		hci.CellSizeZ = heightField.CellSizeZ;
		hci.HeightScale = heightField.HeightScale;
		hci.HeightOffset = heightField.HeightOffset;

		const PhysicsShapeHandle shape = m_Physics.CreateHeightFieldShape(hci);
		heightField.ShapeHandle = shape.Value;
	}

	// Internal helpers: Body creation/destruction
	void PhysicsSystem::ensureBodyCreated(
		CTransform& tr,
		CRigidbody& rb,
		CBoxCollider* box,
		CSphereCollider* sphere,
		CHeightFieldCollider* hf)
	{
		ASSERT(rb.BodyHandle == 0, "Body already created.");

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
		switch (rb.BodyType)
		{
		case ERigidbodyType::Static:    bci.Type = ERigidbodyType::Static;    break;
		case ERigidbodyType::Dynamic:   bci.Type = ERigidbodyType::Dynamic;   break;
		case ERigidbodyType::Kinematic: bci.Type = ERigidbodyType::Kinematic; break;
		default:                          bci.Type = ERigidbodyType::Static;    break;
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

	void PhysicsSystem::destroyBodyAndShapes(CRigidbody* rb, CBoxCollider* box, CSphereCollider* sphere, CHeightFieldCollider* hf)
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
} // namespace shz
