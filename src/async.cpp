// Copyright 2024 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/async.hpp"

// stdc++
#include <cassert>
//#include <cxxabi.h>
//#include <filesystem>
//#include <fstream>
//#include <functional>
//#include <list>
//#include <mutex>
//#include <set>
//#include <thread>
//#include <unordered_map>
//
//// stlab
//#include <stlab/concurrency/default_executor.hpp>
//#include <stlab/concurrency/future.hpp>
//#include <stlab/concurrency/serial_queue.hpp>
//#include <stlab/concurrency/utility.hpp>
//
//// toml++
//#include <toml++/toml.h>
//
//// tbb
//#include <tbb/concurrent_unordered_map.h>
//
//// application
#include "orc/settings.hpp"
#include "orc/task_system.hpp"
#include "orc/tracy.hpp"

/**************************************************************************************************/

namespace orc {

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

struct work_counter {
    struct state {
        void increment() {
            {
                std::lock_guard<std::mutex> lock(_m);
                ++_n;
            }
            _c.notify_all();
        }

        void decrement() {
            {
                std::lock_guard<std::mutex> lock(_m);
                --_n;
            }
            _c.notify_all();
        }

        void wait() {
            std::unique_lock<std::mutex> lock(_m);
            if (_n == 0) return;
            _c.wait(lock, [&] { return _n == 0; });
        }

        std::mutex _m;
        std::condition_variable _c;
        std::size_t _n{0};
    };

    using shared_state = std::shared_ptr<state>;

    friend struct token;

public:
    work_counter() : _impl{std::make_shared<state>()} {}

    struct token {
        token(shared_state w) : _w(std::move(w)) { _w->increment(); }
        token(const token& t) : _w{t._w} { _w->increment(); }
        token(token&& t) = default;
        ~token() {
            if (_w) _w->decrement();
        }

    private:
        shared_state _w;
    };

    auto working() { return token(_impl); }

    void wait() { _impl->wait(); }

private:
    shared_state _impl;

    void increment() { _impl->increment(); }
    void decrement() { _impl->decrement(); }
};

/**************************************************************************************************/

auto& work() {
    static work_counter _work;
    return _work;
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

void do_work(std::function<void()> f) {
    auto doit = [_f = std::move(f)]() {
        // I changed my opinion on this: an unhandled background task exception should terminate
        // the application. This mimics the behavior of an unhandled exception on the main
        // thread. Now (like main thread exceptions) background task exceptions must be
        // handled before they hit this point.
        try {
            _f();
        } catch (const std::exception& error) {
            const char* what = error.what();
            (void)what; // so you can see it in the debugger.
            assert(!"unhandled background task exception");
            std::terminate();
        } catch (...) {
            assert(!"unknown unhandled background task exception");
            std::terminate();
        }
    };

    if (!settings::instance()._parallel_processing) {
        doit();
        return;
    }

    static orc::task_system system;

    system([_work_token = work().working(), _doit = std::move(doit)] {
#if ORC_FEATURE(TRACY)
        thread_local bool tracy_set_thread_name_k = [] {
            TracyCSetThreadName(
                orc::tracy::format_unique("worker %s", orc::tracy::unique_thread_name()));
            return true;
        }();
        (void)tracy_set_thread_name_k;
#endif // ORC_FEATURE(TRACY)
        _doit();
    });
}

/**************************************************************************************************/
// It would be groovy if this routine could do some of the work, too, while it is waiting for the
// pool to finish up.
void block_on_work() {
    work().wait();
}

/**************************************************************************************************/

} // namespace orc

/**************************************************************************************************/
