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

struct dwarf {
    dwarf(object_ancestry&& ancestry,
          freader& s,
          const file_details& details,
          callbacks callbacks);

    void register_section(std::string name, std::size_t offset, std::size_t size);

    void process();

private:
    struct implementation;
    std::unique_ptr<implementation, void (*)(implementation*)> _impl{nullptr,
                                                                     [](implementation*) {}};
};

/**************************************************************************************************/
