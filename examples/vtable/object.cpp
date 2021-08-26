// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#define ADOBE_ORC_ENABLE_FLAG() 1

#include "object.hpp"

namespace example_vtable {

std::string object::optional() const { return "optional"; }
std::string object::api() const { return "api"; }

} // namespace example_vtable {
