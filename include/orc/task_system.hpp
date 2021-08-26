// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <thread>

// stlab
#include <stlab/concurrency/task.hpp>

namespace orc {

using stlab::task;

inline auto queue_size() {
    return std::max(1u, std::thread::hardware_concurrency());
}

class notification_queue {
    using lock_t = std::unique_lock<std::mutex>;

    struct element_t {
        unsigned _priority;
        task<void()> _task;

        template <class F>
        element_t(F&& f, unsigned priority) : _priority{priority}, _task{std::forward<F>(f)} { }

        struct greater {
            bool operator()(const element_t& a, const element_t& b) const {
                return b._priority < a._priority;
            }
        };
    };

    std::vector<element_t> _q; // can't use priority queue because top() is const
    bool _done{false};
    std::mutex _mutex;
    std::condition_variable _ready;

    // This must be called under a lock with a non-empty _q
    task<void()> pop_not_empty() {
        auto result = std::move(_q.front()._task);
        std::pop_heap(begin(_q), end(_q), element_t::greater());
        _q.pop_back();
        return result;
    }

public:
    bool try_pop(task<void()>& x) {
        lock_t lock{_mutex, std::try_to_lock};
        if (!lock || _q.empty()) return false;
        x = pop_not_empty();
        return true;
    }

    bool pop(task<void()>& x) {
        lock_t lock{_mutex};
        while (_q.empty() && !_done) _ready.wait(lock);
        if (_q.empty()) return false;
        x = pop_not_empty();
        return true;
    }

    void done() {
        {
            lock_t lock{_mutex};
            _done = true;
        }
        _ready.notify_all();
    }

    template <typename F>
    bool try_push(F&& f, unsigned priority) {
        {
            lock_t lock{_mutex, std::try_to_lock};
            if (!lock) return false;
            _q.emplace_back(std::forward<F>(f), priority);
            std::push_heap(begin(_q), end(_q), element_t::greater());
        }
        _ready.notify_one();
        return true;
    }

    template <typename F>
    void push(F&& f, unsigned priority) {
        {
            lock_t lock{_mutex};
            _q.emplace_back(std::forward<F>(f), priority);
            std::push_heap(begin(_q), end(_q), element_t::greater());
        }
        _ready.notify_one();
    }
};

/**************************************************************************************************/

class priority_task_system {
    using lock_t = std::unique_lock<std::mutex>;

    const unsigned _count{queue_size()};

    std::vector<std::thread> _threads;
    std::vector<notification_queue> _q{_count};
    std::atomic<unsigned> _index{0};
    std::atomic_bool _done{false};

    void run(unsigned i) {
        #if STLAB_FEATURE(THREAD_NAME_POSIX)
        pthread_setname_np(pthread_self(), "adobe.orc.worker");
        #elif STLAB_FEATURE(THREAD_NAME_APPLE)
        pthread_setname_np("adobe.orc.worker");
        #endif
        while (true) {
            task<void()> f;

            for (unsigned n = 0; n != _count; ++n) {
                if (_q[(i + n) % _count].try_pop(f)) break;
            }
            if (!f && !_q[i].pop(f)) break;

            f();
        }
    }

public:
    priority_task_system() {
        _threads.reserve(_count);
        for (unsigned n = 0; n != _count; ++n) {
            _threads.emplace_back([&, n]{ run(n); });
        }
    }

    ~priority_task_system() {
        for (auto& e : _q) e.done();
        for (auto& e : _threads) e.join();
    }



    template <std::size_t P, typename F>
    void execute(F&& f) {
        static_assert(P < 3, "More than 3 priorities are not known!");
        auto i = _index++;

        for (unsigned n = 0; n != _count; ++n) {
            if (_q[(i + n) % _count].try_push(std::forward<F>(f), P)) return;
        }

        _q[i % _count].push(std::forward<F>(f), P);
    }

    bool steal() {
        task<void()> f;

        for (unsigned n = 0; n != _count; ++n) {
            if (_q[n].try_pop(f)) break;
        }
        if (!f) return false;

        f();

        return true;
    }
};

inline priority_task_system& pts() {
    static priority_task_system only_task_system;
    return only_task_system;
}

enum class executor_priority
{
    high,
    medium,
    low
};

template <executor_priority P = executor_priority::medium>
struct task_system
{
    using result_type = void;

    void operator()(task<void()> f) const {
        pts().execute<static_cast<std::size_t>(P)>(std::move(f));
    }
};

} // namespace orc

/**************************************************************************************************/
