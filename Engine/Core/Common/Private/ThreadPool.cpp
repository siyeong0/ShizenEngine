/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "pch.h"
#include "Platforms/Common/PlatformMisc.hpp"
#include "Engine/Core/Common/Public/Errors.hpp"
#include "Engine/Core/Common/Public/ThreadPool.hpp"

namespace shz
{

	AsyncTaskBase::~AsyncTaskBase()
	{
	}

	class ThreadPoolImpl final : public ObjectBase<IThreadPool>
	{
	public:
		using TBase = ObjectBase<IThreadPool>;

		ThreadPoolImpl(IReferenceCounters* pRefCounters, const ThreadPoolCreateInfo& PoolCI)
			:TBase{ pRefCounters }
		{
			m_WorkerThreads.reserve(PoolCI.NumThreads);
			for (uint32 i = 0; i < PoolCI.NumThreads; ++i)
			{
				m_WorkerThreads.emplace_back(
					[this, PoolCI, i] //
					{
						const std::string ThreadName = "DG:TPW " + std::to_string(i);
						PlatformMisc::SetCurrentThreadName(ThreadName.c_str());

						if (PoolCI.OnThreadStarted)
							PoolCI.OnThreadStarted(i);

						while (ProcessTask(i, /*WaitForTask =*/true))
						{
						}

						if (PoolCI.OnThreadExiting)
							PoolCI.OnThreadExiting(i);
					});
			}
		}

		IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_ThreadPool, TBase);

		virtual bool SHZ_CALL_TYPE ProcessTask(uint32 ThreadId, bool WaitForTask) override final
		{
			QueuedTaskInfo TaskInfo;
			{
				std::unique_lock<std::mutex> lock{ m_TasksQueueMtx };
				if (WaitForTask)
				{
					// The effects of notify_one()/notify_all() and each of the three atomic parts of
					// wait()/wait_for()/wait_until() (unlock+wait, wakeup, and lock) take place in a
					// single total order that can be viewed as modification order of an atomic variable:
					// the order is specific to this individual condition variable. This makes it impossible
					// for notify_one() to, for example, be delayed and unblock a thread that started waiting
					// just after the call to notify_one() was made.
					m_NextTaskCond.wait(lock,
						[this] //
						{
							return m_Stop.load() || !m_TasksQueue.empty();
						} //
					);
				}

				// m_Stop must be accessed under the mutex
				if (m_Stop.load() && m_TasksQueue.empty())
					return false;

				if (!m_TasksQueue.empty())
				{
					auto front = m_TasksQueue.begin();
					TaskInfo = std::move(front->second);
					// NB: we must increment the running task counter while holding the lock and
					//     before removing the task from the queue, otherwise WaitForAllTasks() may
					//     miss the task.
					m_NumRunningTasks.fetch_add(1);
					m_TasksQueue.erase(front);
				}
			}

			if (TaskInfo.pTask)
			{
				// Check prerequisites
				bool  PrerequisitesMet = true;
				float MinPrereqPriority = std::numeric_limits<float32>::max();
				for (auto& pPrereq : TaskInfo.Prerequisites)
				{
					if (auto pPrereqTask = pPrereq.Lock())
					{
						if (!pPrereqTask->IsFinished())
						{
							PrerequisitesMet = false;
							MinPrereqPriority = std::min(MinPrereqPriority, pPrereqTask->GetPriority());
						}
					}
				}

				bool TaskFinished = false;
				if (PrerequisitesMet)
				{
					TaskInfo.pTask->SetStatus(ASYNC_TASK_STATUS_RUNNING);
					ASYNC_TASK_STATUS ReturnStatus = TaskInfo.pTask->Run(ThreadId);
					// NB: It is essential to set the task status after the Run() method returns.
					//     This way if the GetStatus() method returns any value other than ASYNC_TASK_STATUS_RUNNING,
					//     it is guaranteed that the task is not executed by any thread.
					TaskInfo.pTask->SetStatus(ReturnStatus);
					TaskFinished = TaskInfo.pTask->IsFinished();
					ASSERT((TaskFinished || TaskInfo.pTask->GetStatus() == ASYNC_TASK_STATUS_NOT_STARTED),
						"Finished tasks must be in COMPLETE, CANCELLED or NOT_STARTED state");
				}

				{
					std::unique_lock<std::mutex> lock{ m_TasksQueueMtx };

					const int NumRunningTasks = m_NumRunningTasks.fetch_add(-1) - 1;

					if (TaskFinished)
					{
						if (m_TasksQueue.empty() && NumRunningTasks == 0)
						{
							m_TasksFinishedCond.notify_one();
						}
					}
					else
					{
						// If prerequisites are not met or the task requested to be re-run,
						// re-enqueue the task with the minimum prerequisite priority
						if (TaskInfo.pTask->GetPriority() > MinPrereqPriority)
							TaskInfo.pTask->SetPriority(MinPrereqPriority);
						m_TasksQueue.emplace(TaskInfo.pTask->GetPriority(), std::move(TaskInfo));
					}
				}

				if (!TaskFinished)
				{
					m_NextTaskCond.notify_one();
				}
			}

			return true;
		}

