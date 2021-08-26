// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#define ADOBE_ORC_ENABLE_FLAG() 0

#include "object.hpp"

namespace example_vtable {

std::string two(const object& o) {
    return o.api();
}

} // namespace example_vtable

