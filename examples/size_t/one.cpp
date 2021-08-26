// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#include <sys/types.h> // typedef __darwin_size_t size_t;

#include <string>
#include <typeinfo>

namespace {

inline int& function(size_t) {
    static int result{42};
    return result;
}

} // namespace

std::string one() {
    return typeid(size_t).name() + std::string("/") + typeid(function).name();
}
