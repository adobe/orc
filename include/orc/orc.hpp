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
#include <map>

// application
#include <orc/dwarf_structs.hpp>

/**************************************************************************************************/

struct odrv_report {
    odrv_report(std::string_view symbol, const die* list_head) : _symbol(symbol), _list_head(list_head) {}

    std::string_view _symbol;
    const die* _list_head{nullptr};

    std::string category() const;

    const auto& conflict_map() const { return _conflict_map; }

private:
    mutable std::map<std::size_t, const die*> _conflict_map;
};

std::ostream& operator<<(std::ostream& s, const odrv_report& x);

/**************************************************************************************************/

std::vector<odrv_report> orc_process(const std::vector<std::filesystem::path>&);

void orc_reset();

// The returned char* is good until the next call to demangle() on the same thread.
const char* demangle(const char* x);

/**************************************************************************************************/
