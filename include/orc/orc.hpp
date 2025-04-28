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

//--------------------------------------------------------------------------------------------------

struct odrv_report {
    odrv_report(std::string_view symbol, const die* list_head);

    std::size_t category_count() const { return _conflicting_attributes.size(); }
    std::string category(std::size_t n) const;
    std::string reporting_categories() const;
    std::string filtered_categories() const;

    using symbol_instances = std::vector<object_ancestry>;
    using symbol_declaration = location;
    using symbol_location_map = std::unordered_map<symbol_declaration, symbol_instances>;

    struct conflict_details {
        dw::tag _tag{dw::tag::none};
        attribute_sequence _attributes;
        symbol_location_map _locations;
        std::size_t _count{0}; // may be different than _locations.size()
    };

    const auto& conflict_map() const { return _conflict_map; }

    std::string_view _symbol;

private:
    const die* _list_head{nullptr};
    mutable std::map<std::size_t, conflict_details> _conflict_map;
    std::vector<dw::at> _conflicting_attributes;
};

std::ostream& operator<<(std::ostream& s, const odrv_report& x);

// Return `true` if the ODRV report is one we should emit
bool emit_report(const odrv_report& report);

//--------------------------------------------------------------------------------------------------

std::vector<odrv_report> orc_process(std::vector<std::filesystem::path>&&);

namespace orc {

void register_dies(dies die_vector);

std::string to_json(const std::vector<odrv_report>&);

std::string version_json();

} // namespace orc

void orc_reset();

// The returned char* is good until the next call to demangle() on the same thread.
const char* demangle(const char* x);

//--------------------------------------------------------------------------------------------------
// TODO: (fosterbrereton) Find a better home for this somewhere?
template <class C>
void sort_unique(C& container) {
    std::sort(container.begin(), container.end());
    const auto new_end = std::unique(container.begin(), container.end());
    container.erase(new_end, container.end());
}

//--------------------------------------------------------------------------------------------------

std::mutex& ostream_safe_mutex();

template <class F>
void ostream_safe(std::ostream& s, F&& f) {
    std::lock_guard<std::mutex> lock{ostream_safe_mutex()};
    std::forward<F>(f)(s);
    if (settings::instance()._output_file_mode == settings::output_file_mode::json) return;
    auto& output_file = globals::instance()._fp;
    if (output_file.is_open()) {
        std::forward<F>(f)(output_file);
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

//--------------------------------------------------------------------------------------------------
