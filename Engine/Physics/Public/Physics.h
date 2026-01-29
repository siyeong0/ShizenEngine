#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

#include "Engine/Physics/Public/PhysicsBodyHandle.h"
#include "Engine/Physics/Public/PhysicsEvent.h"

namespace shz
{
	enum class EPhysicsObjectLayer : uint8
	{
		NonMoving = 0,
		Moving = 1,
		Count
	};

	enum class ERigidbodyType : uint8
	{
		Static = 0,
		Dynamic,
		Kinematic,
	};

	class Physics final
	{
	public:
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

		struct BodyCreateInfo final
		{
			PhysicsShapeHandle Shape = {};
			float3 Position = { 0, 0, 0 };
			float3 RotationEulerRad = { 0, 0, 0 };

			ERigidbodyType Type = ERigidbodyType::Static;
			EPhysicsObjectLayer Layer = EPhysicsObjectLayer::NonMoving;

			float Mass = 1.0f;
			float LinearDamping = 0.0f;
			float AngularDamping = 0.0f;
			bool bAllowSleeping = true;
			bool bEnableGravity = true;
			bool bIsSensor = false;
			bool bStartActive = true;
		};

		struct HeightFieldCreateInfo final
		{
			const float* pHeights = nullptr;
			uint32 Width = 0;
			uint32 Height = 0;

			float CellSizeX = 1.0f;
			float CellSizeZ = 1.0f;
			float HeightScale = 1.0f;
			float HeightOffset = 0.0f;
		};

	public:
		Physics();
		~Physics();

		Physics(const Physics&) = delete;
		Physics& operator=(const Physics&) = delete;

		bool Initialize(const CreateInfo& ci);
		void Shutdown();

		void Step(float dt);

		PhysicsShapeHandle CreateBoxShape(const float3& halfExtent);
		PhysicsShapeHandle CreateSphereShape(float radius);
		PhysicsShapeHandle CreateHeightFieldShape(const HeightFieldCreateInfo& ci);
		void ReleaseShape(PhysicsShapeHandle shape);

		PhysicsBodyHandle CreateBody(const BodyCreateInfo& ci);
		void DestroyBody(PhysicsBodyHandle body);

		void SetBodyTransform(PhysicsBodyHandle body, const float3& pos, const float3& rotEulerRad, bool bActivate);
		void GetBodyTransform(PhysicsBodyHandle body, float3* outPos, float3* outRotEulerRad) const;

		float3 GetBodyPosition(PhysicsBodyHandle body) const;

		ERigidbodyType GetBodyMotion(PhysicsBodyHandle body) const;

		// Move-out/Consume
		void ConsumeContactEvents(std::vector<ContactEvent>* outEvents);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_pImpl;
	};
}
