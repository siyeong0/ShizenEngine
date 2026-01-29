#include "pch.h"
#include "Engine/Physics/Public/Physics.h"
#include "Engine/Physics/Public/PhysicsEvent.h"

namespace shz
{
	// Small math helpers (float3 <-> JPH)
	static inline JPH::Vec3 toJPH(const float3& v)
	{
		return JPH::Vec3(v.x, v.y, v.z);
	}

	static inline float3 fromJPH(const JPH::Vec3& v)
	{
		return float3{ v.GetX(), v.GetY(), v.GetZ() };
	}

	// Euler (XYZ) <-> Quaternion
	static inline JPH::Quat quatFromEulerXYZ(const float3& eulerRad)
	{
		const float cx = std::cos(eulerRad.x * 0.5f);
		const float sx = std::sin(eulerRad.x * 0.5f);
		const float cy = std::cos(eulerRad.y * 0.5f);
		const float sy = std::sin(eulerRad.y * 0.5f);
		const float cz = std::cos(eulerRad.z * 0.5f);
		const float sz = std::sin(eulerRad.z * 0.5f);

		// q = qx * qy * qz (XYZ intrinsic)
		const float w = cx * cy * cz - sx * sy * sz;
		const float x = sx * cy * cz + cx * sy * sz;
		const float y = cx * sy * cz - sx * cy * sz;
		const float z = cx * cy * sz + sx * sy * cz;

		return JPH::Quat(x, y, z, w).Normalized();
	}

	static inline float3 eulerXYZFromQuat(const JPH::Quat& qIn)
	{
		const JPH::Quat q = qIn.Normalized();
		const float x = q.GetX();
		const float y = q.GetY();
		const float z = q.GetZ();
		const float w = q.GetW();

		// XYZ convention
		const float t0 = 2.0f * (w * x + y * z);
		const float t1 = 1.0f - 2.0f * (x * x + y * y);
		const float rollX = std::atan2(t0, t1);

		float t2 = 2.0f * (w * y - z * x);
		t2 = Clamp01(t2);
		const float pitchY = std::asin(t2);

		const float t3 = 2.0f * (w * z + x * y);
		const float t4 = 1.0f - 2.0f * (y * y + z * z);
		const float yawZ = std::atan2(t3, t4);

		return float3{ rollX, pitchY, yawZ };
	}

	static inline JPH::EMotionType toJPHMotionType(ERigidbodyType t)
	{
		switch (t)
		{
		case ERigidbodyType::Static:    return JPH::EMotionType::Static;
		case ERigidbodyType::Dynamic:   return JPH::EMotionType::Dynamic;
		case ERigidbodyType::Kinematic: return JPH::EMotionType::Kinematic;
		default:                        return JPH::EMotionType::Static;
		}
	}

	static inline JPH::ObjectLayer toJPHObjectLayer(EPhysicsObjectLayer layer)
	{
		// 0 = NonMoving, 1 = Moving
		return static_cast<JPH::ObjectLayer>(layer);
	}

	// ------------------------------------------------------------------------
	// Layers / filters (2-layer setup)
	// ------------------------------------------------------------------------
	namespace layers
	{
		static constexpr JPH::ObjectLayer NON_MOVING = 0;
		static constexpr JPH::ObjectLayer MOVING = 1;
		static constexpr JPH::ObjectLayer NUM_LAYERS = 2;

		static constexpr JPH::BroadPhaseLayer BP_NON_MOVING(0);
		static constexpr JPH::BroadPhaseLayer BP_MOVING(1);
		static constexpr uint32_t NUM_BP_LAYERS = 2;
	}

	class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
	{
	public:
		BPLayerInterfaceImpl()
		{
			mObjectToBroadPhase[layers::NON_MOVING] = layers::BP_NON_MOVING;
			mObjectToBroadPhase[layers::MOVING] = layers::BP_MOVING;
		}

		uint32 GetNumBroadPhaseLayers() const override
		{
			return layers::NUM_BP_LAYERS;
		}

		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
		{
			ASSERT(layer < layers::NUM_LAYERS, "Layer index out of bounds.");
			return mObjectToBroadPhase[layer];
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
		{
			switch ((JPH::BroadPhaseLayer::Type)inLayer)
			{
			case 0: return "NON_MOVING";
			case 1: return "MOVING";
			default: return "UNKNOWN";
			}
		}
#endif

	private:
		JPH::BroadPhaseLayer mObjectToBroadPhase[layers::NUM_LAYERS];
	};

