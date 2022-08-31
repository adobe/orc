// Copyright 2022 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// application
#include "orc/features.hpp"

/**************************************************************************************************/

namespace orc {

/**************************************************************************************************/

// This tool generates GB worth of in-memory data, and keeps it resident for the entire lifetime of
// the application. It does this to maximize performance while doing symbol collision analysis. It
// has been observed in Instruments that the cleanup of these large types during `std::atexit`
// contributes to *at least* half the total running time of the application. This feature is an
// experimental switch that, when enabled, will allocate these large containers via `new`, and let
// them leak intentionally. Since the destruction of these types is otherwise just to clean up
// memory that we're about to let go of anyways with the termination of the application, we let
// them leak on purpose, cutting our total execution time in half (or better.)
//
// How to use: instantiate the variable like so:
//
//    static decltype(auto) my_variable = orc::make_leaky<my_type>();
//
// By using `decltype(auto)`, the type of the variable will either be a my_type& (when leaky) or
// my_type (when not leaky). It's magic. In the case of the leaky memory, the reference destructing
// will not cause the pointer behind it to destruct. In the non-leaky case, you'll NRVO a copy of
// the type into your local variable, and it'll clean up properly when the application tears down.
// If you don't use `decltype(auto)`, you'll always fall into the latter bucket, and clean up.

#define ORC_PRIVATE_FEATURE_LEAKY_MEMORY() (1)

template <class T, class... Args>
decltype(auto) make_leaky(Args&&... args) {
#if ORC_FEATURE(LEAKY_MEMORY)
    return *new T(std::forward<Args>(args)...);
#else
    return T(std::forward<Args>(args)...);
#endif
}

/**************************************************************************************************/

} // namespace orc

/**************************************************************************************************/
