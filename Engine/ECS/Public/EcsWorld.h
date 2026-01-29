#pragma once
#include "Primitives/BasicTypes.h"

#include <memory>
#include <vector>

#include <flecs.h>

namespace shz
{
	// Tag types (optional: purely for readability)
	struct EcsPhaseFixed final {};
	struct EcsPhaseUpdate final {};

	class EcsWorld final
	{
	public:
		struct CreateInfo final
		{
			float FixedDeltaTime = 1.0f / 60.0f;
			uint32 MaxFixedStepsPerFrame = 8;

			int Argc = 0;
			char** Argv = nullptr;
		};

	public:
		EcsWorld();
		~EcsWorld();

		EcsWorld(const EcsWorld&) = delete;
		EcsWorld& operator=(const EcsWorld&) = delete;

	public:
		void Initialize(const CreateInfo& ci = {});
		void Shutdown();

		bool IsValid() const noexcept;

		// Frame driving
		void BeginFrame(float dt);
		uint32 RunFixedSteps();
		void Progress();
		void Tick(float dt);

		// Access
		flecs::world& World();
		const flecs::world& World() const;

		float GetDeltaTime() const noexcept { return m_DeltaTime; }
		float GetFixedDeltaTime() const noexcept { return m_CI.FixedDeltaTime; }

		void RegisterFixedSystem(const flecs::entity& system);
		void RegisterUpdateSystem(const flecs::entity& system);

	private:
		void SetFixedEnabled(bool bEnabled);
		void SetUpdateEnabled(bool bEnabled);

	private:
		CreateInfo m_CI = {};

		std::unique_ptr<flecs::world> m_pWorld = nullptr;

		// We avoid pipeline addon. We just enable/disable system entities.
		std::vector<flecs::entity> m_FixedSystems;
		std::vector<flecs::entity> m_UpdateSystems;

		float m_DeltaTime = 0.0f;
		float m_Accumulator = 0.0f;
	};
} // namespace shz
