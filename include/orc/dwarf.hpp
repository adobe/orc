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

//--------------------------------------------------------------------------------------------------

using die_pair = std::tuple<die, attribute_sequence>;

struct dwarf {
    struct implementation;

    dwarf(std::uint32_t ofd_index, freader&& s, file_details&& details);

    void register_section(std::string name, std::size_t offset, std::size_t size);

    void process_all_dies();

    die_pair fetch_one_die(std::size_t die_offset,
                           std::size_t cu_header_offset,
                           std::size_t cu_die_offset);

private:
    std::unique_ptr<implementation, void (*)(implementation*)> _impl{nullptr,
                                                                     [](implementation*) {}};
};

//--------------------------------------------------------------------------------------------------
