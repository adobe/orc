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
#include <string>
#include <vector>

// stlab
#include <stlab/enum_ops.hpp>

// application
#include "orc/dwarf_constants.hpp"
#include "orc/freader.hpp"
#include "orc/hash.hpp"
#include "orc/object_file_registry.hpp"
#include "orc/string_pool.hpp"

/**************************************************************************************************/

struct deferred_string_descriptor {
    freader _s;
    // the offset to the start of the string (in the debug_str block) from the top of the file,
    // taking into account all other offsets, etc.
    std::size_t _offset{0};

    bool valid() const { return _s && _offset != 0; }

    explicit operator bool() const { return valid(); }

    pool_string resolve() const;
};

/**************************************************************************************************/
// This is intentionally not a union. The reason is because there are a lot of values that are
// binary encoded in DWARF, but then require further interpretation (such as references to other
// DIEs) or can be converted to human-readable strings. In those cases, it can be beneficial to
// have both values around (especially in the DIE reference case.)
    enum class attribute_value_type {
        none = 0,
        passover = 1 << 0,
        uint = 1 << 1,
        sint = 1 << 2,
        string = 1 << 3,
        string_deferred = 1 << 4,
        reference = 1 << 5,
        die = 1 << 6,
    };

auto stlab_enable_bitmask_enum(attribute_value_type) -> std::true_type;

inline auto has_type(attribute_value_type x, attribute_value_type y) {
    return (x & y) != attribute_value_type::none;
}

/**************************************************************************************************/

struct attribute_value {
    void passover() { _type = attribute_value_type::passover; }

    void uint(std::uint64_t x) {
        _type |= attribute_value_type::uint;
        _uint = x;
    }

    auto uint() const {
        assert(has(attribute_value_type::uint));
        return _uint;
    }

    void sint(std::int32_t x) {
        _type |= attribute_value_type::sint;
        _int = x;
    }

    auto sint() const {
        assert(has(attribute_value_type::sint));
        return _int;
    }

    void string(pool_string x) const {
        // if we are getting a string set, then we cannot be
        // string deferred, so clear the bit. This can be the
        // case e.g., when resolving a type, that starts out
        // deferred but then is resolved during type resolution.
        // assert(x);
        _type &= ~attribute_value_type::string_deferred;
        _type |= attribute_value_type::string;
        _string = x;
    }

    void string(deferred_string_descriptor descriptor) {
        assert(descriptor);
        // flag that we have a deferred *and* string
        // so `has` checks will succeed as intended.
        _type |= attribute_value_type::string;
        _type |= attribute_value_type::string_deferred;
        _descriptor = std::move(descriptor);
    }

    pool_string string() const {
        // check deferred case first
        if (has(attribute_value_type::string_deferred)) string(_descriptor.resolve());
        assert(has(attribute_value_type::string));
        return _string;
    }

    auto string_hash() const {
        assert(has(attribute_value_type::string));
        return _string.hash();
    }

    void reference(std::uint32_t offset) {
        _type |= attribute_value_type::reference;
        _uint = offset;
    }

    auto reference() const {
        assert(has(attribute_value_type::reference));
        return _uint;
    }

    void die(const struct die& d) {
        _type |= attribute_value_type::die;
        _die = &d;
    }

    const auto& die() const {
        assert(has(attribute_value_type::die));
        return *_die;
    }

    std::size_t hash() const;

    auto type() const { return _type; }
    bool has(attribute_value_type t) const { return has_type(type(), t); }
    bool has_none() const { return has(attribute_value_type::none); }
    bool has_passover() const { return has(attribute_value_type::passover); }
    bool has_uint() const { return has(attribute_value_type::uint); }
    bool has_sint() const { return has(attribute_value_type::sint); }
    bool has_string() const { return has(attribute_value_type::string); }
    bool has_reference() const { return has(attribute_value_type::reference); }
    bool has_die() const { return has(attribute_value_type::die); }

private:
    friend bool operator==(const attribute_value& x, const attribute_value& y);

    mutable attribute_value_type _type{attribute_value_type::none};
    std::uint64_t _uint{0};
    std::int64_t _int{0};
    mutable pool_string _string; // also used as a cache for deferred string resolve
    deferred_string_descriptor _descriptor;
    const struct die* _die{nullptr};
};

inline bool operator==(const attribute_value& x, const attribute_value& y) {
    // we do string first, as there are references/dies that "resolve" to
    // some string value, and if we can compare that, we should.
    if (x.has(attribute_value_type::string)) return x._string == y._string;
    if (x.has(attribute_value_type::uint)) return x._uint == y._uint;
    if (x.has(attribute_value_type::sint)) return x._int == y._int;

    // we cannot compare references, as they are offsets into specific
    // __debug_info blocks that the two DIEs may not share.
    // if (has(x._type, attribute_value::type::reference)) return x._uint == y._uint;

    // Can we compare DIEs here, taking into account the usual nonfatal attributes, etc.?
    // if (has(x._type, attribute_value::type::die)) return x._die == y._die;

    return x._type == y._type;
}

