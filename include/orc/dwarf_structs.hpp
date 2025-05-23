// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// adobe contract checks
#include "adobe/contract_checks.hpp"

// application
#include "orc/dwarf_constants.hpp"
#include "orc/fixed_vector.hpp"
#include "orc/hash.hpp"
#include "orc/string_pool.hpp"

//--------------------------------------------------------------------------------------------------

struct freader;

//--------------------------------------------------------------------------------------------------
// This is intentionally not a union. The reason is because there are a lot of values that are
// binary encoded in DWARF, but then require further interpretation (such as references to other
// DIEs) or can be converted to human-readable strings. In those cases, it can be beneficial to
// have both values around (especially in the DIE reference case.)
struct attribute_value {
    enum class type {
        none = 0,
        passover = 1 << 0,
        uint = 1 << 1,
        sint = 1 << 2,
        string = 1 << 3,
        reference = 1 << 4,
        die = 1 << 5,
    };
    using ut = typename std::underlying_type<type>::type;

    friend auto operator|=(type& x, const type& y) {
        return reinterpret_cast<enum type&>(reinterpret_cast<ut&>(x) |=
                                            reinterpret_cast<const ut&>(y));
    }
    friend auto has_type(type x, type y) { return (static_cast<ut>(x) & static_cast<ut>(y)) != 0; }

    void passover() { _type = type::passover; }

    void uint(std::uint64_t x) {
        _type |= type::uint;
        _uint = x;
    }

    auto uint() const {
        ADOBE_PRECONDITION(has(type::uint));
        return _uint;
    }

    void sint(std::int32_t x) {
        _type |= type::sint;
        _int = x;
    }

    auto sint() const {
        ADOBE_PRECONDITION(has(type::sint));
        return _int;
    }

    // Return _either_ sint or uint; some attributes
    // may be one or the other, but in some cases the
    // valid values could be represented by either type
    // (e.g., the number cannot be negative or larger
    // than the largest possible signed value.)
    // This routine is useful when the caller doesn't
    // care how it was stored and just wants the value.
    // If this attribute value has _both_, it is assumed
    // they are equal.
    int number() const {
        return has(type::sint) ? static_cast<int>(sint()) : static_cast<int>(uint());
    }
    
    void string(pool_string x) {
        _type |= type::string;
        _string = x;
    }

    const auto& string() const {
        ADOBE_PRECONDITION(has(type::string));
        return _string;
    }

    auto string_hash() const {
        ADOBE_PRECONDITION(has(type::string));
        return _string.hash();
    }

    void reference(std::uint64_t offset) {
        _type |= type::reference;
        _uint = offset;
    }

    auto reference() const {
        ADOBE_PRECONDITION(has(type::reference));
        return _uint;
    }

    std::size_t hash() const;

    auto type() const { return _type; }
    bool has(enum type t) const { return has_type(type(), t); }
    bool has_none() const { return has(type::none); }
    bool has_passover() const { return has(type::passover); }
    bool has_uint() const { return has(type::uint); }
    bool has_sint() const { return has(type::sint); }
    bool has_string() const { return has(type::string); }
    bool has_reference() const { return has(type::reference); }

private:
    friend bool operator==(const attribute_value& x, const attribute_value& y);

    enum type _type { type::none };
    std::uint64_t _uint{0};
    std::int64_t _int{0};
    pool_string _string;
};

inline bool operator==(const attribute_value& x, const attribute_value& y) {
    // we do string first, as there are references/dies that "resolve" to
    // some string value, and if we can compare that, we should.
    if (x.has(attribute_value::type::string)) return x._string == y._string;
    if (x.has(attribute_value::type::uint)) return x._uint == y._uint;
    if (x.has(attribute_value::type::sint)) return x._int == y._int;

    // we cannot compare references, as they are offsets into specific
    // __debug_info blocks that the two DIEs may not share.
    // if (has(x._type, attribute_value::type::reference)) return x._uint == y._uint;

    // Can we compare DIEs here, taking into account the usual nonfatal attributes, etc.?
    // if (has(x._type, attribute_value::type::die)) return x._die == y._die;

    return x._type == y._type;
}

std::ostream& operator<<(std::ostream& s, const attribute_value& x);

//--------------------------------------------------------------------------------------------------

