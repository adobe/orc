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
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <set>

// stlab
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/utility.hpp>
#include <stlab/concurrency/serial_queue.hpp>

// toml++
#include <toml++/toml.h>

// tbb
#include <tbb/concurrent_unordered_map.h>

// application
#include "orc/features.hpp"
#include "orc/parse_file.hpp"
#include "orc/settings.hpp"
#include "orc/str.hpp"
#include "orc/string_pool.hpp"
#include "orc/task_system.hpp"

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

auto& ostream_safe_mutex() {
    static std::mutex m;
    return m;
}

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

/**************************************************************************************************/

std::string_view path_to_symbol(std::string_view path) {
    // lop off the prefix. In most cases it'll be "::[u]::" - in some it'll be just "::[u]" (in
    // which case there is no symbol path - it's a top-level compilation unit.) We assume the path
    // here starts with one of the two options above, and so return the result based on the string
    // length.

    return path.size() < 7 ? std::string_view() : path.substr(7);
}

/**************************************************************************************************/

template <class Container, class T>
bool sorted_has(const Container& c, const T& x) {
    auto found = std::lower_bound(c.begin(), c.end(), x);
    return found != c.end() && *found == x;
}

/**************************************************************************************************/

auto& unsafe_odrv_records() {
    static std::vector<odrv_report> records;
    return records;
}

/**************************************************************************************************/

void record_odrv(const std::string_view& symbol, const die& a, const die& b, dw::at name) {
    static std::mutex record_mutex;
    std::lock_guard<std::mutex> lock(record_mutex);
    unsafe_odrv_records().push_back(odrv_report{
        symbol,
        a,
        b,
        name
    });
}

/**************************************************************************************************/

bool nonfatal_attribute(dw::at at) {
    static const auto attributes = []{
        std::vector<dw::at> nonfatal_attributes = {
            dw::at::apple_block,
            dw::at::apple_flags,
            dw::at::apple_isa,
            dw::at::apple_major_runtime_vers,
            dw::at::apple_objc_complete_type,
            dw::at::apple_objc_direct,
            dw::at::apple_omit_frame_ptr,
            dw::at::apple_optimized,
            dw::at::apple_property,
            dw::at::apple_property_attribute,
            dw::at::apple_property_getter,
            dw::at::apple_property_name,
            dw::at::apple_property_setter,
            dw::at::apple_runtime_class,
            dw::at::apple_sdk,
            dw::at::call_column,
            dw::at::call_file,
            dw::at::call_line,
            dw::at::call_origin,
            dw::at::call_return_pc,
            dw::at::containing_type,
            dw::at::decl_column,
            dw::at::decl_file,
            dw::at::decl_line,
            dw::at::frame_base,
            // According to section 2.17 of the DWARF spec, if high_pc is a constant (e.g., form
            // data4) then its value is the size of the function. Likewise, its existence implies
            // the function it describes is a contiguous block of code in the object file. Since we
            // assume this attribute is of constant form, this is the size of the function. If two
            // or more functions with the same name have different high_pc values, their sizes are
            // different, which means their definitions are going to be different, and that's an
            // ODRV.
            // dw::at::high_pc,
            dw::at::location,
            dw::at::low_pc,
            dw::at::name,
            dw::at::prototyped,
        };

        std::sort(nonfatal_attributes.begin(), nonfatal_attributes.end());

        return nonfatal_attributes;
    }();

    return sorted_has(attributes, at);
}

/**************************************************************************************************/

const die& lookup_die(const dies& dies, std::uint32_t offset) {
    auto found = std::lower_bound(dies.begin(), dies.end(), offset,
                                  [](const auto& x, const auto& offset){
        return x._debug_info_offset < offset;
    });
    bool match = found != dies.end() && found->_debug_info_offset == offset;
    assert(match);
    if (!match) throw std::runtime_error("die not found");
    return *found;
}

/**************************************************************************************************/

const die& find_base_type(const dies& dies, const die& d) {
    if (!d.has_attribute(dw::at::type)) return d;
    return find_base_type(dies, lookup_die(dies, d.attribute_reference(dw::at::type)));
}

/**************************************************************************************************/

void resolve_reference_attributes(const dies& dies, die& d) { // REVISIT (fbrereto): d is an out-arg
    for (auto& attr : d) {
        if (attr._name == dw::at::type) continue;
        if (!attr.has(attribute_value::type::reference)) continue;
        const die& resolved = lookup_die(dies, attr.reference());
        attr._value.die(resolved);
        attr._value.string(resolved._path);
    }
}

