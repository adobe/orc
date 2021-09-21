// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/string_pool.hpp"

// stdc++
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <cassert>

// application
#include "orc/features.hpp"

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

struct pool {
    char* _p;
    std::size_t _n{0};
    std::vector<std::unique_ptr<char[]>> _ponds;

    auto empool(std::string_view incoming) {
        constexpr auto default_min_k = 16 * 1024 * 1024; // 16MB
        const auto sz = incoming.size();
        const auto tsz = sz + 1;
        if (_n < tsz) {
            _n = std::max<std::size_t>(default_min_k, tsz);
            _ponds.push_back(std::make_unique<char[]>(_n));
            _p = _ponds.back().get();
        }
        std::memcpy(_p, incoming.data(), sz);
        std::string_view result(_p, sz);
        _n -= tsz;
        _p += sz;
        *_p++ = 0;
        return result;
    }
};

/**************************************************************************************************/

auto& pool() {
#if ORC_FEATURE(LEAKY_MEMORY)
    thread_local struct pool& instance = *(new struct pool());
#else
    thread_local struct pool instance;
#endif // ORC_FEATURE(LEAKY_MEMORY)

    return instance;
}

/**************************************************************************************************/

struct pool_string_hash {
    auto operator()(const pool_string& x) const {
        return x.hash();
    }
};

struct pool_string_equal_to {
    auto operator()(const pool_string& x, const pool_string& y) const {
        return x.hash() == y.hash();
    }
};

auto& set() {
    using set_type = std::unordered_set<pool_string, pool_string_hash, pool_string_equal_to>;

#if ORC_FEATURE(LEAKY_MEMORY)
    thread_local set_type& instance = *(new set_type());
#else
    thread_local set_type instance;
#endif

    return instance;
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

pool_string empool(std::string_view src) {
    auto& s = set();
    pool_string needle(src);
    auto found = s.find(needle);

    if (found != s.end()) {
        return *found;
    }

    pool_string empooled(pool().empool(src), needle.hash());
    assert(std::hash<std::string_view>{}(empooled._s) == empooled._h);

    auto result = s.insert(std::move(empooled));
    assert(result.second);

    return pool_string(*result.first);
}

/**************************************************************************************************/
