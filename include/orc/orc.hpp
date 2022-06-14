// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <filesystem>
#include <unordered_map>
#include <vector>

// application
#include <orc/dwarf_structs.hpp>

/**************************************************************************************************/

struct odrv_report {
    std::string_view _symbol;
    const die* _die{nullptr};
    dw::at _name;

    std::string category() const;
    pool_string attribute_string(dw::at name) const;
};

std::ostream& operator<<(std::ostream& s, const odrv_report& x);

/**************************************************************************************************/

std::vector<odrv_report> orc_process(const std::vector<std::filesystem::path>&);

void orc_reset();

// The returned char* is good until the next call to demangle() on the same thread.
const char* demangle(const char* x);

/**************************************************************************************************/
