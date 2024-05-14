// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/orc.hpp"

// stdc++
#include <array>
#include <cxxabi.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <list>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

// stlab
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/serial_queue.hpp>
#include <stlab/concurrency/utility.hpp>

// toml++
#include <toml++/toml.h>

// tbb
#include <tbb/concurrent_unordered_map.h>

// application
#include "orc/async.hpp"
#include "orc/dwarf.hpp"
#include "orc/features.hpp"
#include "orc/macho.hpp"
#include "orc/memory.hpp"
#include "orc/object_file_registry.hpp"
#include "orc/parse_file.hpp"
#include "orc/settings.hpp"
#include "orc/str.hpp"
#include "orc/string_pool.hpp"
#include "orc/tracy.hpp"

/**************************************************************************************************/

std::mutex& ostream_safe_mutex() {
    static std::mutex m;
    return m;
}

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

std::string_view path_to_symbol(std::string_view path) {
    // lop off the prefix. In most cases it'll be "::[u]::" - in some it'll be just "::[u]" (in
    // which case there is no symbol path - it's a top-level compilation unit.) We assume the path
    // here starts with one of the two options above, and so return the result based on the string
    // length.

    return path.size() < 7 ? std::string_view() : path.substr(7);
}

/**************************************************************************************************/

bool type_equivalent(const attribute& x, const attribute& y);

bool attributes_conflict(dw::at name, const attribute& x, const attribute& y) {
    if (name == dw::at::type && type_equivalent(x, y)) {
        return false;
    }

    return x != y;
}

