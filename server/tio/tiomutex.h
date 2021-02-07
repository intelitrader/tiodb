#pragma once
#include "pch.h"

namespace tio
{
	class spin_lock
	{
	public:
		void lock()
		{
			while (lck.test_and_set(std::memory_order_acquire))
			{}
		}

		void unlock()
		{
			lck.clear(std::memory_order_release);
		}

	private:
		std::atomic_flag lck = ATOMIC_FLAG_INIT;
	};

    class tio_recursive_mutex
    {
        static bool single_threaded;
        typedef std::recursive_mutex inner_mutex;

        std::shared_ptr<inner_mutex> mutex_;

public:
        static void set_single_threaded()
        {
            single_threaded = true;
        }

        tio_recursive_mutex()
        {
            if(single_threaded)
            {
                assert(single_threaded);
                return;
            }
            
            mutex_ = std::make_shared<inner_mutex>();
        }

        void lock()
        {
            if(!mutex_)
            {
                assert(single_threaded);
                return;
            }

            mutex_->lock();
        }

        void unlock()
        {
            if(!mutex_)
            {
                assert(single_threaded);
                return;
            }

            mutex_->unlock();
        }
    };

    class tio_lock_guard
    {
        tio_recursive_mutex mutex_;
    public:    
        tio_lock_guard(tio_recursive_mutex& mutex)
            : mutex_(mutex)
        {
            mutex_.lock();
        }

        ~tio_lock_guard()
        {
            mutex_.unlock();
        }
    };
}