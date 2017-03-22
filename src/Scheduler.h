#pragma once

#include <thread>
#include <future>
#include <chrono>
#include <vector>
#include <list>
#include <mutex>
#include <condition_variable>
#include <climits>

#define SCHEDULER_INFINITE ULONG_MAX
#define SCHEDULER_INTERVAL_HOUR 1000 * 60 * 60
#define SCHEDULER_INTERVAL_5MIN 1000 * 60 * 5

namespace micasa {

	// =========
	// Scheduler
	// =========

	class Scheduler final {
	
	public:

		// ========
		// BaseTask
		// ========

		class BaseTask : public std::enable_shared_from_this<BaseTask> {
		
		public:
			typedef std::function<bool(BaseTask&)> t_compareFunc;

			std::chrono::system_clock::time_point time;
			unsigned long delay;
			unsigned long repeat;
			unsigned long iteration;
			void* data;

			BaseTask( std::chrono::system_clock::time_point time_, unsigned long delay_, unsigned long repeat_, void* data_ ) :
				time( time_ ),
				delay( delay_ ),
				repeat( repeat_ ),
				iteration( 0 ),
				data( data_ )
			{
			};
			virtual ~BaseTask() { };

			virtual void execute() = 0;
			virtual void complete() = 0;
			void proceed( unsigned long wait_ );
			void advance( unsigned long duration_ );

		}; // class BaseTask

		// ====
		// Task
		// ====

		template<typename T = bool> class Task : public BaseTask {

		public:
			typedef std::function<T(Task<T>&)> t_taskFunc;

			Task( t_taskFunc&& func_, std::chrono::system_clock::time_point time_, unsigned long delay_, unsigned long repeat_, void* data_ ) :
				BaseTask( time_, delay_, repeat_, data_ ),
				m_func( std::move( func_ ) ),
				m_first( true )
			{
				// Retrieving the first result for the task is blocking. All subsequent calls to retrieve the result are
				// instant, returning the last generated value.
				this->m_resultMutex.lock();
			};
			~Task() { };

			void execute() {
				if ( ! this->m_first ) {
					this->m_resultMutex.lock();
				}
				this->m_result = this->m_func( *this );
				this->m_first = false;
				this->m_resultMutex.unlock();
			};

			void complete() {
				this->wait();
			};

			T wait() const {
				std::lock_guard<std::timed_mutex> lock( this->m_resultMutex );
				return this->m_result;
			};

			bool waitFor( unsigned long wait_ ) const {
				return this->m_resultMutex.try_lock_for( std::chrono::milliseconds( wait_ ) );
			};

		private:
			t_taskFunc m_func;
			mutable std::timed_mutex m_resultMutex;
			T m_result;
			bool m_first;

		}; // class Task

		Scheduler() { };
		~Scheduler();

		template<typename V = bool> std::shared_ptr<Task<V> > schedule( unsigned long delay_, unsigned long repeat_, void* data_, typename Task<V>::t_taskFunc&& func_ ) {
			std::shared_ptr<Task<V> > task = std::make_shared<Task<V> >( std::move( func_ ), std::chrono::system_clock::now() + std::chrono::milliseconds( delay_ ), delay_, repeat_, data_ );
			Scheduler::ThreadPool::get().schedule( this, std::static_pointer_cast<BaseTask>( task ) );
			return task;
		};

		template<typename V = bool> std::shared_ptr<Task<V> > schedule( unsigned long delay_, unsigned long repeat_, std::shared_ptr<Task<V> > task_ ) {
			task_->delay = delay_;
			task_->repeat = repeat_;
			task_->time = std::chrono::system_clock::now() + std::chrono::milliseconds( delay_ );
			Scheduler::ThreadPool::get().schedule( this, task_ );
			return task_;
		};

		template<typename V = bool> std::shared_ptr<Task<V> > schedule( std::chrono::system_clock::time_point time_, unsigned long delay_, unsigned long repeat_, void* data_, typename Task<V>::t_taskFunc&& func_ ) {
			std::shared_ptr<Task<V> > task = std::make_shared<Task<V> >( std::move( func_ ), time_, delay_, repeat_, data_ );
			Scheduler::ThreadPool::get().schedule( this, task );
			return task;
		};

		template<typename V = bool> std::shared_ptr<Task<V> > schedule( std::chrono::system_clock::time_point time_, unsigned long delay_, unsigned long repeat_, std::shared_ptr<Task<V> > task_ ) {
			task_->delay = delay_;
			task_->repeat = repeat_;
			task_->time = time_;
			Scheduler::ThreadPool::get().schedule( this, task_ );
			return task_;
		};

		void erase( BaseTask::t_compareFunc&& func_ );
		std::shared_ptr<BaseTask> first( BaseTask::t_compareFunc&& func_ ) const;
		void notify();

	private:

		// ==========
		// ThreadPool
		// ==========

		class ThreadPool final {

		public:
			typedef std::pair<Scheduler*, std::shared_ptr<BaseTask> > t_scheduledTask;

			ThreadPool( const ThreadPool& ) = delete; // Do not copy!
			ThreadPool& operator=( const ThreadPool& ) = delete; // Do not copy-assign!

			void schedule( Scheduler* scheduler_, std::shared_ptr<BaseTask> task_ );
			void reschedule( std::shared_ptr<BaseTask> task_ );
			void erase( Scheduler* scheduler_, BaseTask::t_compareFunc&& func_ = []( BaseTask& ) -> bool { return true; } );
			std::shared_ptr<BaseTask> first( const Scheduler* scheduler_, BaseTask::t_compareFunc&& func_ = []( BaseTask& ) -> bool { return true; } ) const;
			void notify();

			static ThreadPool& get() {
				// In c++11 static initialization is supposed to be thread-safe.
				static ThreadPool instance;
				return instance;
			}

		private:
			bool m_shutdown;
			bool m_continue;

			// Every mutation to m_tasks, the activeTask map and the Task objects therein, including adding a promise,
			// should be done while holding a lock on the taskMutex.
			std::list<t_scheduledTask> m_tasks;
			std::vector<t_scheduledTask> m_activeTasks;
			mutable std::mutex m_tasksMutex;

			std::vector<std::thread> m_threads;
			std::condition_variable m_continueCondition;
			mutable std::mutex m_conditionMutex;

			ThreadPool(); // private constructor
			~ThreadPool(); // private destructor

			void _loop( unsigned int index_ );
			void _insert( Scheduler* scheduler_, std::shared_ptr<BaseTask> task_ );
			void _notify( bool all_, std::function<void()>&& func_ );

		}; // class ThreadPool

	}; // class Scheduler

}; // namespace micasa