struct attribute {
    dw::at _name{0};
    dw::form _form{0};
    attribute_value _value;

    void read(freader& s);

    auto has(enum attribute_value::type t) const { return _value.has(t); }

    auto reference() const { return _value.reference(); }
    const auto& string() const { return _value.string(); }
    auto uint() const { return _value.uint(); }
    auto sint() const { return _value.sint(); }
    auto string_hash() const { return _value.string_hash(); }
};

inline bool operator==(const attribute& x, const attribute& y) {
    return x._name == y._name && x._form == y._form && x._value == y._value;
}

inline bool operator!=(const attribute& x, const attribute& y) { return !(x == y); }

std::ostream& operator<<(std::ostream& s, const attribute& x);

//--------------------------------------------------------------------------------------------------
// I'm not a fan of the name `attribute_sequence`.
//
// TODO: Consider using `std::array` instead of `std::vector` to avoid dynamic allocation. This
// would require we cap the max number of attributes at compile time, which should be okay as long
// as we pick a reasonable number. On the other hand, that would make DIEs with smaller sets of
// attributes less memory efficient. It's the classic space/time tradeoff.
struct attribute_sequence {
    using attributes_type = std::vector<attribute>;
    using value_type = typename attributes_type::value_type;
    using iterator = typename attributes_type::iterator;
    using const_iterator = typename attributes_type::const_iterator;

    void reserve(std::size_t size) {
        _attributes.reserve(size);
    }

    bool has(dw::at name) const {
        auto [valid, iterator] = find(name);
        return valid;
    }

    bool has(dw::at name, enum attribute_value::type t) const {
        auto [valid, iterator] = find(name);
        return valid && iterator->has(t);
    }

    bool has_uint(dw::at name) const {
        return has(name, attribute_value::type::uint);
    }

    bool has_string(dw::at name) const {
        return has(name, attribute_value::type::string);
    }

    bool has_reference(dw::at name) const {
        return has(name, attribute_value::type::reference);
    }

    auto& get(dw::at name) {
        auto [valid, iterator] = find(name);
        ADOBE_INVARIANT(valid);
        return *iterator;
    }

    const auto& get(dw::at name) const {
        auto [valid, iterator] = find(name);
        ADOBE_INVARIANT(valid);
        return *iterator;
    }

    std::size_t hash(dw::at name) const {
        return get(name)._value.hash();
    }

    std::uint64_t uint(dw::at name) const {
        return get(name).uint();
    }

    int number(dw::at name) const {
        return get(name)._value.number();
    }

    std::int64_t sint(dw::at name) const {
        return get(name).sint();
    }

    pool_string string(dw::at name) const {
        return get(name).string();
    }

    std::uint64_t reference(dw::at name) const {
        return get(name).reference();
    }

    void push_back(const value_type& x) {
        _attributes.push_back(x);
    }

    bool empty() const { return _attributes.empty(); }

    auto size() const { return _attributes.size(); }

    auto begin() { return _attributes.begin(); }
    auto begin() const { return _attributes.begin(); }
    auto end() { return _attributes.end(); }
    auto end() const { return _attributes.end(); }

    void erase(dw::at name) {
        auto [valid, iterator] = find(name);
        ADOBE_INVARIANT(valid);
        _attributes.erase(iterator);
    }

    void move_append(attribute_sequence&& rhs) {
        _attributes.insert(_attributes.end(), std::move_iterator(rhs.begin()), std::move_iterator(rhs.end()));
    }

private:
    /// NOTE: Consider sorting these attribues by `dw::at` to improve performance.
    std::tuple<bool, iterator> find(dw::at name) {
        auto result = std::find_if(_attributes.begin(), _attributes.end(), [&](const auto& attr){
            return attr._name == name;
        });
        return std::make_tuple(result != _attributes.end(), result);
    }

    /// NOTE: Consider sorting these attribues by `dw::at` to improve performance.
    std::tuple<bool, const_iterator> find(dw::at name) const {
        auto result = std::find_if(_attributes.begin(), _attributes.end(), [&](const auto& attr){
            return attr._name == name;
        });
        return std::make_tuple(result != _attributes.end(), result);
    }

    attributes_type _attributes;
};

std::ostream& operator<<(std::ostream& s, const attribute_sequence& x);

