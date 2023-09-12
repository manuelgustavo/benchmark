#include <benchmark/benchmark.h>

//#include <benchmark/benchmark.h>
#include <thread>
#include <ctime>
#include <cstdlib>
#include <atomic>
#include <mutex>

using namespace std;

constexpr auto max_items = 500000;

inline void idle() noexcept
{
#ifdef _WIN32
    _mm_pause();
#else
    __builtin_ia32_pause(); // Note: also acts as a compiler memory barrier.
#endif
}

class spin_lock
{
    std::atomic<bool> lock_ = {false};

public:
    bool try_lock() noexcept { return !lock_.exchange(true, std::memory_order_acquire); }

    void lock() noexcept
    {
        for (;;)
        {
            if (try_lock())
            {
                break;
            }
            while (lock_.load(std::memory_order_relaxed))
            {
                idle();
            }
        }
    }

    void unlock() noexcept { lock_.store(false, std::memory_order_release); }
};

class scoped_spin_lock final
{
    spin_lock& lock_;

public:
    scoped_spin_lock(spin_lock& l) noexcept
        : lock_(l)
    {
        lock_.lock();
    }

    scoped_spin_lock(const scoped_spin_lock&) = delete;
    scoped_spin_lock& operator=(const scoped_spin_lock&) = delete;

    ~scoped_spin_lock() noexcept { lock_.unlock(); }
};

