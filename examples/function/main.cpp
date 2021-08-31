// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#include <iostream>

void alt();

namespace example_function {

template <class T>
T foo(T x) {
    return x;
}

} // namespace example_function

int main() {
    std::cout << "main: " << example_function::foo<int>(42) << '\n';

    alt();
}