//--------------------------------------------------------------------------------------------------
/**
 * @brief Represents a source code location in a file
 *
 * This structure stores information about a specific location in source code,
 * typically used to identify where a symbol is defined or declared in DWARF debug info.
 */
struct location {
    pool_string file; /// The source file path or name
    std::uint64_t loc{0}; /// The 1-indexed line number within the file
};

inline bool operator==(const location& x, const location& y) {
    return x.file == y.file && x.loc == y.loc;
}
inline bool operator!=(const location& x, const location& y) {
    return !(x == y);
}
inline bool operator<(const location& x, const location& y) {
    return x.file.hash() < y.file.hash() || (x.file == y.file && x.loc < y.loc);
}

template <>
struct std::hash<location> {
    std::size_t operator()(const location& x) const {
        return orc::hash_combine(x.file.hash(), x.loc);
    }
};

std::ostream& operator<<(std::ostream&, const location&);

//--------------------------------------------------------------------------------------------------
/**
 * @brief Derives the source code location where a symbol is defined
 *
 * This function extracts location information from a DIE's attributes to determine
 * where a symbol is defined in the source code. It primarily looks for `DW_AT_decl_file
 * and DW_AT_decl_line attributes, but may fall back to other location-related attributes
 * if the primary ones aren't available.
 *
 * @param x The attribute sequence from which to derive the location information
 *
 * @return An optional `location structure containing the file and line information,
 *         or `std::nullopt if no valid location information could be found
 *
 * @pre The attribute sequence must be properly initialized
 * @post If a valid location is found, the returned optional contains a location with
 *       non-empty file name and a line number greater than 0
 */
std::optional<location> derive_definition_location(const attribute_sequence& x);

//--------------------------------------------------------------------------------------------------

enum class arch : std::uint8_t {
    unknown,
    x86,
    x86_64,
    arm,
    arm64,
    arm64_32,
};

const char* to_string(arch arch);

//--------------------------------------------------------------------------------------------------
/**
 * @brief Represents the ancestry of an object file
 *
 * Object files can be stored within an arbitrarily nested set of archive formats. For example,
 * the `.o` file may be stored within an archive (`.a`) file, which itself may be stored within another
 * archive, etc. This structure keeps track of the file(s) that contain the object file in
 * question. This facilitates reporting when ODRVs are found, giving the user a breadcrumb as
 * to how the ODRV is being introduced. For efficiency purposes, we fix the max number of ancestors
 * at compile time, but this can be adjusted if necessary.
 */
struct object_ancestry {
    orc::fixed_vector<pool_string, 5> _ancestors;

    auto size() const { return _ancestors.size(); }
    auto begin() const { return _ancestors.begin(); }
    auto end() const { return _ancestors.end(); }

    auto& back() {
        assert(!_ancestors.empty());
        return _ancestors.back();
    }

    const auto& back() const {
        assert(!_ancestors.empty());
        return _ancestors.back();
    }

    void emplace_back(pool_string&& ancestor) {
        assert(_ancestors.size() < _ancestors.capacity());
        _ancestors.push_back(std::move(ancestor));
    }

    bool operator<(const object_ancestry& rhs) const {
        if (_ancestors.size() < rhs._ancestors.size())
            return true;

        if (_ancestors.size() > rhs._ancestors.size())
            return false;

        for (size_t i = 0; i < _ancestors.size(); ++i) {
            if (_ancestors[i].view() < rhs._ancestors[i].view())
                return true;

            if (_ancestors[i].view() > rhs._ancestors[i].view())
                return false;
        }

        return false;
    }
};

std::ostream& operator<<(std::ostream& s, const object_ancestry& x);

