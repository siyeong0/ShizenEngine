#include "pch.h"
#include "Engine/Physics/Public/PhysicsSystem.h"

namespace shz
{
	void PhysicsSystem::Initialize()
	{
		Physics::CreateInfo ci = {};
		ci.Gravity = float3(0.0f, -0.01f, 0.0f);
		m_Physics.Initialize(ci);
	}

	void PhysicsSystem::Shutdown()
	{
		m_Physics.Shutdown();
	}

	void PhysicsSystem::Step(float dt)
	{
		m_Physics.Step(dt);
	}
} // namespace shz
