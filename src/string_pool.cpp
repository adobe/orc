// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/string_pool.hpp"

// stdc++
#include <cassert>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// application
#include "orc/features.hpp"
#include "orc/hash.hpp"

/*static*/ std::string_view pool_string::default_view("");

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

std::size_t string_view_hash(std::string_view s) {
    return orc::murmur3_64(s.data(), s.length());
}

/**************************************************************************************************/

// Data is backed and not aligned.
// Before the _data pointer:
//      'uint32_t' length of string
//      'size_t' hash
// The _data pointer is returned to a null terminated string, to make debugging easier
// get_size() and get_hash() unpack this data as needed.
//
struct pool {
    char* _p{nullptr};
    std::size_t _n{0};
    std::vector<std::unique_ptr<char[]>> _ponds;

    const char* empool(std::string_view incoming) {
        constexpr auto default_min_k = 16 * 1024 * 1024; // 16MB
        const uint32_t sz = (uint32_t)incoming.size();
        const uint32_t tsz = sz + sizeof(uint32_t) + sizeof(size_t) + 1;

        if (_n < tsz) {
            _n = std::max<std::size_t>(default_min_k, tsz);
            _ponds.push_back(std::make_unique<char[]>(_n));
            _p = _ponds.back().get();
        }
        const std::size_t h = string_view_hash(incoming);
        // Memory isn't aligned - need to memcpy to pack the data
        std::memcpy(_p, &sz, sizeof(uint32_t));
        std::memcpy(_p + sizeof(uint32_t), &h, sizeof(size_t));
        std::memcpy(_p + sizeof(uint32_t) + sizeof(size_t), incoming.data(), sz);
        *(_p + tsz - 1) = 0; // null terminate for debugging

        const char* result = _p + sizeof(uint32_t) + sizeof(size_t);
        _n -= tsz;
        _p += tsz;
        return result;
    }
};
} // namespace

/**************************************************************************************************/

std::size_t pool_string::get_size(const char* d) {
    assert(d);
    const void* bytes = d - sizeof(std::uint32_t) - sizeof(std::size_t);
    std::uint32_t s;
    std::memcpy(&s, bytes, sizeof(s)); // not aligned - need to use memcpy
    assert(s > 0);                     // required, else should have been _data == nullptr
    assert(s < 100000);                // sanity check
    return s;
}

std::size_t pool_string::get_hash(const char* d) {
    assert(d);
    const void* bytes = d - sizeof(std::size_t);
    std::size_t h;
    std::memcpy(&h, bytes, sizeof(h)); // not aligned -- need to use memcpy
    return h;
}

pool_string empool(std::string_view src) {
    // A pool_string is empty iff _data = nullptr
    // So this creates an empty pool_string (as opposed to an empty string_view, where
    // default_view would be returned.)
    if (src.empty()) return pool_string(nullptr);

    struct pool_key_to_hash {
        auto operator()(size_t key) const { return key; }
    };

    thread_local std::unordered_map<size_t, const char*> keys;
    thread_local pool the_pool;

    // Is the string interned already?
    const std::size_t h = string_view_hash(src);

    auto found = keys.find(h);
    if (found != keys.end()) {
        return pool_string(found->second);
    }

    // Not already interned; empool it and add to the 'keys'
    const char* ptr = the_pool.empool(src);
    keys[h] = ptr;
    return pool_string(ptr);
}

/**************************************************************************************************/
