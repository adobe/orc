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

void read_fat(object_ancestry&& ancestry,
              freader& s,
              std::istream::pos_type end_pos,
              file_details details,
              macho_params params);

/**************************************************************************************************/
