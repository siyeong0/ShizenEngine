#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

// Forward declarations for Jolt
namespace JPH
{
	class TempAllocator;
	class JobSystem;
	class PhysicsSystem;
	class BodyInterface;

	class Shape;
	template <class T> class Ref;

	class BodyID;
	class BodyCreationSettings;
}

namespace shz
{
	enum PhysicsObjectLayer : uint8
	{
		PHYS_LAYER_NON_MOVING = 0,
		PHYS_LAYER_MOVING = 1,
		PHYS_LAYER_COUNT
	};

	class Physics final
	{
	public:
		using ShapeRef = JPH::Ref<JPH::Shape>;

		struct CreateInfo final
		{
			uint32 MaxBodies = 65536;
			uint32 NumBodyMutexes = 0;
			uint32 MaxBodyPairs = 65536;
			uint32 MaxContactConstraints = 10240;

			uint32 TempAllocatorSizeBytes = 16u * 1024u * 1024u;
			uint32 NumWorkerThreads = 0; // 0: auto

			float3 Gravity = { 0.0f, -9.81f, 0.0f };
		};

		Physics();
		~Physics();

		Physics(const Physics&) = delete;
		Physics& operator=(const Physics&) = delete;

		bool Initialize(const CreateInfo& ci);
		void Shutdown();

		void Step(float dt);

		JPH::BodyInterface* GetBodyInterface() const;

		ShapeRef CreateBoxShape(const float3& halfExtent) const;
		ShapeRef CreateSphereShape(float radius) const;

		JPH::BodyID CreateBody(const JPH::BodyCreationSettings& settings, bool bActivate);
		void DestroyBody(JPH::BodyID bodyId);

		void SetBodyPosition(JPH::BodyID bodyId, const float3& pos, bool bActivate);
		float3 GetBodyPosition(JPH::BodyID bodyId) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_pImpl;
	};
} // namespace shz