template<typename T, std::size_t Size> class spsc_queue
{
public:
    spsc_queue()
        : head_(0)
        , tail_(0)
    {
    }

    bool push(const T& value)
    {
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t next_head = next(head);
        if (next_head == tail_.load(std::memory_order_acquire))
            return false;
        ring_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    bool pop(T& value)
    {
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        // make sure we don't keep a copy of the objects in the ring, which may take
        // up memory (for instance if the objects have big buffers), and more
        // importantly may contain shared_ptr that could prevent timely deletion.
        value = std::move(ring_[tail]);
        tail_.store(next(tail), std::memory_order_release);
        return true;
    }

private:
    std::size_t next(std::size_t current) { return (current + 1) % Size; }
    T ring_[Size];
    std::atomic<std::size_t> head_, tail_;
};

template<typename T, std::size_t Size> class spsc_queue_plus_one
{
public:
    spsc_queue_plus_one()
        : head_(0)
        , tail_(0)
    {
    }

    bool push(const T& value)
    {
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t next_head = next(head);
        if (next_head == tail_.load(std::memory_order_acquire))
            return false;
        ring_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    bool pop(T& value)
    {
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        // make sure we don't keep a copy of the objects in the ring, which may take up memory
        // (for instance if the objects have big buffers), and more importantly may contain
        // shared_ptr that could prevent timely deletion.
        value = std::move(ring_[tail]);
        tail_.store(next(tail), std::memory_order_release);
        return true;
    }

private:
    std::size_t next(std::size_t current) { return (current + 1) % (Size + 1); }
    T ring_[Size + 1];
    std::atomic<std::size_t> head_, tail_;
};

template<typename T, std::size_t Size> class spsc_queue_no_false_sharing
{
public:
    spsc_queue_no_false_sharing()
        : head_(0)
        , tail_(0)
    {
    }

    bool push(const T& value)
    {
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t next_head = next(head);
        if (next_head == tail_.load(std::memory_order_acquire))
            return false;
        ring_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    bool pop(T& value)
    {
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        // make sure we don't keep a copy of the objects in the ring, which may take up memory
        // (for instance if the objects have big buffers), and more importantly may contain
        // shared_ptr that could prevent timely deletion.
        value = std::move(ring_[tail]);
        tail_.store(next(tail), std::memory_order_release);
        return true;
    }

private:
    std::size_t next(std::size_t current) { return (current + 1) % (Size + 1); }
    T ring_[Size + 1];
    std::atomic<std::size_t> head_;
    volatile char c[1024];
    std::atomic<std::size_t> tail_;
};

template<typename T, std::size_t Size> class spsc_queue_double_spin_lock
{
public:
    spsc_queue_double_spin_lock()
        : head_(0)
        , tail_(0)
    {
    }

    bool push(const T& value)
    {
        scoped_spin_lock l(write);
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t next_head = next(head);
        if (next_head == tail_.load(std::memory_order_acquire))
            return false;
        ring_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    bool pop(T& value)
    {
        scoped_spin_lock l(read);
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        // make sure we don't keep a copy of the objects in the ring, which may take up memory
        // (for instance if the objects have big buffers), and more importantly may contain
        // shared_ptr that could prevent timely deletion.
        value = std::move(ring_[tail]);
        tail_.store(next(tail), std::memory_order_release);
        return true;
    }

private:
    std::size_t next(std::size_t current) { return (current + 1) % (Size + 1); }
    T ring_[Size + 1];
    spin_lock read;
    spin_lock write;
    std::atomic<std::size_t> head_, tail_;
};

template<typename T, std::size_t Size> class spsc_queue_single_spin_lock
{
public:
    spsc_queue_single_spin_lock()
        : head_(0)
        , tail_(0)
    {
    }

    bool push(const T& value)
    {
        scoped_spin_lock l(lock);
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t next_head = next(head);
        if (next_head == tail_.load(std::memory_order_acquire))
            return false;
        ring_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    bool pop(T& value)
    {
        scoped_spin_lock l(lock);
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        // make sure we don't keep a copy of the objects in the ring, which may take up memory
        // (for instance if the objects have big buffers), and more importantly may contain
        // shared_ptr that could prevent timely deletion.
        value = std::move(ring_[tail]);
        tail_.store(next(tail), std::memory_order_release);
        return true;
    }

private:
    std::size_t next(std::size_t current) { return (current + 1) % (Size + 1); }
    T ring_[Size + 1];
    spin_lock lock;
    std::atomic<std::size_t> head_, tail_;
};

template<typename T, std::size_t Size> class spsc_queue_double_mutex
{
public:
    spsc_queue_double_mutex()
        : head_(0)
        , tail_(0)
    {
    }

    bool push(const T& value)
    {
        std::scoped_lock l(write);
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t next_head = next(head);
        if (next_head == tail_.load(std::memory_order_acquire))
            return false;
        ring_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    bool pop(T& value)
    {
        std::scoped_lock l(read);
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        // make sure we don't keep a copy of the objects in the ring, which may take up memory
        // (for instance if the objects have big buffers), and more importantly may contain
        // shared_ptr that could prevent timely deletion.
        value = std::move(ring_[tail]);
        tail_.store(next(tail), std::memory_order_release);
        return true;
    }

private:
    std::size_t next(std::size_t current) { return (current + 1) % (Size + 1); }
    T ring_[Size + 1];
    std::mutex read;
    std::mutex write;
    std::atomic<std::size_t> head_, tail_;
};

template<typename T, std::size_t Size> class spsc_queue_single_mutex
{
public:
    spsc_queue_single_mutex()
        : head_(0)
        , tail_(0)
    {
    }

    bool push(const T& value)
    {
        std::scoped_lock l(lock);
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t next_head = next(head);
        if (next_head == tail_.load(std::memory_order_acquire))
            return false;
        ring_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    bool pop(T& value)
    {
        std::scoped_lock l(lock);
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        // make sure we don't keep a copy of the objects in the ring, which may take up memory
        // (for instance if the objects have big buffers), and more importantly may contain
        // shared_ptr that could prevent timely deletion.
        value = std::move(ring_[tail]);
        tail_.store(next(tail), std::memory_order_release);
        return true;
    }

private:
    std::size_t next(std::size_t current) { return (current + 1) % (Size + 1); }
    T ring_[Size + 1];
    std::mutex lock;
    std::atomic<std::size_t> head_, tail_;
};

template<typename T, std::size_t Size> class spsc_queue_spin_lock_no_atomic
{
public:
    spsc_queue_spin_lock_no_atomic()
        : head_(0)
        , tail_(0)
    {
    }

    bool push(const T& value)
    {
        scoped_spin_lock l(write);
        std::size_t head = head_;
        std::size_t next_head = next(head);
        if (next_head == tail_)
            return false;
        ring_[head] = value;
        head_ = next_head;
        return true;
    }
    bool pop(T& value)
    {
        scoped_spin_lock l(write);
        std::size_t tail = tail_;
        if (tail == head_)
            return false;
        // make sure we don't keep a copy of the objects in the ring, which may take up memory
        // (for instance if the objects have big buffers), and more importantly may contain
        // shared_ptr that could prevent timely deletion.
        value = std::move(ring_[tail]);
        tail_ = next(tail);
        return true;
    }

private:
    std::size_t next(std::size_t current) { return (current + 1) % (Size + 1); }
    T ring_[Size + 1];
    spin_lock write;
    std::size_t head_, tail_;
};

template<typename T, std::size_t Size> class spsc_queue_spin_lock_no_raii
{
public:
    spsc_queue_spin_lock_no_raii()
        : head_(0)
        , tail_(0)
    {
    }

    bool push(const T& value) noexcept
    {
        write.lock();
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t next_head = next(head);
        if (next_head == tail_.load(std::memory_order_acquire))
        {
            write.unlock();
            return false;
        }
        ring_[head] = value;
        head_.store(next_head, std::memory_order_release);
        write.unlock();
        return true;
    }
    bool pop(T& value) noexcept
    {
        read.lock();
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
        {
            read.unlock();
            return false;
        }
        // make sure we don't keep a copy of the objects in the ring, which may take up memory
        // (for instance if the objects have big buffers), and more importantly may contain
        // shared_ptr that could prevent timely deletion.
        value = std::move(ring_[tail]);
        tail_.store(next(tail), std::memory_order_release);
        read.unlock();
        return true;
    }

private:
    std::size_t next(std::size_t current) noexcept { return (current + 1) % (Size + 1); }
    T ring_[Size + 1];
    spin_lock read;
    spin_lock write;
    std::atomic<std::size_t> head_, tail_;
};

static void test_spsc_queue(benchmark::State& state)
{
    srand(time(nullptr));

    spsc_queue<int, max_items + 1> queue;
    size_t num_items = max_items;
    for (auto _ : state)
    {
        thread consumer([&] {
            size_t consumed_items = 0;
            int v;
            while (consumed_items < num_items)
            {
                if (queue.pop(v))
                    consumed_items++;
                else
                    idle();
            }
        });

        // producing
        for (size_t i = 0; i < num_items; ++i)
        {
            queue.push(rand());
        }
        consumer.join();
    }
}
BENCHMARK(test_spsc_queue);
static void test_spsc_queue_plus_one(benchmark::State& state)
{
    srand(time(nullptr));

    spsc_queue_plus_one<int, max_items> queue;
    size_t num_items = max_items;
    for (auto _ : state)
    {
        thread consumer([&] {
            size_t consumed_items = 0;
            int v;
            while (consumed_items < num_items)
            {
                if (queue.pop(v))
                    consumed_items++;
                else
                    idle();
            }
        });

        // producing
        for (size_t i = 0; i < num_items; ++i)
        {
            queue.push(rand());
        }
        consumer.join();
    }
}

BENCHMARK(test_spsc_queue_plus_one);

static void test_spsc_queue_no_sharing(benchmark::State& state)
{
    srand(time(nullptr));

    spsc_queue_no_false_sharing<int, max_items> queue;
    size_t num_items = max_items;
    for (auto _ : state)
    {
        thread consumer([&] {
            size_t consumed_items = 0;
            int v;
            while (consumed_items < num_items)
            {
                if (queue.pop(v))
                    consumed_items++;
                else
                    idle();
            }
        });

        // producing
        for (size_t i = 0; i < num_items; ++i)
        {
            queue.push(rand());
        }
        consumer.join();
    }
}

BENCHMARK(test_spsc_queue_no_sharing);

static void test_spsc_queue_double_spin_lock(benchmark::State& state)
{
    srand(time(nullptr));

    spsc_queue_double_spin_lock<int, max_items> queue;
    size_t num_items = max_items;
    for (auto _ : state)
    {
        thread consumer([&] {
            size_t consumed_items = 0;
            int v;
            while (consumed_items < num_items)
            {
                if (queue.pop(v))
                    consumed_items++;
                else
                    idle();
            }
        });

        // producing
        for (size_t i = 0; i < num_items; ++i)
        {
            queue.push(rand());
        }
        consumer.join();
    }
}

BENCHMARK(test_spsc_queue_double_spin_lock);

static void test_spsc_queue_single_spin_lock(benchmark::State& state)
{
    srand(time(nullptr));

    spsc_queue_single_spin_lock<int, max_items> queue;
    size_t num_items = max_items;
    for (auto _ : state)
    {
        thread consumer([&] {
            size_t consumed_items = 0;
            int v;
            while (consumed_items < num_items)
            {
                if (queue.pop(v))
                    consumed_items++;
                else
                    idle();
            }
        });

        // producing
        for (size_t i = 0; i < num_items; ++i)
        {
            queue.push(rand());
        }
        consumer.join();
    }
}

BENCHMARK(test_spsc_queue_single_spin_lock);

static void test_spsc_queue_double_mutex(benchmark::State& state)
{
    srand(time(nullptr));

    spsc_queue_double_mutex<int, max_items> queue;
    size_t num_items = max_items;
    for (auto _ : state)
    {
        thread consumer([&] {
            size_t consumed_items = 0;
            int v;
            while (consumed_items < num_items)
            {
                if (queue.pop(v))
                    consumed_items++;
                else
                    idle();
            }
        });

        // producing
        for (size_t i = 0; i < num_items; ++i)
        {
            queue.push(rand());
        }
        consumer.join();
    }
}

BENCHMARK(test_spsc_queue_double_mutex);

static void test_spsc_queue_single_mutex(benchmark::State& state)
{
    srand(time(nullptr));

    spsc_queue_single_mutex<int, max_items> queue;
    size_t num_items = max_items;
    for (auto _ : state)
    {
        thread consumer([&] {
            size_t consumed_items = 0;
            int v;
            while (consumed_items < num_items)
            {
                if (queue.pop(v))
                    consumed_items++;
                else
                    idle();
            }
        });

        // producing
        for (size_t i = 0; i < num_items; ++i)
        {
            queue.push(rand());
        }
        consumer.join();
    }
}

BENCHMARK(test_spsc_queue_single_mutex);

static void test_spsc_queue_spin_lock_no_atomic(benchmark::State& state)
{
    srand(time(nullptr));

    spsc_queue_spin_lock_no_atomic<int, max_items> queue;
    size_t num_items = max_items;
    for (auto _ : state)
    {
        thread consumer([&] {
            size_t consumed_items = 0;
            int v;
            while (consumed_items < num_items)
            {
                if (queue.pop(v))
                    consumed_items++;
                else
                    idle();
            }
        });

        // producing
        for (size_t i = 0; i < num_items; ++i)
        {
            queue.push(rand());
        }
        consumer.join();
    }
}

BENCHMARK(test_spsc_queue_spin_lock_no_atomic);

static void test_spsc_queue_spin_lock_no_raii(benchmark::State& state)
{
    srand(time(nullptr));

    spsc_queue_spin_lock_no_raii<int, max_items> queue;
    size_t num_items = max_items;
    for (auto _ : state)
    {
        thread consumer([&] {
            size_t consumed_items = 0;
            int v;
            while (consumed_items < num_items)
            {
                if (queue.pop(v))
                    consumed_items++;
                else
                    idle();
            }
        });

        // producing
        for (size_t i = 0; i < num_items; ++i)
        {
            queue.push(rand());
        }
        consumer.join();
    }
}

BENCHMARK(test_spsc_queue_spin_lock_no_raii);

BENCHMARK_MAIN();