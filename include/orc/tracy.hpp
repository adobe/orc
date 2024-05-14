// Copyright 2023 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// From the tracy docs:
//     Some source location data such as function name, file path or line number can be overriden
//     with defines TracyFunction, TracyFile, TracyLine made before including tracy/Tracy.hpp.
//     By default the macros unwrap to __FUNCTION__, __FILE__ and __LINE__ respectively.

#if defined(__clang__) || defined(__GNUC__)
    #define TracyFunction __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
    #define TracyFunction __FUNCSIG__
#endif

#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

#include <orc/features.hpp>

// These `L` suffixes are wrong; they're not compile-time literals
#if ORC_FEATURE(TRACY)
    #define ZoneTextL(msg) ZoneText((msg), std::strlen(msg));
    #define ZoneNameL(msg) ZoneName((msg), std::strlen(msg));
#else
    #define ZoneTextL(msg)
    #define ZoneNameL(msg)
#endif

//==================================================================================================
// This used to be orc::tracy, but then the compiler would complain about some of the Tracy macros
// failing to resolve to symbols in this nested namespace. It failed to find the global ::tracy
// namespace where they actually live. This has been renamed `profiler` to avoid the collision.
namespace orc::profiler {

//==================================================================================================
// returns a unique `const char*` per thread for the lifetime of the application. A _brief_ name,
// and unrelated to the (unique) C++ thread id of the thread. Calling this routine with Tracy
// disabled will throw an exception.
const char* unique_thread_name();

// returns a NEW `const char*` every time it is called. Intended to be used during thread_local
// initialization. The memory is intended to leak. Calling this routine with Tracy disabled will
// throw an exception. The max string length returned is 32 characters.
const char* format_unique(const char* format, ...);

// MUST be called FIRST in your `main` routine. It does a couple things. The first is to set the
// main thread's name to `main`. The second is to install a handler to block the app shutdown until
// the Tracy profiler has completed sending all data to the Tracy analyzer. (This emulates the
// `TRACY_NO_EXIT` behavior for platforms like macOS which do not support it.) If the analyzer is
// not connected, the handler returns immediately. Note that if there is any static/global teardown
// profiling, it may be missed even if you use this call. Calling this routine with Tracy disabled
// does nothing.
void initialize();

//==================================================================================================

} // namespace orc::profiler

//==================================================================================================