/**************************************************************************************************/

void resolve_type_attribute(const dies& dies, die& d) { // REVISIT (fbrereto): d is an out-arg
    constexpr auto type_k = dw::at::type;

    if (!d.has_attribute(type_k)) return; // nothing to resolve
    if (d._type_resolved) return; // already resolved

    const die& base_type_die = find_base_type(dies, d);

    // Now that we have the resolved type die, overwrite this die's type to reflect
    // the resolved type.
    attribute& type_attr = d.attribute(type_k);
    type_attr._value.die(base_type_die);
    if (base_type_die.has_attribute(dw::at::name)) {
        type_attr._value.string(base_type_die.attribute_string(dw::at::name));
    }

    d._type_resolved = true;
}

/**************************************************************************************************/
bool type_equivalent(const attribute& x, const attribute& y);
dw::at find_die_conflict(const die& x, const die& y) {
    const auto& yfirst = y.begin();
    const auto& ylast = y.end();

    for (const auto& xattr : x) {
        auto name = xattr._name;
        if (nonfatal_attribute(name)) continue;

        auto yfound = std::find_if(yfirst, ylast, [&](auto& x) { return name == x._name; });
        if (yfound == ylast) continue; // return name;

        const auto& yattr = *yfound;

        if (name == dw::at::type && type_equivalent(xattr, yattr)) continue;
        else if (xattr == yattr) continue;

        return name;
    }

    // REVISIT (fbrereto) : There may be some attributes left over in y; should we care?
    // If they're not nonfatal attributes, we should...

    return dw::at::none; // they're "the same"
}

/**************************************************************************************************/

bool type_equivalent(const attribute& x, const attribute& y) {
    // types are pretty convoluted, so we pull their comparison out here in an effort to
    // keep it all in a developer's head.

    if (x.has(attribute_value::type::reference) &&
        y.has(attribute_value::type::reference) &&
        x.reference() == y.reference()) {
        return true;
    }

    if (x.has(attribute_value::type::string) &&
        y.has(attribute_value::type::string) &&
        x.string_hash() == y.string_hash()) {
        return true;
    }

    if (x.has(attribute_value::type::die) && y.has(attribute_value::type::die) &&
        find_die_conflict(x.die(), y.die()) == dw::at::none) {
        return true; // Should this change based on find_die_conflict's result?
    }

    // Type mismatch.
    return false;
}

/**************************************************************************************************/

bool skip_tagged_die(const die& d) {
    static const dw::tag skip_tags[] = {
        dw::tag::compile_unit,
        dw::tag::partial_unit,
        dw::tag::variable,
        dw::tag::formal_parameter,
    };
    static const auto first = std::begin(skip_tags);
    static const auto last = std::end(skip_tags);

    return std::find(first, last, d._tag) != last;
}

/**************************************************************************************************/

void enforce_odr(const std::string_view& symbol, const die& x, const die& y) {
#if 0
    if (x._path == "::[arm64]::size_type") {
        int x;
        (void)x;
    }
#endif

    auto conflict_name = find_die_conflict(x, y);

    if (conflict_name != dw::at::none) {
        record_odrv(symbol, x, y, conflict_name);
    }
}

/**************************************************************************************************/

bool skip_die(const dies& dies, die& d, const std::string_view& symbol) {
    // These are a handful of "filters" we use to elide false positives.

    // These are the tags we don't deal with (yet, if ever.)
    if (skip_tagged_die(d)) return true;

    // Empty path means the die (or an ancestor) is anonymous/unnamed. No need to register
    // them.
    if (d._path.empty()) return true;

    // Symbols with __ in them are reserved, so are not user-defined. No need to register
    // them.
    if (d._path.view().find("::__") != std::string::npos) return true;

    // lambdas are ephemeral and can't cause (hopefully) an ODRV
    if (d._path.view().find("lambda") != std::string::npos) return true;

    // we don't handle any die that's ObjC-based.
    if (d.has_attribute(dw::at::apple_runtime_class)) return true;

    // If the symbol is listed in the symbol_ignore list, we're done here.
    if (sorted_has(settings::instance()._symbol_ignore, symbol)) return true;

    // Unfortunately we have to do this work to see if we're dealing with
    // a self-referential type.
    if (d.has_attribute(dw::at::type)) {
        resolve_type_attribute(dies, d);
        const auto& type = d.attribute(dw::at::type);
        // if this is a self-referential type, it's die will have no attributes.
        if (type.die()._attributes_size == 0) return true;
    }

    return false;
}

/**************************************************************************************************/

