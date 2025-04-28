// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// application
#include "orc/dwarf_structs.hpp"
#include "orc/parse_file.hpp"

//--------------------------------------------------------------------------------------------------

struct object_file_descriptor {
    object_ancestry _ancestry;
    file_details _details;
};

//--------------------------------------------------------------------------------------------------

std::size_t object_file_register(object_ancestry&& ancestry, file_details&& details);

const object_file_descriptor& object_file_fetch(std::size_t index);

inline const object_ancestry& object_file_ancestry(std::size_t index) {
    return object_file_fetch(index)._ancestry;
}

inline const file_details& object_file_details(std::size_t index) {
    return object_file_fetch(index)._details;
}

//--------------------------------------------------------------------------------------------------
