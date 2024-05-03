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
#include <iostream>

// application
#include "orc/dwarf_structs.hpp"
#include "orc/settings.hpp"

/**************************************************************************************************/

struct odrv_report {
    odrv_report(std::string_view symbol, const die* list_head);

    std::string category() const;

    struct conflict_details {
        const die* _die{nullptr};
        attribute_sequence _attributes;
    };

    const auto& conflict_map() const { return _conflict_map; }

    std::string_view _symbol;

private:
    const die* _list_head{nullptr};
    mutable std::map<std::size_t, conflict_details> _conflict_map;
    dw::at _name{dw::at::none};
};

std::ostream& operator<<(std::ostream& s, const odrv_report& x);

// Return `true` if an ODRV
bool filter_report(const odrv_report& report);

/**************************************************************************************************/

std::vector<odrv_report> orc_process(std::vector<std::filesystem::path>&&);

namespace orc {

void register_dies(dies die_vector);

} // namespace orc

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

/**************************************************************************************************/