	class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
		{
			// Simple 2-layer rules:
			// NonMoving collides with Moving, and Moving collides with both.
			if (inLayer1 == layers::NON_MOVING)
			{
				return (inLayer2 == layers::BP_MOVING);
			}
			if (inLayer1 == layers::MOVING)
			{
				return (inLayer2 == layers::BP_NON_MOVING) || (inLayer2 == layers::BP_MOVING);
			}
			return false;
		}
	};

	class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
		{
			if (inObject1 == layers::NON_MOVING && inObject2 == layers::NON_MOVING)
			{
				return false;
			}
			return true; // everything else collides
		}
	};

	// ------------------------------------------------------------------------
	// Physics::Impl
	// ------------------------------------------------------------------------
	struct Physics::Impl final
	{
		bool bInitialized = false;

		// Jolt core
		JPH::TempAllocatorImpl* pTempAllocator = nullptr;
		JPH::JobSystemThreadPool* pJobSystem = nullptr;

		// Physics
		JPH::PhysicsSystem System = {};

		// Filters / interfaces
		BPLayerInterfaceImpl BroadPhaseLayerInterface = {};
		ObjectVsBroadPhaseLayerFilterImpl ObjVsBPLayerFilter = {};
		ObjectLayerPairFilterImpl ObjLayerPairFilter = {};

		// Shape storage (opaque handles)
		std::mutex ShapeMutex = {};
		uint64 NextShapeId = 1;
		std::unordered_map<uint64, JPH::RefConst<JPH::Shape>> Shapes = {};

		// Contact
		std::mutex ContactMutex = {};
		std::vector<ContactEvent> ContactEvents = {}; // step 동안 누적

		// Helpers
		JPH::BodyInterface& BodyIF()
		{
			return System.GetBodyInterface();
		}

		const JPH::BodyInterface& BodyIF() const
		{
			return System.GetBodyInterface();
		}

		static inline PhysicsBodyHandle MakeBodyHandle(JPH::BodyID id)
		{
			// Ensure 0 is invalid.
			// BodyID packs index+sequence into 32-bit.
			const uint32 packed = id.GetIndexAndSequenceNumber();
			PhysicsBodyHandle h = {};
			h.Value = packed + 1;
			return h;
		}

		static inline JPH::BodyID ToBodyID(PhysicsBodyHandle h)
		{
			ASSERT(h.IsValid(), "Invalid handle");
			return JPH::BodyID(h.Value - 1);
		}

		PhysicsShapeHandle StoreShape(JPH::RefConst<JPH::Shape> shape)
		{
			ASSERT(shape, "Shape is null.");

			std::scoped_lock lock(ShapeMutex);

			const uint64 id = NextShapeId++;
			Shapes.emplace(id, shape);

			PhysicsShapeHandle out = {};
			out.Value = id;
			return out;
		}

		JPH::RefConst<JPH::Shape> GetShape(PhysicsShapeHandle h) const
		{
			ASSERT(h.IsValid(), "Invalid handle");

			auto it = Shapes.find(h.Value);
			if (it == Shapes.end())
			{
				ASSERT(false, "Shape not exists in a physics world.");
				return nullptr;
			}

			return it->second;
		}

		void ReleaseShape(PhysicsShapeHandle h)
		{
			ASSERT(h.IsValid(), "Invalid handle");

			std::scoped_lock lock(ShapeMutex);
			Shapes.erase(h.Value);
		}
		
		class ContactListenerImpl final : public JPH::ContactListener
		{
		public:
			Impl* pOwner = nullptr;

			JPH::ValidateResult OnContactValidate(
				const JPH::Body& inBody1,
				const JPH::Body& inBody2,
				JPH::RVec3Arg inBaseOffset,
				const JPH::CollideShapeResult& inCollisionResult) override
			{
				return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
			}

			void OnContactAdded(
				const JPH::Body& inBody1,
				const JPH::Body& inBody2,
				const JPH::ContactManifold& inManifold,
				JPH::ContactSettings& /*ioSettings*/) override
			{
				if (!pOwner) return;

				ContactEvent ev = {};
				ev.Type = EContactEventType::Added;
				ev.BodyA = Impl::MakeBodyHandle(inBody1.GetID());
				ev.BodyB = Impl::MakeBodyHandle(inBody2.GetID());

				// Optional contact info
				ev.NormalWS = fromJPH(inManifold.mWorldSpaceNormal);
				ev.PenetrationDepth = inManifold.mPenetrationDepth;

				// Sensor 판단(버전/설정에 따라 다를 수 있으니 최소한 플래그로만)
				// ev.bIsSensor = inBody1.IsSensor() || inBody2.IsSensor(); // 가능하면 이런 식

				std::scoped_lock lock(pOwner->ContactMutex);
				pOwner->ContactEvents.push_back(ev);
			}

			void OnContactPersisted(
				const JPH::Body& inBody1,
				const JPH::Body& inBody2,
				const JPH::ContactManifold& inManifold,
				JPH::ContactSettings& /*ioSettings*/) override
			{
				if (!pOwner) return;

				ContactEvent ev = {};
				ev.Type = EContactEventType::Persisted;
				ev.BodyA = Impl::MakeBodyHandle(inBody1.GetID());
				ev.BodyB = Impl::MakeBodyHandle(inBody2.GetID());
				ev.NormalWS = fromJPH(inManifold.mWorldSpaceNormal);
				ev.PenetrationDepth = inManifold.mPenetrationDepth;

				std::scoped_lock lock(pOwner->ContactMutex);
				pOwner->ContactEvents.push_back(ev);
			}

			void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
			{
				if (!pOwner) return;

				// Removed는 Body를 직접 안 주고 SubShapeIDPair를 주는 형태가 흔함(버전마다 다름).
				// 그래도 pair에서 BodyID를 얻을 수 있음:
				ContactEvent ev = {};
				ev.Type = EContactEventType::Removed;
				ev.BodyA = Impl::MakeBodyHandle(inSubShapePair.GetBody1ID());
				ev.BodyB = Impl::MakeBodyHandle(inSubShapePair.GetBody2ID());

				std::scoped_lock lock(pOwner->ContactMutex);
				pOwner->ContactEvents.push_back(ev);
			}
		};

		ContactListenerImpl ContactListener = {};
	};

	// ------------------------------------------------------------------------
	// Physics public
	// ------------------------------------------------------------------------
	Physics::Physics()
		: m_pImpl(std::make_unique<Impl>())
	{
	}

	Physics::~Physics()
	{
		Shutdown();
	}

	bool Physics::Initialize(const CreateInfo& ci)
	{
		if (!m_pImpl)
		{
			m_pImpl = std::make_unique<Impl>();
		}

		Impl& I = *m_pImpl;
		if (I.bInitialized)
		{
			return true;
		}

		// Jolt global init
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();

		// Allocators / job system
		I.pTempAllocator = new JPH::TempAllocatorImpl(ci.TempAllocatorSizeBytes);

		uint32 numThreads = ci.NumWorkerThreads;
		if (numThreads == 0)
		{
			const unsigned hc = std::thread::hardware_concurrency();
			// Keep at least 1 worker, but don't explode on unknown returns 0.
			numThreads = (hc > 1) ? (hc - 1) : 1;
		}

		I.pJobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

		// PhysicsSystem init
		I.System.Init(
			ci.MaxBodies,
			ci.NumBodyMutexes,
			ci.MaxBodyPairs,
			ci.MaxContactConstraints,
			I.BroadPhaseLayerInterface,
			I.ObjVsBPLayerFilter,
			I.ObjLayerPairFilter);

		// Gravity
		I.System.SetGravity(toJPH(ci.Gravity));

		// Contact listener
		I.ContactListener.pOwner = &I;
		I.System.SetContactListener(&I.ContactListener);

		I.bInitialized = true;
		return true;
	}

	void Physics::Shutdown()
	{
		if (!m_pImpl)
		{
			return;
		}

		Impl& I = *m_pImpl;
		if (!I.bInitialized)
		{
			return;
		}

		// Destroy all bodies (best-effort)
		{
			// We don't keep a list; users should destroy explicitly.
			// Still, clear shape table.
			std::scoped_lock lock(I.ShapeMutex);
			I.Shapes.clear();
		}

		delete I.pJobSystem;
		I.pJobSystem = nullptr;

		delete I.pTempAllocator;
		I.pTempAllocator = nullptr;

		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;

		I.bInitialized = false;
	}

	void Physics::Step(float dt)
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");

		Impl& I = *m_pImpl;

		// Clear per-step events
		{
			std::scoped_lock lock(I.ContactMutex);
			I.ContactEvents.clear();
		}

		// TODO: Typical: 1 collision step, 1 integration sub-step.
		I.System.Update(dt, 1, I.pTempAllocator, I.pJobSystem);
	}

	PhysicsShapeHandle Physics::CreateBoxShape(const float3& halfExtent)
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");

		Impl& I = *m_pImpl;

		const JPH::Vec3 he = toJPH(halfExtent);
		// Jolt expects positive extents
		ASSERT(he.GetX() > 0.0f && he.GetY() > 0.0f || he.GetZ() > 0.0f, "Expects positive extents.");

		JPH::BoxShapeSettings settings(he);
		JPH::ShapeSettings::ShapeResult res = settings.Create();
		if (res.HasError())
		{
			ASSERT(false, "Shape creation failed.\nERROR MESSAGE : %s", res.GetError().c_str());
			return {};
		}

		return I.StoreShape(res.Get());
	}

	PhysicsShapeHandle Physics::CreateSphereShape(float radius)
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");

		Impl& I = *m_pImpl;

		ASSERT(radius > 0.0f, "Expects positive radias.");

		JPH::SphereShapeSettings settings(radius);
		JPH::ShapeSettings::ShapeResult res = settings.Create();
		if (res.HasError())
		{
			ASSERT(false, "Shape creation failed.\nERROR MESSAGE : %s", res.GetError().c_str());
			return {};
		}

		return I.StoreShape(res.Get());
	}

	PhysicsShapeHandle Physics::CreateHeightFieldShape(const HeightFieldCreateInfo& ci)
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");

		Impl& I = *m_pImpl;

		ASSERT(ci.pHeights && ci.Width > 1 && ci.Height > 2, "Invalid height params.");

		// Jolt expects samples in float array. We'll bake scale/offset into sample values here.
		// Layout: row-major, x changes fastest.
		std::vector<float> samples;
		samples.resize(static_cast<size_t>(ci.Width) * static_cast<size_t>(ci.Height));

		for (uint32 z = 0; z < ci.Height; ++z)
		{
			for (uint32 x = 0; x < ci.Width; ++x)
			{
				const size_t idx = static_cast<size_t>(z) * ci.Width + x;
				float h = ci.pHeights[idx];
				h = h * ci.HeightScale + ci.HeightOffset;
				samples[idx] = h;
			}
		}

		// World-space scale for XZ cell sizes. Heights are already baked above.
		const JPH::Vec3 offset = JPH::Vec3(0, 0, 0);
		const JPH::Vec3 scale = JPH::Vec3(ci.CellSizeX, 1.0f, ci.CellSizeZ);
		const uint32 numSamples = ci.Width;


		JPH::HeightFieldShapeSettings settings(
			samples.data(),
			offset,
			scale,
			numSamples,
			nullptr,
			JPH::PhysicsMaterialList());

		// Keep a copy of samples alive until shape is created
		JPH::ShapeSettings::ShapeResult res = settings.Create();
		if (res.HasError())
		{
			ASSERT(false, "Shape creation failed.\nERROR MESSAGE : %s", res.GetError().c_str());
			return {};
		}

		return I.StoreShape(res.Get());
	}

	void Physics::ReleaseShape(PhysicsShapeHandle shape)
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");
		m_pImpl->ReleaseShape(shape);
	}

	PhysicsBodyHandle Physics::CreateBody(const BodyCreateInfo& ci)
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");

		Impl& I = *m_pImpl;

		const JPH::RefConst<JPH::Shape> shape = I.GetShape(ci.Shape);
		ASSERT(shape != nullptr, "Shape is null.");

		const JPH::Vec3 pos = toJPH(ci.Position);
		const JPH::Quat rot = quatFromEulerXYZ(ci.RotationEulerRad);

		JPH::BodyCreationSettings bcs(
			shape,
			pos,
			rot,
			toJPHMotionType(ci.Type),
			toJPHObjectLayer(ci.Layer));

		// Sensor flag: in Jolt, use "IsSensor" on shape via material/subshape?
		// There is BodyCreationSettings::mIsSensor for broad sensor behavior (depending on version).
		// If your Jolt version doesn't have mIsSensor, remove this line.
