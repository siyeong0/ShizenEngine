#include "pch.h"
#include "Engine/ECS/Public/EcsWorld.h"

namespace shz
{
	EcsWorld::EcsWorld() = default;

	EcsWorld::~EcsWorld()
	{
		Shutdown();
	}

	void EcsWorld::Initialize(const CreateInfo& ci)
	{
		ASSERT(m_pWorld == nullptr, "Ecs world already initialized.");

		m_CI = ci;

		// flecs::world may not have a default ctor depending on version/config.
		// Prefer explicit argc/argv ctor for maximum compatibility.
		m_pWorld = std::make_unique<flecs::world>(m_CI.Argc, m_CI.Argv);

		m_DeltaTime = 0.0f;
		m_Accumulator = 0.0f;

		// Optional: configure pipeline/threads here later if you want
		// m_pWorld->set_threads(...);

		// If you want "time" component to be consistent:
		// m_pWorld->set_delta_time(m_CI.FixedDeltaTime); // don't force here; we set per step.
	}

	void EcsWorld::Shutdown()
	{
		if (!m_pWorld)
		{
			return;
		}

		// Ensure all destructors run before shutdown returns
		m_pWorld.reset();

		m_DeltaTime = 0.0f;
		m_Accumulator = 0.0f;
	}

	bool EcsWorld::IsValid() const noexcept
	{
		return (m_pWorld != nullptr);
	}

	void EcsWorld::BeginFrame(float dt)
	{
		ASSERT(m_pWorld, "EcsWorld is not initialized.");

		// Clamp negative dt (can happen during pauses or clock issues)
		if (dt < 0.0f)
		{
			dt = 0.0f;
		}

		m_DeltaTime = dt;

		// Accumulate for fixed-step
		m_Accumulator += dt;
	}

	uint32 EcsWorld::RunFixedSteps()
	{
		ASSERT(m_pWorld, "EcsWorld is not initialized.");

		const float fixedDt = std::max(0.0f, m_CI.FixedDeltaTime);
		ASSERT(fixedDt > 0.0f, "Fixed delta time must bigger than 0.");

		const uint32 maxSteps = std::max(1u, m_CI.MaxFixedStepsPerFrame);

		uint32 steps = 0;
		while (m_Accumulator >= fixedDt && steps < maxSteps)
		{
			// Run one fixed step
			m_pWorld->progress(fixedDt);
			m_Accumulator -= fixedDt;
			++steps;
		}

		// If we hit the cap, drop the remainder to avoid spiral of death
		if (steps == maxSteps)
		{
			m_Accumulator = 0.0f;
		}

		return steps;
	}

	void EcsWorld::Progress()
	{
		ASSERT(m_pWorld, "EcsWorld is not initialized.");

		// Run one "frame step" with variable dt
		m_pWorld->progress(m_DeltaTime);
	}

	void EcsWorld::Tick(float dt)
	{
		BeginFrame(dt);
		RunFixedSteps();
		Progress();
	}

	flecs::world& EcsWorld::World()
	{
		ASSERT(m_pWorld, "EcsWorld is not initialized.");
		return *m_pWorld;
	}

	const flecs::world& EcsWorld::World() const
	{
		ASSERT(m_pWorld, "EcsWorld is not initialized.");
		return *m_pWorld;
	}
} // namespace shz
