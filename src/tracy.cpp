// Copyright 2023 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/tracy.hpp"

// stdc++
#include <cmath>
#include <sstream>

//==================================================================================================

namespace orc {

//==================================================================================================

const char* unique_thread_name() {
    static std::atomic_int counter_s{0};
    thread_local const char* result = []{
        thread_local char result[2] = {0};
        result[0] = 'A' + counter_s++;
        return result;
    }();
    return result;
}

//==================================================================================================

} // namespace orc

//==================================================================================================
