// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#include <stddef.h> // typedef long unsigned int size_t;

#include <string>
#include <typeinfo>

namespace {

inline int& function(size_t) {
    static int result{42};
    return result;
}

} // namespace

std::string two() {
    return typeid(size_t).name() + std::string("/") + typeid(function).name();
}
