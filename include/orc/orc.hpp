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
#include <iostream>

// application
#include "orc/dwarf_structs.hpp"
#include "orc/settings.hpp"

/**************************************************************************************************/

struct odrv_report {
    std::string_view _symbol;
    const die* _list_head{nullptr};
    dw::at _name;

    std::string category() const;
};

std::ostream& operator<<(std::ostream& s, const odrv_report& x);

/**************************************************************************************************/

std::vector<odrv_report> orc_process(const std::vector<std::filesystem::path>&);

void orc_reset();

// The returned char* is good until the next call to demangle() on the same thread.
const char* demangle(const char* x);

/**************************************************************************************************/


std::mutex& ostream_safe_mutex();

template <class F>
void ostream_safe(std::ostream& s, F&& f) {
    std::lock_guard<std::mutex> lock{ostream_safe_mutex()};
    std::forward<F>(f)(s);
    if (globals::instance()._fp.is_open()) {
        std::forward<F>(f)(globals::instance()._fp);
    }
}

template <class F>
void cout_safe(F&& f) {
    ostream_safe(std::cout, std::forward<F>(f));
}

template <class F>
void cerr_safe(F&& f) {
    ostream_safe(std::cerr, std::forward<F>(f));
}