//--------------------------------------------------------------------------------------------------
// DIE is an acronym for "Debug Information Entry". It is the basic unit of information in DWARF.
//
// A DIE is constructed by reading an abbreviation entry, then filling in the abbreviation's
// attribute values with data taken from `_debug_info`. Thus it is possible for more than one DIE to
// use the same abbreviation, but because the DIE is listed in a different place in the `debug_info`
// data block, its values will be different than previous "stampings" of the abbreviation.
//
// A NOTE ON ADDRESS V. OFFSET (because I keep confusing myself)
//     * An ADDRESS is absolute relative to the _top of the file_. Address-based variables
//       are always relative to the top of the file, so need no additional annotation.
//     * An OFFSET is relative to either `__debug_info` or the start of the compilation unit
//       (whose offset is relative to `__debug_info`.) Offsets should always be annotated with
//       what their value is relative to.
// All DWARF/DIE/scanning related variables should follow the above conventions.
//
// During an ORC scan, multiple translation units worth of DIEs are brought together to determine
// if any of them violate the One Definition Rule. DIEs across those units that are "the same" will
// have the same `_hash` value, and will be linked together via the `_next_die` pointer. The top-level
// ORC scan will then have a collection of singly-linked lists, one per unique symbol / `_hash`.
// Once all these lists are constructed, each are checked individually for ODRVs.
struct die {
    // Because the quantity of these created at runtime can beon the order of millions of instances,
    // these are ordered for optimal alignment. If you change the ordering, or add/remove items
    // here, please consider alignment issues.
    pool_string _path; // the user-readable symbol name, "pathed"/namespaced by containing DIEs. May be mangled.
    die* _next_die{nullptr}; // pointer to the next DIE that has the same `_hash` value.
    std::optional<location> _location; // file_decl and file_line, if they exist for the DIE.
    std::size_t _hash{0}; // uniquely identifies the DIE across differing targets (e.g., the same symbol in a FAT binary.)
    std::size_t _fatal_attribute_hash{0}; // within a target, a hash of attributes that contribute to ODRVs.
    std::uint32_t _ofd_index{0}; // object file descriptor index
    std::size_t _cu_header_offset{0}; // offset to the compilation unit that contains this DIE; relative to `__debug_info`
    std::size_t _cu_die_offset{0}; // offset to the associated compilation unit DIE entry; relative to `__debug_info`
    std::size_t _offset{0}; // offset of this DIE; relative to `__debug_info`
    dw::tag _tag{dw::tag::none};
    arch _arch{arch::unknown};
    bool _has_children{false};
    bool _conflict{false};
    bool _skippable{false};

    friend bool operator<(const die& x, const die& y);
};

std::ostream& operator<<(std::ostream& s, const die& x);

using dies = std::vector<die>;

//--------------------------------------------------------------------------------------------------

/**
 * @brief Determines if a DWARF attribute is considered non-fatal for ODRV purposes
 * 
 * This function identifies attributes that can be safely ignored when checking for
 * One Definition Rule Violations (ODRVs). These attributes typically contain
 * information that doesn't affect the actual definition of a symbol, such as
 * debug-specific metadata or compiler-specific extensions.
 *
 * @param at The DWARF attribute to check
 * 
 * @return true if the attribute is non-fatal and can be ignored for ODRV checks,
 *         false if the attribute must be considered when checking for ODRVs
 * 
 * @pre The attribute must be a valid DWARF attribute
 * @post The return value will be consistent with the internal list of nonfatal attributes
 */
bool nonfatal_attribute(dw::at at);
inline bool fatal_attribute(dw::at at) { return !nonfatal_attribute(at); }

//--------------------------------------------------------------------------------------------------

template <class Container, class T>
bool sorted_has(const Container& c, const T& x) {
    auto found = std::lower_bound(c.begin(), c.end(), x);
    return found != c.end() && *found == x;
}

//--------------------------------------------------------------------------------------------------
// Quick and dirty type to print an integer value as a padded, fixed-width hex value.
// e.g., std::cout << hex_print(my_int) << '\n';
template <class Integral>
struct hex_print_t {
    explicit hex_print_t(Integral x) : _x{x} {}
    Integral _x;
};

template <class Integral>
auto hex_print(Integral x) {
    return hex_print_t<Integral>(x);
}

struct flag_saver {
    explicit flag_saver(std::ios_base& s) : _s{s}, _f{_s.flags()} {}
    ~flag_saver() { _s.setf(_f); }

private:
    std::ios_base& _s;
    std::ios_base::fmtflags _f;
};

template <class Integral>
inline std::ostream& operator<<(std::ostream& s, const hex_print_t<Integral>& x) {
    constexpr auto width_k = sizeof(x._x) * 2 + 2; // +2 to the width for std::showbase
    flag_saver fs(s);
    return s << std::internal << std::showbase << std::hex << std::setw(width_k)
             << std::setfill('0') << x._x;
}

//--------------------------------------------------------------------------------------------------