void update_progress() {
    if (!settings::instance()._show_progress) return;

    std::size_t done = globals::instance()._die_analyzed_count;
    std::size_t total = globals::instance()._die_processed_count;
    std::size_t percentage = static_cast<double>(done) / total * 100;

    cout_safe([&](auto& s){
        s << '\r' << done << "/" << total << "  " << percentage << "%; ";
        s << globals::instance()._odrv_count << " violation(s) found";
        s << "          "; // 10 spaces of overprint to clear out any previous lingerers
    });
}

/**************************************************************************************************/

auto& unsafe_global_die_collection() {
#if ORC_FEATURE(LEAKY_MEMORY)
    static std::list<dies>* collection_s = new std::list<dies>;
    return *collection_s;
#else
    static std::list<dies> collection_s;
    return collection_s;
#endif
}

template <class F>
auto with_global_die_collection(F&& f) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    return f(unsafe_global_die_collection());
}

/**************************************************************************************************/

auto& global_die_map() {
    using map_type = tbb::concurrent_unordered_map<std::size_t, const die*>;

#if ORC_FEATURE(LEAKY_MEMORY)
    static map_type* map_s = new map_type;
    return *map_s;
#else
    static map_type map_s;
    return map_s;
#endif
}

/**************************************************************************************************/

void register_dies(dies die_vector) {
    // This is a list so the die vectors don't move about. The dies become pretty entangled as they
    // point to one another by reference, and the odr_map itself stores const pointers to the dies
    // it registers. Thus, we move our incoming die_vector to the end of this list, and all the
    // pointers we use will stay valid for the lifetime of the application.
    dies& dies = *with_global_die_collection([&](auto& collection){
        collection.push_back(std::move(die_vector));
        return --collection.end();
    });

    globals::instance()._die_processed_count += dies.size();

    for (auto& d : dies) {
        // save for debugging. Useful to watch for a specific symbol.
#if 0
        if (d._path == "::[u]::_ZNK14example_vtable6object3apiEv") {
            int x;
            (void)x;
        }
#endif

        auto symbol = path_to_symbol(d._path.view());
        auto should_skip = skip_die(dies, d, symbol);

        if (settings::instance()._print_symbol_paths) {
#if 0
            // This is all horribly broken, especially now that we're calling this from multiple threads.
            static pool_string last_object_file_s;

            cout_safe([&](auto& s){
                if (d._object_file != last_object_file_s) {
                    last_object_file_s = d._object_file;
                    s << '\n' << last_object_file_s << '\n';
                }

                s << (should_skip ? 'S' : 'R') << " - 0x";
                s.width(8);
                s.fill('0');
                s << std::hex << d._debug_info_offset << std::dec << " " << d._path << '\n';
            });
#endif
        }

        if (should_skip) continue;

        //
        // After this point, we KNOW we're going to register the die. Do additional work
        // necessary just for DIEs we're registering after this point.
        //

        resolve_reference_attributes(dies, d);

        auto result = global_die_map().insert(std::make_pair(d._hash, &d));
        if (result.second) {
            ++globals::instance()._die_registered_count;
            continue;
        }

        // possible violation - make sure everything lines up!

        enforce_odr(symbol, d, *result.first->second);
    }

    globals::instance()._die_analyzed_count += dies.size();

    update_progress();
}

/**************************************************************************************************/

struct cmdline_results {
    std::vector<std::filesystem::path> _file_object_list;
    bool _ld_mode{false};
    bool _libtool_mode{false};
};

/**************************************************************************************************/

struct work_counter {
    struct state {
        void increment() {
            {
            std::lock_guard<std::mutex> lock(_m);
            ++_n;
            }
            _c.notify_all();
        }

        void decrement() {
            {
            std::lock_guard<std::mutex> lock(_m);
            --_n;
            }
            _c.notify_all();
        }

        void wait() {
            std::unique_lock<std::mutex> lock(_m);
            if (_n == 0) return;
            _c.wait(lock, [&]{ return _n == 0; });
        }

        std::mutex _m;
        std::condition_variable _c;
        std::size_t _n{0};
    };

    using shared_state = std::shared_ptr<state>;

    friend struct token;

public:
    work_counter() : _impl{std::make_shared<state>()} {}

    struct token {
        token(shared_state w) : _w(std::move(w)) { _w->increment(); }
        token(const token& t) : _w{t._w}  { _w->increment(); }
        token(token&& t) = default;
        ~token() { if (_w) _w->decrement(); }
    private:
        shared_state _w;
    };

