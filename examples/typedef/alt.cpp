// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#include <iostream>

namespace example_typedef {

using conflict_type = bool;

} // namespace example_typedef

void alt() {
    // conflict_type must be used in order to get compiled into the application.
    std::cout << "alt: " << example_typedef::conflict_type(42);
}
