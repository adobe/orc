// Copyright 2022 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <vector>

// application
#include "orc/allocator.hpp"

/**************************************************************************************************/

namespace orc {

/**************************************************************************************************/

#if ORC_FEATURE(ALLOCATOR)

template <class T>
using vector = std::vector<T, orc::allocator<T>>;

#else

template <class T>
using vector = std::vector<T>;

#endif // ORC_FEATURE(ALLOCATOR)

/**************************************************************************************************/

} // namespace orc

/**************************************************************************************************/
