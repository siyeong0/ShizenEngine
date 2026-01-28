#pragma once
#include "Primitives/BasicTypes.h"

#include <flecs.h>

namespace shz
{
	// ------------------------------------------------------------
	// EcsWorld
	//  - Owns a flecs::world instance
	//  - Provides frame stepping helpers (variable dt + fixed dt)
	//  - Keeps lifetime out of App/Sample code
	// ------------------------------------------------------------
	class EcsWorld final
	{
	public:
		struct CreateInfo final
		{
			// Fixed-step simulation helper
			float FixedDeltaTime = 1.0f / 60.0f;

			// Prevent spiral of death when frame time spikes
			uint32 MaxFixedStepsPerFrame = 8;

			// Optional: if you want to pass app args to flecs for built-in features
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

		// Call once per frame
		void BeginFrame(float dt);

		// Run fixed-step updates (0..N times based on accumulator)
		// Returns how many fixed steps executed.
		uint32 RunFixedSteps();

		// Run normal variable dt systems (typically once per frame)
		void Progress();

		// Convenience: BeginFrame + fixed + variable progress
		void Tick(float dt);

		flecs::world& World();
		const flecs::world& World() const;

		float GetDeltaTime() const noexcept { return m_DeltaTime; }
		float GetFixedDeltaTime() const noexcept { return m_CI.FixedDeltaTime; }

	private:
		CreateInfo m_CI = {};

		std::unique_ptr<flecs::world> m_pWorld = nullptr;

		float m_DeltaTime = 0.0f;
		float m_Accumulator = 0.0f;
	};
} // namespace shz
