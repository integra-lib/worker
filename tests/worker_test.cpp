#include <gtest/gtest.h>

#include <hwlib/execution/worker.hpp>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

using hwlib::execution::Worker;

// A mutex that refuses to be taken twice. Were the worker to hold its lock while a
// piece of work runs, work that posts more work would take it a second time — a
// deadlock with a real non-recursive mutex, an exception here.
class NonRecursiveMutex
{
public:
    void lock()
    {
        if (m_locked)
        {
            throw std::logic_error{"locked twice"};
        }
        m_locked = true;
        ++m_lockCount;
    }

    void unlock()
    {
        m_locked = false;
    }

    [[nodiscard]] int LockCount() const
    {
        return m_lockCount;
    }

private:
    bool m_locked{false};
    int m_lockCount{0};
};

TEST(WorkerTest, RunsNothingUntilUpdated)
{
    Worker<std::mutex> worker;
    bool ran = false;
    worker.Post([&ran] { ran = true; });

    EXPECT_FALSE(ran);
    worker.Update();
    EXPECT_TRUE(ran);
}

TEST(WorkerTest, RunsWorkInTheOrderItWasPosted)
{
    Worker<std::mutex> worker;
    std::vector<int> order;
    for (int i = 0; i < 5; ++i)
    {
        worker.Post([&order, i] { order.push_back(i); });
    }

    worker.Update();
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(WorkerTest, RunsEachPieceOfWorkOnce)
{
    Worker<std::mutex> worker;
    int runs = 0;
    worker.Post([&runs] { ++runs; });

    worker.Update();
    worker.Update();
    EXPECT_EQ(runs, 1);
}

TEST(WorkerTest, RunsWorkPostedByWorkInTheSameUpdate)
{
    Worker<NonRecursiveMutex> worker;
    std::vector<std::string> order;
    worker.Post([&] {
        order.emplace_back("first");
        worker.Post([&] { order.emplace_back("posted by first"); });
    });
    worker.Post([&] { order.emplace_back("second"); });

    // Throws if the lock were held while "first" runs.
    worker.Update();
    EXPECT_EQ(order, (std::vector<std::string>{"first", "second", "posted by first"}));
}

TEST(WorkerTest, DropsAnEmptyCallable)
{
    Worker<NonRecursiveMutex> worker;
    worker.Post(nullptr);
    worker.Post(Worker<NonRecursiveMutex>::Work{});

    // Nothing to run, and nothing to call through an empty std::function.
    EXPECT_NO_THROW(worker.Update());
}

TEST(WorkerTest, MovesTheWorkInsteadOfCopyingIt)
{
    // The original took `const Work&&`, so std::move in it produced a const rvalue
    // and every post copied the callable, captures and all. A capture that counts
    // its copies proves this one does not.
    struct CopyCounter
    {
        std::shared_ptr<int> copies = std::make_shared<int>(0);
        CopyCounter()               = default;

        CopyCounter(const CopyCounter& other)
            : copies{other.copies}
        {
            ++*copies;
        }

        CopyCounter(CopyCounter&&) noexcept            = default;
        CopyCounter& operator=(const CopyCounter&)     = delete;
        CopyCounter& operator=(CopyCounter&&) noexcept = default;
        ~CopyCounter()                                 = default;
    };

    Worker<std::mutex> worker;
    CopyCounter counter;
    const auto copies = counter.copies;
    Worker<std::mutex>::Work work{[captured = std::move(counter)] { (void)captured; }};
    const int copiesBeforePost = *copies;

    worker.Post(std::move(work));
    worker.Update();
    EXPECT_EQ(*copies, copiesBeforePost);
}

} // namespace
