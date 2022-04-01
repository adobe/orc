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
#include <unordered_map>

// application
#include "orc/features.hpp"

/*static*/ std::string_view pool_string::default_view("");


/**************************************************************************************************/

namespace {

/**************************************************************************************************/


struct pool {
    char* _p;
    std::size_t _n{0};
    std::vector<std::unique_ptr<char[]>> _ponds;
    std::size_t _total_alloc = 0;

    const char* empool(std::string_view incoming) {
        constexpr auto default_min_k = 16 * 1024 * 1024; // 16MB
        const uint32_t sz = (uint32_t)incoming.size();
        const uint32_t tsz = sz + sizeof(uint32_t) + sizeof(size_t) + 1;
        
        if (_n < tsz) {
            _n = std::max<std::size_t>(default_min_k, tsz);
            _total_alloc += _n;
            printf("Total string pool mem: %zu\n", _total_alloc);
            _ponds.push_back(std::make_unique<char[]>(_n));
            _p = _ponds.back().get();
        }
        size_t h = std::hash<std::string_view>{}(incoming);
        std::memcpy(_p, &sz, sizeof(uint32_t));
        std::memcpy(_p + sizeof(uint32_t), &h, sizeof(size_t));
        std::memcpy(_p + sizeof(uint32_t) + sizeof(size_t), incoming.data(), sz);
        *(_p + tsz - 1) = 0;
        
        const char* result = _p + sizeof(uint32_t) + sizeof(size_t);
        _n -= tsz;
        _p += tsz;
        return result;
    }
};
 
/**************************************************************************************************/

/*
auto& pool() {
#if ORC_FEATURE(LEAKY_MEMORY)
    thread_local struct pool& instance = *(new struct pool());
#else
    thread_local struct pool instance;
#endif // ORC_FEATURE(LEAKY_MEMORY)

    return instance;
}
 */

/**************************************************************************************************/

struct pool_string_hash {
    auto operator()(const pool_string& x) const {
        return x.hash();
    }
};

struct pool_string_equal_to {
    auto operator()(const pool_string& x, const pool_string& y) const {
        return x == y;
    }
};

struct pool_key_to_hash {
    auto operator()(size_t key) const { return key;}
};

/*
auto& set() {
    using set_type = std::unordered_set<pool_string, pool_string_hash, pool_string_equal_to>;

#if ORC_FEATURE(LEAKY_MEMORY)
    thread_local set_type& instance = *(new set_type());
#else
    thread_local set_type instance;
#endif

    return instance;
}
*/

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

pool_string empool(std::string_view src) {
    if (src.empty())
        return pool_string(nullptr);
    
    static std::mutex mutex;
    std::lock_guard<std::mutex> guard(mutex);
    
    //using set_type = std::unordered_set<pool_string, pool_string_hash, pool_string_equal_to>;
    //static set_type s;
    static std::unordered_multimap<size_t, const char*, pool_key_to_hash> keys;
    static pool the_pool;
    
    // Is the string interned already?
    const size_t h = std::hash<std::string_view>{}(src);
    
    const auto range = keys.equal_range(h);
    for(auto it = range.first; it != range.second; ++it) {
        pool_string ps(it->second);
        if (ps.view() == src) {
            return ps; // check the string_view is equal! not the pool pointer.
        }
    }
    
    const char* ptr = the_pool.empool(src);
    assert(*ptr >= 32);
    keys.insert({{h, ptr}});
    if (memcmp(ptr, "XML_Size\\", 9) == 0)
        printf("d");
    return pool_string(ptr);
    
    /*
    
       
    pool_string needle(src);
    auto found = s.find(needle);

    if (found != s.end()) {
        return *found;
    }

    pool_string empooled(pool().empool(src), needle.hash());
    assert(std::hash<std::string_view>{}(empooled._s) == empooled._h);

    auto result = s.insert(std::move(empooled));
    assert(result.second);

    return pool_string(*result.first);*/
}

/**************************************************************************************************/