    auto working() { return token(_impl); }

    void wait() { _impl->wait(); }

private:
    shared_state _impl;

    void increment() { _impl->increment(); }
    void decrement() { _impl->decrement(); }
};

/**************************************************************************************************/

auto& work() {
    static work_counter _work;
    return _work;
}

/**************************************************************************************************/

void do_work(std::function<void()> f){
    auto doit = [](auto&& f){
        try {
            f();
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
        } catch (...) {
            std::cerr << "unknown exception caught" << '\n';
        }
    };

    if (!settings::instance()._parallel_processing) {
        doit(f);
        return;
    }

    static orc::task_system system;

    system([_work_token = work().working(), _doit = doit, _f = std::move(f)] {
        _doit(_f);
    });
}

/**************************************************************************************************/

const char* problem_prefix() { return settings::instance()._graceful_exit ? "warning" : "error"; }

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

std::string odrv_report::category() const {
    return to_string(_a._tag) + std::string(":") + to_string(_name);
}

/**************************************************************************************************/

pool_string odrv_report::attribute_string(dw::at name) const {
    if (!_a.has_attribute(name) || !_b.has_attribute(name)) {
        throw std::runtime_error(std::string("Missing attribute: ") + to_string(name));
    }

    if (!_a.attribute_has_string(name) || !_b.attribute_has_string(name)) {
        throw std::runtime_error(std::string("Attribute type mismatch: ") + to_string(name));
    }

    auto a_value = _a.attribute_string(name);
    auto b_value = _b.attribute_string(name);

    if (a_value != b_value) {
        throw std::runtime_error(std::string("Attribute value mismatch: ") + to_string(name));
    }

    return a_value;
}

/**************************************************************************************************/

std::ostream& operator<<(std::ostream& s, const odrv_report& report) {
    const std::string_view& symbol = report._symbol;
    const die& a = report._a;
    const die& b = report._b;
    auto& settings = settings::instance();
    std::string odrv_category(report.category());
    bool do_report{true};

    if (!settings._violation_ignore.empty()) {
        // Report everything except the stuff on the ignore list
        do_report = !sorted_has(settings._violation_ignore, odrv_category);
    } else if (!settings._violation_report.empty()) {
        // Report nothing except the the stuff on the report list
        do_report = sorted_has(settings._violation_report, odrv_category);
    }

    if (!do_report) return s;

    // The number of unique ODRVs is tricky.
    // If A, B, and C are types, and C is different, then if we scan:
    // A, B, then C -> 1 ODRV, but C, then A, B -> 2 ODRVs. Complicating
    // this is that (as this comment is written) we don't detect supersets.
    // So if C has more methods than A and B it may not be detected.
    if (settings._filter_redundant) {
        static std::set<std::size_t> unique_odrv_types;
        static std::mutex unique_odrv_mutex;
        
        std::lock_guard guard(unique_odrv_mutex);
        std::size_t symbol_hash = hash_combine(0, symbol, odrv_category);
        bool did_insert = unique_odrv_types.insert(symbol_hash).second;
        if (!did_insert) return s; // We have already reported an instance of this.
    }

    s << problem_prefix() << ": ODRV (" << odrv_category << "); conflict in `"
      << demangle(symbol.data()) << "`\n";
    s << a << '\n';
    s << b << '\n';
    s << "\n";

    ++globals::instance()._odrv_count;

    if (settings::instance()._max_violation_count > 0 &&
        globals::instance()._odrv_count >= settings::instance()._max_violation_count) {
        throw std::runtime_error("ODRV limit reached");
    }

    return s;
}

/**************************************************************************************************/

std::vector<odrv_report> orc_process(const std::vector<std::filesystem::path>& file_list) {
    for (const auto& input_path : file_list) {
        do_work([_input_path = input_path] {
            if (!exists(_input_path)) {
                throw std::runtime_error("file " + _input_path.string() + " does not exist");
            }

            freader input(_input_path);
            callbacks callbacks = {
                register_dies,
                do_work,
                empool,
            };

            parse_file(_input_path.string(),
                       object_ancestry(),
                       input,
                       input.size(),
                       std::move(callbacks));
        });
    }

    work().wait();

    // Instead of moving the records, we swap with an empty vector to keep the records well-formed
    // in case another orc processing pass is desired.
    std::vector<odrv_report> result;
    std::swap(result, unsafe_odrv_records());
    return result;
}

/**************************************************************************************************/

void orc_reset() {
    global_die_map().clear();
    with_global_die_collection([](auto& collection){
        collection.clear();
    });
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
