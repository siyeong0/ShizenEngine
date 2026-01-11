/*
 *  Copyright 2024-2025 Diligent Graphics LLC
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

#pragma once

 // \file
 // Defines shz::IAsyncTask and shz::IThreadPool interfaces.

#include "Primitives/Object.h"
#include "Primitives/BasicTypes.h"

namespace shz
{

	// Asynchronous task status
	enum ASYNC_TASK_STATUS : uint32
	{
		// The asynchronous task status is unknown.
		ASYNC_TASK_STATUS_UNKNOWN,

		// The asynchronous task has not been started yet.
		ASYNC_TASK_STATUS_NOT_STARTED,

		// The asynchronous task is running.
		ASYNC_TASK_STATUS_RUNNING,

		// The asynchronous task was cancelled.
		ASYNC_TASK_STATUS_CANCELLED,

		// The asynchronous task is complete.
		ASYNC_TASK_STATUS_COMPLETE
	};


	// {B06D1DDA-AEA0-4CFD-969A-C8E2011DC294}
	static constexpr INTERFACE_ID IID_AsyncTask =
	{ 0xb06d1dda, 0xaea0, 0x4cfd, {0x96, 0x9a, 0xc8, 0xe2, 0x1, 0x1d, 0xc2, 0x94} };

	// Asynchronous task interface
	struct SHZ_INTERFACE IAsyncTask : public IObject
	{
		// Run the asynchronous task.

		// \param [in] ThreadId - Id of the thread that is running this task.
		//
		// Before starting the task, the thread pool sets its
		// status to shz::ASYNC_TASK_STATUS_RUNNING.
		//
		// The method must return one of the following values:
		//   - shz::ASYNC_TASK_STATUS_CANCELLED to indicate that the task was cancelled.
		//   - shz::ASYNC_TASK_STATUS_COMPLETE to indicate that the task is finished successfully.
		//   - shz::ASYNC_TASK_STATUS_NOT_STARTED to request the task to be rescheduled.
		//
		// The thread pool will set the task status to the returned value after
		// the Run() method completes. This way if the GetStatus() method returns
		// any value other than shz::ASYNC_TASK_STATUS_RUNNING, it is guaranteed that the task
		// is not executed by any thread.
		virtual ASYNC_TASK_STATUS Run(uint32 ThreadId) = 0;

		// Cancel the task, if possible.

		// If the task is running, the task implementation should
		// abort the task execution, if possible.
		virtual void Cancel() = 0;

		// Sets the task status, see shz::ASYNC_TASK_STATUS.
		virtual void SetStatus(ASYNC_TASK_STATUS TaskStatus) = 0;

		// Gets the task status, see shz::ASYNC_TASK_STATUS.
		virtual ASYNC_TASK_STATUS GetStatus() const = 0;

		// Sets the task priorirty.
		virtual void SetPriority(float fPriority) = 0;

		// Returns the task priorirty.
		virtual float GetPriority() const = 0;

		// Checks if the task is finished (i.e. cancelled or complete).
		virtual bool IsFinished() const = 0;

		// Waits until the task is complete.

		// \note   This method must not be called from the same thread that is
		//         running the task or a deadlock will occur.
		virtual void WaitForCompletion() const = 0;

		// Waits until the tasks is running.

		// \warning  An application is responsible to make sure that
		//           tasks currently in the queue will eventually finish
		//           allowing the task to start.
		//
		// This method must not be called from the worker thread.
		virtual void WaitUntilRunning() const = 0;
	};


	// {8BB92B5E-3EAB-4CC3-9DA2-5470DBBA7120}
	static constexpr INTERFACE_ID IID_ThreadPool =
	{ 0x8bb92b5e, 0x3eab, 0x4cc3, {0x9d, 0xa2, 0x54, 0x70, 0xdb, 0xba, 0x71, 0x20} };


	// Thread pool interface
	struct SHZ_INTERFACE IThreadPool : public IObject
	{
		// Enqueues asynchronous task for execution.

		// \param[in] pTask            - Task to run.
		// \param[in] ppPrerequisites  - Array of task prerequisites, e.g. the tasks
		//                               that must be completed before this task can start.
		// \param[in] NumPrerequisites - Number of prerequisites.
		//
		// Thread pool will keep a strong reference to the task,
		// so an application is free to release it after enqueuing.
		// 
		// \note       An application must ensure that the task prerequisites are not circular
		//             to avoid deadlocks.
		virtual void EnqueueTask(IAsyncTask* pTask, IAsyncTask** ppPrerequisites = nullptr, uint32       NumPrerequisites = 0) = 0;


		// Reprioritizes the task in the queue.

		// \param[in] pTask - Task to reprioritize.
		//
		// \return     true if the task was found in the queue and was
		//             successfully reprioritized, and false otherwise.
		//
		// When the tasks is enqueued, its priority is used to
		// place it in the priority queue. When an application changes
		// the task priority, it should call this method to update the task
		// position in the queue.
		virtual bool ReprioritizeTask(IAsyncTask* pTask) = 0;


		// Reprioritizes all tasks in the queue.

		// This method should be called if task priorities have changed
		// to update the positions of all tasks in the queue.
		virtual void ReprioritizeAllTasks() = 0;


		// Removes the task from the queue, if possible.

		// \param[in] pTask - Task to remove from the queue.
		//
		// \return    true if the task was successfully removed from the queue,
		//            and false otherwise.
		virtual bool RemoveTask(IAsyncTask* pTask) = 0;


		// Waits until all tasks in the queue are finished.

		// The method blocks the calling thread until all
		// tasks in the quque are finished and the queue is empty.
		// An application is responsible to make sure that all tasks
		// will finish eventually.
		virtual void WaitForAllTasks() = 0;


		// Returns the current queue size.
		virtual uint32 GetQueueSize() = 0;

		// Returns the number of currently running tasks
		virtual uint32 GetRunningTaskCount() const = 0;


		// Stops all worker threads.

		// his method makes all worker threads to exit.
		// If an application enqueues tasks after calling this methods,
		// this tasks will never run.
		virtual void StopThreads() = 0;


		// Manually processes the next task from the queue.

		// \param[in] ThreadId    - Id of the thread that is running this task.
		// \param[in] WaitForTask - whether the function should wait for the next task:
		//                          - if true, the function will block the thread until the next task
		//                            is retrieved from the queue and processed.
		//                          - if false, the function will return immediately if there are no
		//                            tasks in the queue.
		//
		// \return     Whether there are more tasks to process. The calling thread must keep
		//             calling the function until it returns false.
		//
		// This method allows an application to implement its own threading strategy.
		// A thread pool may be created with zero threads, and the application may call
		// ProcessTask() method from its own threads.
		//
		// An application must keep calling the method until it returns false.
		// If there are unhandled tasks in the queue and the application stops processing
		// them, the thread pool will hang up.
		//
		// An example of handling the tasks is shown below:
		//
		//     // Initialization
		//     auto pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
		//
		//     std::vector<std::thread> WorkerThreads(4);
		//     for (uint32 i = 0; i < WorkerThreads.size(); ++i)
		//     {
		//         WorkerThreads[i] = std::thread{
		//             [&ThreadPool = *pThreadPool, i] //
		//             {
		//                 while (ThreadPool.ProcessTask(i, true))
		//                 {
		//                 }
		//             }};
		//     }
		//
		//     // Enqueue async tasks
		//
		//     pThreadPool->WaitForAllTasks();
		//
		//     // Stop all threads in the pool
		//     pThreadPool->StopThreads();
		//
		//     // Cleanup (must be done after all threads are stopped)
		//     for (auto& Thread : WorkerThreads)
		//     {
		//         Thread.join();
		//     }
		//
		virtual bool ProcessTask(uint32 ThreadId, bool WaitForTask) = 0;
	};


} // namespace shz