std::ostream& operator<<(std::ostream& s, const attribute_value& x);

/**************************************************************************************************/

struct attribute {
    dw::at _name{0};
    dw::form _form{0};
    attribute_value _value;

    void read(freader& s);

    auto has(attribute_value_type t) const { return _value.has(t); }

    auto reference() const { return _value.reference(); }
    pool_string string() const { return _value.string(); }
    auto uint() const { return _value.uint(); }
    auto string_hash() const { return _value.string_hash(); }
    const auto& die() const { return _value.die(); }
};

inline bool operator==(const attribute& x, const attribute& y) {
    return x._name == y._name && x._form == y._form && x._value == y._value;
}

inline bool operator!=(const attribute& x, const attribute& y) { return !(x == y); }

std::ostream& operator<<(std::ostream& s, const attribute& x);

/**************************************************************************************************/
// I'm not a fan of this name.
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

    bool has(dw::at name, attribute_value_type t) const {
        auto [valid, iterator] = find(name);
        return valid ? iterator->has(t) : false;
    }

    bool has_uint(dw::at name) const {
        return has(name, attribute_value_type::uint);
    }

    bool has_string(dw::at name) const {
        return has(name, attribute_value_type::string);
    }

    bool has_reference(dw::at name) const {
        return has(name, attribute_value_type::reference);
    }

    auto& get(dw::at name) {
        auto [valid, iterator] = find(name);
        assert(valid);
        return *iterator;
    }

    const auto& get(dw::at name) const {
        auto [valid, iterator] = find(name);
        assert(valid);
        return *iterator;
    }

    std::size_t hash(dw::at name) const {
        return get(name)._value.hash();
    }

    std::uint64_t uint(dw::at name) const {
        return get(name).uint();
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

    auto begin() { return _attributes.begin(); }
    auto begin() const { return _attributes.begin(); }
    auto end() { return _attributes.end(); }
    auto end() const { return _attributes.end(); }

private:
    std::tuple<bool, iterator> find(dw::at name) {
        auto result = std::find_if(_attributes.begin(), _attributes.end(), [&](const auto& attr){
            return attr._name == name;
        });
        return std::make_tuple(result != _attributes.end(), result);
    }

    std::tuple<bool, const_iterator> find(dw::at name) const {
        auto result = std::find_if(_attributes.begin(), _attributes.end(), [&](const auto& attr){
            return attr._name == name;
        });
        return std::make_tuple(result != _attributes.end(), result);
    }

    attributes_type _attributes;
};

std::ostream& operator<<(std::ostream& s, const attribute_sequence& x);

/**************************************************************************************************/
// A die is constructed by reading an abbreviation entry, then filling in the abbreviation's
// attribute values with data taken from _debug_info. Thus it is possible for more than one die to
// use the same abbreviation, but because the die is listed in a different place in the debug_info
// data block, it's values will be different than previous "stampings" of the abbreviation.
struct die {
    // Because the quantity of these created at runtime can beon the order of millions of instances,
    // these are ordered for optimal alignment. If you change the ordering, or add/remove items
    // here, please consider alignment issues.
    pool_string _path;
    die* _next_die{nullptr};
    std::size_t _hash{0};
    std::size_t _fatal_attribute_hash{0};
    ofd_index _ofd_index{0};
    std::uint32_t _debug_info_offset{0}; // relative from top of __debug_info
    dw::tag _tag{dw::tag::none};
    bool _has_children{false};
    bool _conflict{false};
    bool _skippable{false};

    friend bool operator<(const die& x, const die& y);
};

std::ostream& operator<<(std::ostream& s, const die& x);

using dies = std::vector<die>;

/**************************************************************************************************/

bool nonfatal_attribute(dw::at at);

/**************************************************************************************************/

template <class Container, class T>
bool sorted_has(const Container& c, const T& x) {
    auto found = std::lower_bound(c.begin(), c.end(), x);
    return found != c.end() && *found == x;
}

/**************************************************************************************************/
// Quick and dirty type to print an integer value as a padded, fixed-width hex value.
// e.g., std::cout << hex_print(my_int) << '\n';
struct hex_print {
    explicit hex_print(std::size_t x) : _x{x} {}
    std::size_t _x;
};

inline std::ostream& operator<<(std::ostream& s, const hex_print& x) {
    s << "0x";
    s.width(8);
    s.fill('0');
    return s << std::hex << x._x << std::dec;
}

/**************************************************************************************************/
