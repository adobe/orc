// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <iostream>

// application
#include "orc/object_file_registry.hpp"
#include "orc/parse_file.hpp"

/**************************************************************************************************/

void read_macho(object_ancestry&& ancestry,
                freader s,
                std::istream::pos_type end_pos,
                file_details details,
                callbacks callbacks);

/**************************************************************************************************/

struct dwarf dwarf_from_macho(ofd_index index, register_dies_callback&& callback);

/**************************************************************************************************/
