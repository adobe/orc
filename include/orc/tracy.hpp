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

namespace orc {

//==================================================================================================
// returns a unique `const char*` per thread for the lifetime of the application. A _brief_ name,
// and unrelated to the (unique) C++ thread id of the thread.
const char* unique_thread_name();

//==================================================================================================

} // namespace orc

//==================================================================================================
