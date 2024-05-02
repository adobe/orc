// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <iostream>
#include <memory>

// application
#include "orc/parse_file.hpp"

/**************************************************************************************************/

using die_pair = std::tuple<die, attribute_sequence>;

struct dwarf {
    dwarf(std::uint32_t ofd_index,
          freader&& s,
          file_details&& details,
          register_dies_callback&& callback);

    void register_section(std::string name, std::size_t offset, std::size_t size);

    void process_all_dies(); // assumes register die mode

    die_pair fetch_one_die(std::size_t debug_info_offset, std::size_t cu_address);

private:
    struct implementation;
    std::unique_ptr<implementation, void (*)(implementation*)> _impl{nullptr,
                                                                     [](implementation*) {}};
};

/**************************************************************************************************/
