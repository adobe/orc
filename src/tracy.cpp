// Copyright 2023 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/tracy.hpp"

// stdc++
#include <cmath>
#include <iostream>
#include <sstream>

//==================================================================================================

namespace orc::profiler {

//==================================================================================================

const char* unique_thread_name() {
#if ORC_FEATURE(TRACY)
    static std::atomic_int counter_s{0};
    thread_local const char* result = [] {
        thread_local char result[2] = {0};
        result[0] = 'A' + counter_s++;
        if (counter_s > 126) {
            // REVISIT (fosterbrereton): We should handle this case better, by extending the names
            // to e.g., `AA`, `AB`, etc.
            throw std::runtime_error("counter overflow");
        }
        return result;
    }();
    return result;
#else
    throw std::runtime_error("calling tracy support API with tracy disabled");
#endif // ORC_FEATURE(TRACY)
}

//==================================================================================================

const char* format_unique(const char* format, ...) {
#if ORC_FEATURE(TRACY)
    char* result = new char[32]; // allocate for the lifetime of the application
    va_list args;
    va_start(args, format);
    vsnprintf(result, 32, format, args);
    va_end(args);
    return result;
#else
    throw std::runtime_error("calling tracy support API with tracy disabled");
#endif // ORC_FEATURE(TRACY)
}

//==================================================================================================

void initialize() {
#if ORC_FEATURE(TRACY)
    TracyCSetThreadName("main");

    std::atexit([] {
        // On MacOS, there is no support for `TRACY_NO_EXIT`. This is a workaround provided from
        // Tracy's issue #8: https://github.com/wolfpld/tracy/issues/8#issuecomment-826349289
        auto& profiler = ::tracy::GetProfiler();

        profiler.RequestShutdown();

        // Apparently this will block even if the analyzer never connects in the first place.
        // The workaround is to connect the analyzer or preempt the application. In either
        // case, you're not losing profiling data.
        while (!profiler.HasShutdownFinished()) {
            static bool do_once = [] {
                std::cout << "Waiting for Tracy...\n";
                return true;
            }();
            (void)do_once;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        };
    });
#endif // ORC_FEATURE(TRACY)
}

//==================================================================================================

} // namespace orc::profiler

//==================================================================================================
