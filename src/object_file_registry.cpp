// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/object_file_registry.hpp"

// tbb
#include <tbb/concurrent_vector.h>

//--------------------------------------------------------------------------------------------------

namespace {

//--------------------------------------------------------------------------------------------------

tbb::concurrent_vector<object_file_descriptor>& obj_registry() {
    static tbb::concurrent_vector<object_file_descriptor> result;
    return result;
}

//--------------------------------------------------------------------------------------------------

} // namespace

//--------------------------------------------------------------------------------------------------

std::size_t object_file_register(object_ancestry&& ancestry, file_details&& details) {
    // According to the OneTBB website,
    // "Growing the container does not invalidate any existing iterators or indices."
    // https://spec.oneapi.io/versions/latest/elements/oneTBB/source/containers/concurrent_vector_cls.html

    auto result = obj_registry().emplace_back(
        object_file_descriptor{std::move(ancestry), std::move(details)});
    return std::distance(obj_registry().begin(), result);
}

const object_file_descriptor& object_file_fetch(std::size_t index) { return obj_registry()[index]; }

//--------------------------------------------------------------------------------------------------