		virtual void SHZ_CALL_TYPE EnqueueTask(IAsyncTask* pTask,
			IAsyncTask** ppPrerequisites,
			uint32       NumPrerequisites) override final
		{
			ASSERT_EXPR(pTask != nullptr);
			if (pTask == nullptr)
				return;

			{
				std::unique_lock<std::mutex> lock{ m_TasksQueueMtx };
				ASSERT(!m_Stop, "Enqueue on a stopped ThreadPool");

				QueuedTaskInfo TaskInfo;
				TaskInfo.pTask = pTask;
				if (ppPrerequisites != nullptr && NumPrerequisites > 0)
				{
					TaskInfo.Prerequisites.reserve(NumPrerequisites);
					float MinPrereqPriority = std::numeric_limits<float32>::max();
					for (uint32 i = 0; i < NumPrerequisites; ++i)
					{
						if (ppPrerequisites[i] != nullptr)
						{
							TaskInfo.Prerequisites.emplace_back(ppPrerequisites[i]);
							MinPrereqPriority = std::min(MinPrereqPriority, ppPrerequisites[i]->GetPriority());
						}
					}
					if (pTask->GetPriority() > MinPrereqPriority)
					{
						TaskInfo.pTask->SetPriority(MinPrereqPriority);
					}
				}

				m_TasksQueue.emplace(pTask->GetPriority(), std::move(TaskInfo));
			}
			m_NextTaskCond.notify_one();
		}

		virtual void SHZ_CALL_TYPE WaitForAllTasks() override final
		{
			std::unique_lock<std::mutex> lock{ m_TasksQueueMtx };
			if (!m_TasksQueue.empty() || m_NumRunningTasks.load() > 0)
			{
				m_TasksFinishedCond.wait(lock,
					[this] //
					{
						return m_TasksQueue.empty() && m_NumRunningTasks.load() == 0;
					} //
				);
			}
		}

		virtual void SHZ_CALL_TYPE StopThreads() override final
		{
			{
				std::unique_lock<std::mutex> lock{ m_TasksQueueMtx };
				// NB: even if the shared variable is atomic, it must be modified under the mutex
				//     in order to correctly publish the modification to the waiting thread.
				m_Stop.store(true);
			}
			// Note that if there are outstanding tasks in the queue, the threads may be woken up
			// by the corresponding notify_one() as notify*() and wait*() take place in a single
			// total order.
			m_NextTaskCond.notify_all();
			for (std::thread& worker : m_WorkerThreads)
				worker.join();

			m_WorkerThreads.clear();
		}

		virtual bool SHZ_CALL_TYPE RemoveTask(IAsyncTask* pTask) override final
		{
			std::unique_lock<std::mutex> lock{ m_TasksQueueMtx };

			auto it = m_TasksQueue.begin();
			while (it != m_TasksQueue.end() && it->second.pTask != pTask)
				++it;
			if (it != m_TasksQueue.end())
			{
				m_TasksQueue.erase(it);
				return true;
			}

			return false;
		}

		virtual bool SHZ_CALL_TYPE ReprioritizeTask(IAsyncTask* pTask) override final
		{
			const float Priority = pTask->GetPriority();

			std::unique_lock<std::mutex> lock{ m_TasksQueueMtx };

			auto it = m_TasksQueue.begin();
			while (it != m_TasksQueue.end() && it->second.pTask != pTask)
				++it;
			if (it != m_TasksQueue.end())
			{
				if (it->first != Priority)
				{
					QueuedTaskInfo ExistingTaskInfo = std::move(it->second);
					m_TasksQueue.erase(it);
					m_TasksQueue.emplace(Priority, std::move(ExistingTaskInfo));
				}

				return true;
			}
			return false;
		}

