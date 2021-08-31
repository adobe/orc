// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#include <iostream>

namespace example_function {

template <class T>
T foo(T x) {
    return ((x + x) - 2) * 2 / x;
}

} // namespace example_function

void alt() {
    std::cout << "alt: " << example_function::foo<int>(42) << '\n';
}
