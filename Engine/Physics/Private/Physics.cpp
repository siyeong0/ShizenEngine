#include "pch.h"
#include "Physics.h"

namespace shz
{
	namespace
	{
		class BPLayerInterface final : public JPH::BroadPhaseLayerInterface
		{
		public:
			BPLayerInterface()
			{
				m_ObjectToBroadPhase[PHYS_LAYER_NON_MOVING] = JPH::BroadPhaseLayer(0);
				m_ObjectToBroadPhase[PHYS_LAYER_MOVING] = JPH::BroadPhaseLayer(1);
			}

			uint GetNumBroadPhaseLayers() const override
			{
				return 2;
			}

			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
			{
				return m_ObjectToBroadPhase[(uint)inLayer];
			}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
			{
				switch (inLayer.GetValue())
				{
				case 0: return "NON_MOVING";
				case 1: return "MOVING";
				default: return "UNKNOWN";
				}
			}
#endif

		private:
			JPH::BroadPhaseLayer m_ObjectToBroadPhase[PHYS_LAYER_COUNT] = {};
		};

		class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override
			{
				return true;
			}
		};

		class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override
			{
				return true;
			}
		};

		static void EnsureJoltInitializedOnce()
		{
			static bool s_Initialized = false;
			if (s_Initialized)
			{
				return;
			}

			JPH::RegisterDefaultAllocator();
			JPH::Factory::sInstance = new JPH::Factory();
			JPH::RegisterTypes();

			s_Initialized = true;
		}
	} // namespace

	struct Physics::Impl final
	{
		CreateInfo CI = {};

		std::unique_ptr<JPH::TempAllocator> TempAlloc = nullptr;
		std::unique_ptr<JPH::JobSystem>     JobSys = nullptr;

		BPLayerInterface              BpLayerIf = {};
		ObjectVsBroadPhaseLayerFilter ObjVsBp = {};
		ObjectLayerPairFilter         ObjPair = {};

		JPH::PhysicsSystem            System = {};
	};

	Physics::Physics()
		: m_pImpl(nullptr)
	{
	}

	Physics::~Physics()
	{
		Shutdown();
	}

	bool Physics::Initialize(const CreateInfo& ci)
	{
		ASSERT(m_pImpl == nullptr, "Physics already initialized.");
		EnsureJoltInitializedOnce();

		m_pImpl = std::make_unique<Impl>();
		m_pImpl->CI = ci;

		m_pImpl->TempAlloc = std::make_unique<JPH::TempAllocatorImpl>(ci.TempAllocatorSizeBytes);

		const uint32 hw = std::max(1u, std::thread::hardware_concurrency());
		const uint32 numThreads = (ci.NumWorkerThreads == 0) ? std::max(1u, hw - 1u) : ci.NumWorkerThreads;

		m_pImpl->JobSys = std::make_unique<JPH::JobSystemThreadPool>(
			JPH::cMaxPhysicsJobs,
			JPH::cMaxPhysicsBarriers,
			(int)numThreads);

		m_pImpl->System.Init(
			ci.MaxBodies,
			ci.NumBodyMutexes,
			ci.MaxBodyPairs,
			ci.MaxContactConstraints,
			m_pImpl->BpLayerIf,
			m_pImpl->ObjVsBp,
			m_pImpl->ObjPair);

		m_pImpl->System.SetGravity(JPH::Vec3(ci.Gravity.x, ci.Gravity.y, ci.Gravity.z));

		return true;
	}

	void Physics::Shutdown()
	{
		if (!m_pImpl)
		{
			return;
		}
		m_pImpl.reset();
	}

	void Physics::Step(float dt)
	{
		ASSERT(m_pImpl, "Physics not initialized.");

		// One substep is fine as a default
		m_pImpl->System.Update(dt, 1, m_pImpl->TempAlloc.get(), m_pImpl->JobSys.get());
	}

	JPH::BodyInterface* Physics::GetBodyInterface() const
	{
		ASSERT(m_pImpl, "Physics not initialized.");

		return &m_pImpl->System.GetBodyInterface();
	}

	Physics::ShapeRef Physics::CreateBoxShape(const float3& halfExtent) const
	{
		return new JPH::BoxShape(JPH::Vec3(halfExtent.x, halfExtent.y, halfExtent.z));
	}

	Physics::ShapeRef Physics::CreateSphereShape(float radius) const
	{
		return new JPH::SphereShape(radius);
	}

	JPH::BodyID Physics::CreateBody(const JPH::BodyCreationSettings& settings, bool bActivate)
	{
		ASSERT(m_pImpl, "Physics not initialized.");

		JPH::BodyInterface& bi = m_pImpl->System.GetBodyInterface();
		JPH::Body* body = bi.CreateBody(settings);
		ASSERT(body, "Failed to create Jolt body.");

		const JPH::BodyID id = body->GetID();
		bi.AddBody(id, bActivate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

		return id;
	}

	void Physics::DestroyBody(JPH::BodyID bodyId)
	{
		ASSERT(m_pImpl, "Physics not initialized.");

		JPH::BodyInterface& bi = m_pImpl->System.GetBodyInterface();
		bi.RemoveBody(bodyId);
		bi.DestroyBody(bodyId);
	}

	void Physics::SetBodyPosition(JPH::BodyID bodyId, const float3& pos, bool bActivate)
	{
		ASSERT(m_pImpl, "Physics not initialized.");

		JPH::BodyInterface& bi = m_pImpl->System.GetBodyInterface();
		bi.SetPosition(
			bodyId,
			JPH::RVec3(pos.x, pos.y, pos.z),
			bActivate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	}

	float3 Physics::GetBodyPosition(JPH::BodyID bodyId) const
	{
		ASSERT(m_pImpl, "Physics not initialized.");

		const JPH::BodyInterface& bi = m_pImpl->System.GetBodyInterface();
		const JPH::RVec3 p = bi.GetPosition(bodyId);
		return float3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
	}
} // namespace shz