		virtual void SHZ_CALL_TYPE ReprioritizeAllTasks() override final
		{
			std::unique_lock<std::mutex> lock{ m_TasksQueueMtx };

			m_ReprioritizationList.clear();
			for (auto it = m_TasksQueue.begin(); it != m_TasksQueue.end();)
			{
				QueuedTaskInfo& TaskInfo = it->second;
				float           Priority = TaskInfo.pTask->GetPriority();
				if (it->first != Priority)
				{
					m_ReprioritizationList.emplace_back(Priority, std::move(TaskInfo));
					it = m_TasksQueue.erase(it);
				}
				else
				{
					++it;
				}
			}

			for (auto& it : m_ReprioritizationList)
			{
				m_TasksQueue.emplace(it.first, std::move(it.second));
			}

			m_ReprioritizationList.clear();
		}

		uint32 SHZ_CALL_TYPE GetQueueSize() override final
		{
			std::unique_lock<std::mutex> lock{ m_TasksQueueMtx };
			return StaticCast<uint32>(m_TasksQueue.size());
		}

		virtual uint32 SHZ_CALL_TYPE GetRunningTaskCount() const override final
		{
			return m_NumRunningTasks.load();
		}

		~ThreadPoolImpl()
		{
			StopThreads();
			ASSERT_EXPR(m_TasksQueue.empty());
			ASSERT_EXPR(m_NumRunningTasks.load() == 0);
		}

	private:
		std::vector<std::thread> m_WorkerThreads;

		struct QueuedTaskInfo
		{
			RefCntAutoPtr<IAsyncTask>              pTask;
			std::vector<RefCntWeakPtr<IAsyncTask>> Prerequisites;
		};
		// Priority queue
		std::mutex                                                m_TasksQueueMtx;
		std::multimap<float, QueuedTaskInfo, std::greater<float>> m_TasksQueue;

		std::vector<std::pair<float, QueuedTaskInfo>> m_ReprioritizationList;

		std::condition_variable m_NextTaskCond{};
		std::condition_variable m_TasksFinishedCond{};
		std::atomic<bool>       m_Stop{ false };

		std::atomic<int> m_NumRunningTasks{ 0 };
	};

	RefCntAutoPtr<IThreadPool> CreateThreadPool(const ThreadPoolCreateInfo& ThreadPoolCI)
	{
		return RefCntAutoPtr<ThreadPoolImpl>{MakeNewRCObj<ThreadPoolImpl>()(ThreadPoolCI)};
	}

	uint64 PinWorkerThread(uint32 ThreadId, uint64 AllowedCoresMask)
	{
		if (AllowedCoresMask == 0)
		{
			return 0;
		}

		uint64 NumCores = std::thread::hardware_concurrency();
		if (NumCores <= 1)
			return 0;

		uint64 AffinityMask = AllowedCoresMask;
		if (NumCores < 64)
			AffinityMask &= (uint64{ 1 } << NumCores) - uint64{ 1 };

		if (AffinityMask == 0)
		{
			LOG_WARNING_MESSAGE("Allowed cores mask (0x", std::hex, AllowedCoresMask, ") does not set any bits corresponding to ", std::dec, NumCores, " available cores");
			return 0;
		}

		const uint32 NumAllowedCores = PlatformMisc::CountOneBits(AffinityMask);
		const uint32 CoreBitInd = ThreadId % NumAllowedCores;

		for (uint32 bit = 0; bit < CoreBitInd; ++bit)
		{
			ASSERT_EXPR(AffinityMask != 0);
			uint64 LSB = PlatformMisc::GetLSB(AffinityMask);
			AffinityMask &= ~(uint64{ 1 } << LSB);
		}

		ASSERT_EXPR(AffinityMask != 0);
		uint32 WorkerCore = PlatformMisc::GetLSB(AffinityMask);
		ASSERT_EXPR(WorkerCore < NumCores);
		uint64 PrevMask = PlatformMisc::SetCurrentThreadAffinity(uint64{ 1 } << WorkerCore) != 0;
		if (PrevMask == 0)
		{
			LOG_WARNING_MESSAGE("Failed to pin worker thread ", ThreadId, " to core ", WorkerCore);
		}

		return PrevMask;
	}

} // namespace shz