#if defined(JPH_VERSION) || 1
		bcs.mIsSensor = ci.bIsSensor;
#endif

		bcs.mAllowSleeping = ci.bAllowSleeping;

		// Gravity factor (1 = enabled, 0 = disabled)
		bcs.mGravityFactor = ci.bEnableGravity ? 1.0f : 0.0f;

		// Damping
		bcs.mLinearDamping = ci.LinearDamping;
		bcs.mAngularDamping = ci.AngularDamping;

		// Mass: Jolt computes mass/inertia from shape if dynamic, but you can override.
		// We'll apply mass override only for dynamic bodies.
		if (ci.Type == ERigidbodyType::Dynamic)
		{
			// Let Jolt compute inertia; then scale to desired mass.
			// This is a simple approximation. If you want exact mass properties, use MassProperties override.
			bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			bcs.mMassPropertiesOverride.mMass = (ci.Mass > 0.0f) ? ci.Mass : 1.0f;
		}

		JPH::BodyInterface& BI = I.BodyIF();

		JPH::Body* pBody = BI.CreateBody(bcs);
		ASSERT(pBody, "Body is null.");

		const JPH::BodyID id = pBody->GetID();

		// Add to world
		BI.AddBody(id, ci.bStartActive ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

		return Impl::MakeBodyHandle(id);
	}

	void Physics::DestroyBody(PhysicsBodyHandle body)
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");
		ASSERT(body.IsValid(), "Body is invalid.");

		Impl& I = *m_pImpl;

		const JPH::BodyID id = Impl::ToBodyID(body);
		ASSERT(!id.IsInvalid(), "Invalid BodyID.");

		JPH::BodyInterface& BI = I.BodyIF();

		// Remove + destroy
		BI.RemoveBody(id);
		BI.DestroyBody(id);
	}

	void Physics::SetBodyTransform(PhysicsBodyHandle body, const float3& pos, const float3& rotEulerRad, bool bActivate)
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");

		Impl& I = *m_pImpl;
		if (!body.IsValid())
			return;

		const JPH::BodyID id = Impl::ToBodyID(body);
		ASSERT(!id.IsInvalid(), "Invalid BodyID.");

		const JPH::Vec3 p = toJPH(pos);
		const JPH::Quat q = quatFromEulerXYZ(rotEulerRad);

		JPH::BodyInterface& BI = I.BodyIF();
		BI.SetPositionAndRotation(
			id,
			p,
			q,
			bActivate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	}

	void Physics::GetBodyTransform(PhysicsBodyHandle body, float3* outPos, float3* outRotEulerRad) const
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");
		ASSERT(body.IsValid(), "Body is invalid.");

		const Impl& I = *m_pImpl;

		const JPH::BodyID id = Impl::ToBodyID(body);
		ASSERT(!id.IsInvalid(), "Invalid BodyID.");

		const JPH::BodyInterface& BI = I.BodyIF();

		if (outPos)
		{
			const JPH::Vec3 p = BI.GetPosition(id);
			*outPos = fromJPH(p);
		}

		if (outRotEulerRad)
		{
			const JPH::Quat q = BI.GetRotation(id);
			*outRotEulerRad = eulerXYZFromQuat(q);
		}
	}

	float3 Physics::GetBodyPosition(PhysicsBodyHandle body) const
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");
		ASSERT(body.IsValid(), "Body is invalid.");

		float3 p = { 0, 0, 0 };

		const Impl& I = *m_pImpl;
		const JPH::BodyID id = Impl::ToBodyID(body);
		ASSERT(!id.IsInvalid(), "Invalid BodyID.");

		const JPH::Vec3 jp = I.BodyIF().GetPosition(id);
		return fromJPH(jp);
	}

	ERigidbodyType Physics::GetBodyMotion(PhysicsBodyHandle body) const
	{
		ASSERT(m_pImpl && m_pImpl->bInitialized, "Physics not initialized.");
		ASSERT(body.IsValid(), "Body is invalid.");

		const Impl& I = *m_pImpl;
		const JPH::BodyID id = Impl::ToBodyID(body);
		ASSERT(!id.IsInvalid(), "Invalid BodyID.");

		const JPH::BodyInterface& BI = I.BodyIF();
		const JPH::EMotionType mt = BI.GetMotionType(id);

		switch (mt)
		{
		case JPH::EMotionType::Static:    return ERigidbodyType::Static;
		case JPH::EMotionType::Dynamic:   return ERigidbodyType::Dynamic;
		case JPH::EMotionType::Kinematic: return ERigidbodyType::Kinematic;
		default: ASSERT(false, "Invalid body motion type.");
		}

		return ERigidbodyType::Static;
	}

	void Physics::ConsumeContactEvents(std::vector<ContactEvent>* outEvents)
	{
		ASSERT(outEvents, "outEvents is null.");
		Impl& I = *m_pImpl;

		std::scoped_lock lock(I.ContactMutex);
		outEvents->swap(I.ContactEvents); // move-out
		I.ContactEvents.clear();
	}

} // namespace shz
