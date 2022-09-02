// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <array>

// application
#include "orc/semantic_type.hpp"
#include "orc/string_pool.hpp"

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

    const auto& back() const {
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

enum class arch : std::uint8_t {
    unknown,
    x86,
    x86_64,
    arm,
    arm64,
    arm64_32,
};

const char* to_string(arch arch); // in dwarf.cpp

/**************************************************************************************************/

struct file_details {
    enum class format {
        unknown,
        macho,
        ar,
        fat,
    };
    std::size_t _offset{0};
    format _format{format::unknown};
    arch _arch{arch::unknown};
    bool _is_64_bit{false};
    bool _needs_byteswap{false};
};

/**************************************************************************************************/

struct object_file_descriptor {
    object_ancestry _ancestry;
    file_details _details;
};

/**************************************************************************************************/

using ofd_index = orc::semantic<std::size_t, struct object_file_register_index>;

ofd_index object_file_register(object_ancestry&& ancestry, file_details&& details);

const object_file_descriptor& object_file_fetch(ofd_index index);

inline const object_ancestry& object_file_ancestry(ofd_index index) {
    return object_file_fetch(index)._ancestry;
}

inline const file_details& object_file_details(ofd_index index) {
    return object_file_fetch(index)._details;
}

/**************************************************************************************************/
