// Copyright 2022 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <string>

// application
#include "orc/allocator.hpp"

/**************************************************************************************************/

namespace orc {

/**************************************************************************************************/

#if ORC_FEATURE(ALLOCATOR)

using string = std::basic_string<char, std::char_traits<char>, orc::allocator<char>>;

#else

using string = std::string;

#endif // ORC_FEATURE(ALLOCATOR)

/**************************************************************************************************/

} // namespace orc

/**************************************************************************************************/
