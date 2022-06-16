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

// application
#include "orc/dwarf_constants.hpp"
#include "orc/string_pool.hpp"

/**************************************************************************************************/

struct freader;

/**************************************************************************************************/
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
        assert(has(type::uint));
        return _uint;
    }

    void sint(std::int32_t x) {
        _type |= type::sint;
        _int = x;
    }

    auto sint() const {
        assert(has(type::sint));
        return _int;
    }

    void string(pool_string x) {
        _type |= type::string;
        _string = x;
    }

    const auto& string() const {
        assert(has(type::string));
        return _string;
    }

    auto string_hash() const {
        assert(has(type::string));
        return _string.hash();
    }

    void reference(std::uint32_t offset) {
        _type |= type::reference;
        _uint = offset;
    }

    auto reference() const {
        assert(has(type::reference));
        return _uint;
    }

    void die(const struct die& d) {
        _type |= type::die;
        _die = &d;
    }

    const auto& die() const {
        assert(has(type::die));
        return *_die;
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
    bool has_die() const { return has(type::die); }

private:
    friend bool operator==(const attribute_value& x, const attribute_value& y);

    enum type _type { type::none };
    std::uint64_t _uint{0};
    std::int64_t _int{0};
    pool_string _string;
    const struct die* _die{nullptr};
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

/**************************************************************************************************/

struct attribute {
    dw::at _name{0};
    dw::form _form{0};
    attribute_value _value;

    void read(freader& s); // definition in dwarf.cpp

    auto has(enum attribute_value::type t) const { return _value.has(t); }

    auto reference() const { return _value.reference(); }
    const auto& string() const { return _value.string(); }
    auto string_hash() const { return _value.string_hash(); }
    const auto& die() const { return _value.die(); }
};

inline bool operator==(const attribute& x, const attribute& y) {
    return x._name == y._name && x._form == y._form && x._value == y._value;
}

inline bool operator!=(const attribute& x, const attribute& y) { return !(x == y); }

std::ostream& operator<<(std::ostream& s, const attribute& x);

/**************************************************************************************************/

enum class arch : std::uint8_t {
    unknown,
    x86,
    x86_64,
    arm,
    arm64,
    arm64_32,
};

const char* to_string(arch arch);

/**************************************************************************************************/

struct object_ancestry {
    std::array<pool_string, 5> _ancestors;
    std::size_t _count{0};

    auto begin() const { return _ancestors.begin(); }
    auto end() const { return begin() + _count; }
    auto& back() {
        assert(_count);
        return _ancestors[_count];
    }

    void emplace_back(pool_string&& ancestor) {
        assert((_count + 1) < _ancestors.size());
        _ancestors[_count++] = std::move(ancestor);
    }

    bool operator<(const object_ancestry& rhs) const {
        if (_count < rhs._count)
            return true;
        if (_count > rhs._count)
            return false;
        for(size_t i=0; i<_count; ++i) {
            if (_ancestors[i].view() < rhs._ancestors[i].view())
                return true;
            if (_ancestors[i].view() > rhs._ancestors[i].view())
                return false;
        }
        return false;
    }
};

/**************************************************************************************************/
// A die is constructed by reading an abbreviation entry, then filling in the abbreviation's
// attribute values with data taken from _debug_info. Thus it is possible for more than one die to
// use the same abbreviation, but because the die is listed in a different place in the debug_info
// data block, it's values will be different than previous "stampings" of the abbreviation.
struct die {
    // Because the quantity of these created at runtime can beon the order of millions of instances,
    // these are ordered for optimal alignment. If you change the ordering, or add/remove items
    // here, please consider alignment issues.
    object_ancestry _ancestry;
    pool_string _path;
    attribute* _attributes{nullptr};
    die* _next_die{nullptr};
    std::size_t _hash{0};
    std::uint32_t _debug_info_offset{0}; // relative from top of __debug_info
    std::uint32_t _attributes_size{0};
    dw::tag _tag{dw::tag::none};
    arch _arch{arch::unknown};
    bool _has_children{false};
    bool _type_resolved{false};
    bool _conflict{false};

    bool operator<(const die& rhs) const {
        if (_path.view() < rhs._path.view())
            return true;
        if (_path.view() > rhs._path.view())
            return false;
        return _ancestry < rhs._ancestry;
    }
            

    auto begin() { return _attributes; }
    auto begin() const { return _attributes; }
    auto end() { return _attributes + _attributes_size; }
    auto end() const { return _attributes + _attributes_size; }

    bool has_attribute(dw::at at) const {
        for (const auto& a : *this)
            if (a._name == at) return true;
        return false;
    }

    const auto& attribute(dw::at at) const {
        for (const auto& a : *this)
            if (a._name == at) return a;
        throw std::runtime_error(std::string("missing attribute: ") + to_string(at));
    }

    auto& attribute(dw::at at) {
        for (auto& a : *this)
            if (a._name == at) return a;
        throw std::runtime_error(std::string("missing attribute: ") + to_string(at));
    }

    bool attribute_has_type(dw::at at, enum attribute_value::type type) const {
        for (const auto& a : *this)
            if (a._name == at) return a.has(type);
        return false;
    }

    bool attribute_has_uint(dw::at at) const {
        return attribute_has_type(at, attribute_value::type::uint);
    }
    bool attribute_has_string(dw::at at) const {
        return attribute_has_type(at, attribute_value::type::string);
    }
    bool attribute_has_reference(dw::at at) const {
        return attribute_has_type(at, attribute_value::type::reference);
    }
    bool attribute_has_die(dw::at at) const {
        return attribute_has_type(at, attribute_value::type::die);
    }

    auto attribute_uint(dw::at at) const { return attribute(at)._value.uint(); }
    const auto& attribute_string(dw::at at) const { return attribute(at)._value.string(); }
    auto attribute_reference(dw::at at) const { return attribute(at)._value.reference(); }
    const auto& attribute_die(dw::at at) const { return attribute(at)._value.die(); }
};

std::ostream& operator<<(std::ostream& s, const die& x);

using dies = std::vector<die>;

/**************************************************************************************************/
