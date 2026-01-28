#pragma once
#include "Engine/Physics/Public/Physics.h"

namespace shz
{
	class PhysicsSystem final
	{
	public:
		PhysicsSystem() = default;
		~PhysicsSystem() = default;

		PhysicsSystem(const PhysicsSystem&) = delete;
		PhysicsSystem& operator=(const PhysicsSystem&) = delete;

		void Initialize();
		void Shutdown();

		void Step(float dt);

		Physics& GetPhysics() { return m_Physics; }
		const Physics& GetPhysics() const { return m_Physics; }

	private:
		Physics m_Physics = {};
	};
} // namespace shz