std::vector<dw::at> fatal_attribute_names(const attribute_sequence& x) {
    std::vector<dw::at> result;
    for (const auto& entry : x) {
        if (nonfatal_attribute(entry._name)) continue;
        result.push_back(entry._name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<dw::at> find_attribute_conflict(const attribute_sequence& x,
                                            const attribute_sequence& y) {
    const auto x_names = fatal_attribute_names(x);
    const auto y_names = fatal_attribute_names(y);

    std::vector<dw::at> result;
    std::vector<dw::at> intersection;

    std::set_symmetric_difference(x_names.begin(), x_names.end(), y_names.begin(), y_names.end(),
                                  std::back_inserter(result));

    std::set_intersection(x_names.begin(), x_names.end(), y_names.begin(), y_names.end(),
                          std::back_inserter(intersection));

    const auto xf = x.begin();
    const auto xl = x.end();
    const auto yf = y.begin();
    const auto yl = y.end();

    for (const auto name : intersection) {
        auto xfound = std::find_if(xf, xl, [&](auto& x) { return name == x._name; });
        auto yfound = std::find_if(yf, yl, [&](auto& y) { return name == y._name; });
        assert(xfound != xl);
        assert(yfound != yl);
        if (!attributes_conflict(name, *xfound, *yfound)) continue;
        result.push_back(name);
    }

    return result;
}

/**************************************************************************************************/

bool type_equivalent(const attribute& x, const attribute& y) {
    // types are pretty convoluted, so we pull their comparison out here in an effort to
    // keep it all in a developer's head.

    if (x.has(attribute_value::type::string) && y.has(attribute_value::type::string)) {
        return x.string_hash() == y.string_hash();
    }

    if (x.has(attribute_value::type::reference) && y.has(attribute_value::type::reference)) {
        return x.reference() == y.reference();
    }

    // Type mismatch.
    return false;
}

/**************************************************************************************************/

auto& unsafe_global_die_collection() {
    static decltype(auto) collection_s = orc::make_leaky<std::list<dies>>();
    return collection_s;
}

template <class F>
auto with_global_die_collection(F&& f) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    return f(unsafe_global_die_collection());
}

/**************************************************************************************************/

auto& global_die_map() {
    static decltype(auto) map_s =
        orc::make_leaky<tbb::concurrent_unordered_map<std::size_t, die*>>();
    return map_s;
}

/**************************************************************************************************/

struct cmdline_results {
    std::vector<std::filesystem::path> _file_object_list;
    bool _ld_mode{false};
    bool _libtool_mode{false};
};

/**************************************************************************************************/

const char* problem_prefix() { return settings::instance()._graceful_exit ? "warning" : "error"; }

/**************************************************************************************************/

attribute_sequence fetch_attributes_for_die(const die& d) {
    // Too verbose for larger projects, but keep around for debugging/smaller projects.
    // ZoneScoped;

    auto dwarf = dwarf_from_macho(d._ofd_index, macho_params{macho_reader_mode::odrv_reporting});

    auto [die, attributes] = dwarf.fetch_one_die(d._debug_info_offset, d._cu_die_address);
    assert(die._tag == d._tag);
    assert(die._arch == d._arch);
    assert(die._has_children == d._has_children);
    assert(die._debug_info_offset == d._debug_info_offset);
    return std::move(attributes);
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

odrv_report::odrv_report(std::string_view symbol, const die* list_head)
    : _symbol(symbol), _list_head(list_head) {
    // Too verbose for larger projects, but keep around for debugging/smaller projects.
    // ZoneScoped;

    assert(_list_head->_conflict);

    // Construct a map of unique definitions of the conflicting symbol.
    // Each entry in `conflict_map` will be a collection of dies
    // whose fatal attribute hashes are all the same.
    for (const die* next_die = _list_head; next_die; next_die = next_die->_next_die) {
        const die& die = *next_die;
        const std::size_t hash = die._fatal_attribute_hash;
        const bool new_conflict = _conflict_map.count(hash) == 0;
        auto& conflict = _conflict_map[hash];

        ++conflict._count;

        if (die._location) {
            conflict._locations[*die._location].emplace_back(object_file_ancestry(die._ofd_index));
        }

        if (new_conflict) {
            // The fatal attribute hash should be the same for all instances
            // of this `conflict`, so we only need to set its attributes once.
            conflict._attributes = fetch_attributes_for_die(die);
            conflict._tag = die._tag;
        }
    }

    assert(_conflict_map.size() > 1);

    // Derive the ODRV categories.
    const auto conflict_first = _conflict_map.begin();
    const auto conflict_last = _conflict_map.end();

    for (auto x = conflict_first; x != conflict_last; ++x) {
        for (auto y = std::next(x); y != conflict_last; ++y) {
            auto conflicts = find_attribute_conflict(x->second._attributes, y->second._attributes);
            _conflicting_attributes.insert(_conflicting_attributes.end(), conflicts.begin(),
                                           conflicts.end());
        }
    }

    sort_unique(_conflicting_attributes);
}

/**************************************************************************************************/

bool should_report_category(const std::string& category) {
    auto& settings = settings::instance();

    if (!settings._violation_ignore.empty()) {
        // Report everything except the stuff on the ignore list (denylist)
        return !sorted_has(settings._violation_ignore, category);
    } else if (!settings._violation_report.empty()) {
        // Report nothing except the the stuff on the report list (allowlist)
        return sorted_has(settings._violation_report, category);
    }

    return true;
}

/**************************************************************************************************/
// Returns a comma-separated serialization of categories NOT considered when determining an ODRV.
std::string odrv_report::filtered_categories() const {
    std::string result;
    bool first = true;

    for (std::size_t i = 0; i < category_count(); ++i) {
        auto c = category(i);
        if (should_report_category(c)) continue;

        if (first) {
            first = false;
        } else {
            result += ", ";
        }

        result += std::move(c);
    }

    return result;
}

/**************************************************************************************************/
// Returns a comma-separated serialization of categories considered when determining an ODRV.
std::string odrv_report::reporting_categories() const {
    std::string result;
    bool first = true;

    for (std::size_t i = 0; i < category_count(); ++i) {
        auto c = category(i);
        if (!should_report_category(c)) continue;

        if (first) {
            first = false;
        } else {
            result += ", ";
        }

        result += std::move(c);
    }

    return result;
}

/**************************************************************************************************/
// Generates a category "slug" based on this symbol's kind + the category (e.g. member:type).
std::string odrv_report::category(std::size_t n) const {
    return to_string(_conflict_map.begin()->second._tag) + std::string(":") +
           (_conflicting_attributes.empty() ? "<none>" : to_string(_conflicting_attributes[n]));
}

/**************************************************************************************************/
// Given an ODRV report, we need to decide if the report's categories are ones we should
// report for, or if we should filter this report out.
bool emit_report(const odrv_report& report) {
    // The general rule here is that if any category is
    // marked "report", we issue the ODRV report.
    for (std::size_t i = 0; i < report.category_count(); ++i) {
        if (should_report_category(report.category(i))) {
            return true;
        }
    }

    return false;
}

/**************************************************************************************************/

template <class MapType>
auto sorted_keys(const MapType& map) {
    std::vector<typename MapType::key_type> result;
    for (const auto& entry : map) {
        result.emplace_back(entry.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

/**************************************************************************************************/

std::ostream& operator<<(std::ostream& s, const odrv_report& report) {
    const std::string_view& symbol = report._symbol;

    s << problem_prefix() << ": ODRV (" << report.reporting_categories() << "); "
      << report.conflict_map().size() << " conflicts with `"
      << (symbol.data() ? demangle(symbol.data()) : "<unknown>") << "`\n";
    const auto& conflicts = report.conflict_map();
    for (const auto& entry : sorted_keys(conflicts)) {
        const auto& conflict = conflicts.at(entry);
        const auto& locations = conflict._locations;

        s << conflict._attributes;
        s << "    symbol defintion location(s):\n";
        for (const auto& entry : sorted_keys(locations)) {
            const auto& instances = locations.at(entry);
            s << "        " << entry << " (used by `" << instances.front() << "` and "
              << (instances.size() - 1) << " others)\n";
        }
        s << '\n';
    }
    s << "\n";

    return s;
}

/**************************************************************************************************/

die* enforce_odrv_for_die_list(die* base, std::vector<odrv_report>& results) {
    ZoneScoped;

    // pre-flight the vector allocation by counting the number of dies
    // we'll be storing in it.
    std::size_t count = 0;
    for (die* ptr = base; ptr; ptr = ptr->_next_die) {
        ++count;
    }

    ZoneValue(count);

    if (count == 1) return base;

    // By making this thread_local and using `resize`, we are only
    // reallocating the vector when the die count grows between runs;
    // Otherwise, we are reusing the same memory over again, saving
    // us time.
    thread_local std::vector<die*> dies;
    dies.resize(count, nullptr);

    // traverse the linked list and put its pointers into the vector
    // so we can sort them by file ancestry.
    std::size_t i = 0;
    for (die* ptr = base; ptr; ptr = ptr->_next_die) {
        dies[i++] = ptr;
    }

    assert(dies.front() == base);
    assert(dies.back() != nullptr);

    // Theory: if multiple copies of the same source file were compiled,
    // the ancestry might not be unique. We assume that's an edge case
    // and the ancestry is unique.
    std::sort(dies.begin(), dies.end(), [](const die* a, const die* b) {
        return object_file_ancestry(a->_ofd_index) < object_file_ancestry(b->_ofd_index);
    });

    bool conflict{false};
    for (size_t i = 1; i < dies.size(); ++i) {
        // Re-link the die list to match the sorted order.
        dies[i - 1]->_next_die = dies[i];

        // Check and see if we have any conflicts along the way.
        if (!conflict) {
            conflict = dies[i - 1]->_fatal_attribute_hash != dies[i]->_fatal_attribute_hash;
        }
    }
    dies.back()->_next_die = nullptr;

    if (!conflict) return dies.front();

    dies[0]->_conflict = true;

    odrv_report report{path_to_symbol(base->_path.view()), dies[0]};

    // This can be very contentious for large projects. We may want to think
    // about a more efficient way to collect these reports per-thread, then
    // bubble them up once they've all been collected.
    static TracyLockable(std::mutex, odrv_report_mutex);
    {
        std::lock_guard<LockableBase(std::mutex)> lock(odrv_report_mutex);
        results.push_back(report);
    }

    return dies.front();
}

/**************************************************************************************************/

std::vector<odrv_report> orc_process(std::vector<std::filesystem::path>&& file_list) {
    // First stage: (optional) dependency/dylib preprocessing
    if (settings::instance()._dylib_scan_mode) {
        TracyMessageL("orc_process: dylib scan");

        // dylib scan mode involves a pre-processing step where we parse the file list
        // and discover any dylibs those Mach-O files depend upon. Note that we're
        // glomming all these dependencies together, so if there are multiple
        // files in `file_list`, we could be "finding" ODRVs across independent
        // artifact+dylib groups that really do not exist.
        file_list = macho_derive_dylibs(std::move(file_list));
    }

    TracyMessageL("orc_process: process all DIEs");

    for (const auto& input_path : file_list) {
        orc::do_work([_input_path = input_path] {
            if (!exists(_input_path)) {
                if (log_level_at_least(settings::log_level::verbose)) {
                    cerr_safe([&](auto& s) {
                        s << "file " << _input_path.string() << " does not exist\n";
                    });
                }
                return;
            }

            freader input(_input_path);

            parse_file(_input_path.string(), object_ancestry(), input, input.size(),
                       macho_params{macho_reader_mode::register_dies});
        });
    }

    orc::block_on_work();

    TracyMessageL("orc_process: review DIEs for ODRVs");

    std::vector<odrv_report> result;

    // We now subdivide the work. We do so by looking at the total number of entries
    // in the map, breaking up the work into chunks, and then issuing a `do_work`
    // statement on each chunk. This is generally faster than issuing a `do_work`
    // call for each entry, as there can be _many_ entries, and each `do_work` call
    // incurs some bookkeeping. (It should go without saying that the die map
    // should not be modified while this processing happens.) We set the number of
    // work chunks to be the processor count of the machine, and resize the amount
    // of work per core.
    const std::size_t work_size = global_die_map().size();
    const std::size_t chunk_count = std::thread::hardware_concurrency();
    const std::size_t chunk_size = std::ceil(work_size / static_cast<float>(chunk_count));
    std::size_t cur_work = 0;
    auto first = global_die_map().begin();

    while (cur_work != work_size) {
        // All but the last chunk will be the same size.
        // The last one could be up to (chunk_count - 1) smaller.
        const auto next_chunk_size = std::min(chunk_size, work_size - cur_work);
        const auto last = std::next(first, next_chunk_size);
        orc::do_work([_first = first, _last = last, &result]() mutable {
            for (; _first != _last; ++_first) {
                _first->second = enforce_odrv_for_die_list(_first->second, result);
            }
        });
        cur_work += next_chunk_size;
        first = last;
    }

    orc::block_on_work();

    TracyMessageL("orc_process: Sorting & filtering ODRV reports");

    std::sort(result.begin(), result.end(),
              [](const odrv_report& a, const odrv_report& b) { return a._symbol < b._symbol; });

    return result;
}

/**************************************************************************************************/

namespace orc {

/**************************************************************************************************/

void register_dies(dies die_vector) {
    ZoneScoped;

    globals::instance()._die_processed_count += die_vector.size();

    // pre-process the vector of dies by partitioning them into those that are skippable and those
    // that are not. Then, we erase the skippable ones and shrink the vector to fit, which will
    // cause a reallocation and copying of only the necessary dies into a vector whose memory
    // consumption is exactly what's needed.

    auto unskipped_end =
        std::partition(die_vector.begin(), die_vector.end(), std::not_fn(&die::_skippable));

    std::size_t skip_count = std::distance(unskipped_end, die_vector.end());

    die_vector.erase(unskipped_end, die_vector.end());
    die_vector.shrink_to_fit();

    // This is a list so the die vectors don't move about. The dies become pretty entangled as they
    // point to one another by reference, and the odr_map itself stores const pointers to the dies
    // it registers. Thus, we move our incoming die_vector to the end of this list, and all the
    // pointers we use will stay valid for the lifetime of the application.
    dies& dies = *with_global_die_collection([&](auto& collection) {
        collection.push_back(std::move(die_vector));
        return --collection.end();
    });

    for (auto& d : dies) {
        assert(!d._skippable);

        //
        // At this point we know we're going to register the die. Hereafter belongs
        // work exclusive to DIEs getting registered/odr-enforced.
        //

        auto result = global_die_map().insert(std::make_pair(d._hash, &d));
        if (result.second) {
            ++globals::instance()._unique_symbol_count;
            continue;
        }

        constexpr auto mutex_count_k = 67; // prime; to help reduce any hash bias
        static std::mutex mutexes_s[mutex_count_k];
        std::lock_guard<std::mutex> lock(mutexes_s[d._hash % mutex_count_k]);

        die& d_in_map = *result.first->second;
        d._next_die = d_in_map._next_die;
        d_in_map._next_die = &d;
    }

    globals::instance()._die_skipped_count += skip_count;
}

/**************************************************************************************************/

} // namespace orc

/**************************************************************************************************/

void orc_reset() {
    global_die_map().clear();
    with_global_die_collection([](auto& collection) { collection.clear(); });
}

/**************************************************************************************************/

const char* demangle(const char* x) {
    // The returned char* is good until the next call to demangle() on the same thread.
    // See: https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html
    thread_local std::unique_ptr<char, void (*)(void*)> hold{nullptr, &free};
    int status = 0;
    char* p = abi::__cxa_demangle(x, nullptr, nullptr, &status);
    if (!p || status != 0) return x;
    hold.reset(p);
    return p;
}

/**************************************************************************************************/
