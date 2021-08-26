// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#include <iostream>
#include <string>

#define ADOBE_ORC_ENABLE_FLAG() 1

#include "object.hpp"

namespace example_vtable {

std::string one(const object&);
std::string two(const object&);

} // namespace example_vtable

int main() {
    example_vtable::object o;

    std::cout << "one: " << one(o) << '\n';
    std::cout << "two: " << two(o) << '\n';
}
