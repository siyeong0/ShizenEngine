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

		m_pWorld = std::make_unique<flecs::world>(m_CI.Argc, m_CI.Argv);

		m_DeltaTime = 0.0f;
		m_Accumulator = 0.0f;

		m_FixedSystems.clear();
		m_UpdateSystems.clear();
	}

	void EcsWorld::Shutdown()
	{
		if (!m_pWorld)
			return;

		m_FixedSystems.clear();
		m_UpdateSystems.clear();

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

		if (dt < 0.0f)
			dt = 0.0f;

		m_DeltaTime = dt;
		m_Accumulator += dt;
	}

	void EcsWorld::RegisterFixedSystem(const flecs::entity& sys)
	{
		ASSERT(m_pWorld, "EcsWorld is not initialized.");
		ASSERT(sys.is_valid(), "RegisterFixedSystem: sys is invalid.");

		m_FixedSystems.push_back(sys);

		// Default: fixed systems are disabled during variable update
		sys.disable();
	}

	void EcsWorld::RegisterUpdateSystem(const flecs::entity& sys)
	{
		ASSERT(m_pWorld, "EcsWorld is not initialized.");
		ASSERT(sys.is_valid(), "RegisterUpdateSystem: sys is invalid.");

		m_UpdateSystems.push_back(sys);

		// Default: update systems enabled
		sys.enable();
	}

	void EcsWorld::SetFixedEnabled(bool bEnabled)
	{
		for (auto& s : m_FixedSystems)
		{
			if (!s.is_valid())
				continue;
			if (bEnabled) s.enable();
			else s.disable();
		}
	}

	void EcsWorld::SetUpdateEnabled(bool bEnabled)
	{
		for (auto& s : m_UpdateSystems)
		{
			if (!s.is_valid())
				continue;
			if (bEnabled) s.enable();
			else s.disable();
		}
	}

	uint32 EcsWorld::RunFixedSteps()
	{
		ASSERT(m_pWorld, "EcsWorld is not initialized.");

		const float fixedDt = std::max(0.0f, m_CI.FixedDeltaTime);
		ASSERT(fixedDt > 0.0f, "Fixed delta time must bigger than 0.");

		const uint32 maxSteps = std::max(1u, m_CI.MaxFixedStepsPerFrame);

		uint32 steps = 0;

		// Only fixed systems run
		SetUpdateEnabled(false);
		SetFixedEnabled(true);

		while (m_Accumulator >= fixedDt && steps < maxSteps)
		{
			m_pWorld->progress(fixedDt);
			m_Accumulator -= fixedDt;
			++steps;
		}

		// If we hit max steps, drop the remainder to avoid spiral of death.
		if (steps == maxSteps)
		{
			m_Accumulator = 0.0f;
		}

		// Turn fixed systems off again (so Progress() won't accidentally run them)
		SetFixedEnabled(false);
		SetUpdateEnabled(true);

		return steps;
	}

	void EcsWorld::Progress()
	{
		ASSERT(m_pWorld, "EcsWorld is not initialized.");

		// Only update systems run
		SetFixedEnabled(false);
		SetUpdateEnabled(true);

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
