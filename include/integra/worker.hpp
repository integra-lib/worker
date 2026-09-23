#pragma once

#include <concepts>
#include <functional>
#include <mutex>
#include <queue>
#include <utility>

namespace integra
{

/// Anything std::scoped_lock can hold. On a host that is std::mutex; on an RTOS it
/// is a ten-line wrapper around the kernel's mutex — which is also where the kernel
/// object gets initialised, so the worker needs no initialisation hook of its own.
template<typename Mutex>
concept BasicLockable = requires(Mutex& mutex) {
    mutex.lock();
    mutex.unlock();
};

/// A queue of deferred work: any context posts a callable, and the one context that
/// owns the worker runs everything posted so far when it calls `Update()`.
///
/// The lock is held only while the queue itself is touched, never while a piece of
/// work runs. That is what lets work post more work — and what makes the worker
/// usable with a mutex that is not recursive, which a kernel mutex usually is not.
///
/// The queue grows on the heap, one `std::function` per posted item. A producer that
/// outpaces `Update()` grows it without bound.
template<BasicLockable Mutex = std::mutex>
class Worker
{
public:
    using Work = std::function<void()>;

    Worker() = default;

    Worker(const Worker&)            = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&)                 = delete;
    Worker& operator=(Worker&&)      = delete;
    ~Worker()                        = default;

    /// Queues `work` to run on the next `Update()`. An empty callable is dropped
    /// here rather than skipped later, so the queue only ever holds something to run.
    void Post(Work work)
    {
        if (!work)
        {
            return;
        }
        const std::scoped_lock lock{m_mutex};
        m_works.push(std::move(work));
    }

    /// Runs everything posted so far, in the order it was posted — including work
    /// posted by the work being run, which joins the end of the same drain.
    void Update()
    {
        while (true)
        {
            Work work;
            {
                const std::scoped_lock lock{m_mutex};
                if (m_works.empty())
                {
                    return;
                }
                work = std::move(m_works.front());
                m_works.pop();
            }
            work();
        }
    }

private:
    Mutex m_mutex;
    std::queue<Work> m_works;
};

} // namespace integra
