// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <iostream>

// application
#include "orc/parse_file.hpp"

/**************************************************************************************************/

template <typename C>
void move_append(C& dst, C&& src) {
    dst.insert(dst.end(),
               std::move_iterator(src.begin()),
               std::move_iterator(src.end()));
    src.clear();
}

/**************************************************************************************************/

void read_macho(object_ancestry&& ancestry,
                freader s,
                std::istream::pos_type end_pos,
                file_details details,
                callbacks callbacks);

/**************************************************************************************************/

struct dwarf dwarf_from_macho(std::uint32_t ofd_index, register_dies_callback&& callback);

/**************************************************************************************************/

std::vector<std::filesystem::path> macho_derive_dylibs(const std::vector<std::filesystem::path>& root_binaries);

/**************************************************************************************************/
