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
    char* _p{nullptr};
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

// The key is the hash; map one to the other.
struct pool_key_to_hash {
    auto operator()(size_t key) const { return key;}
};

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

pool_string empool(std::string_view src) {
    if (src.empty())
        return pool_string(nullptr);
    
    thread_local std::unordered_multimap<size_t, const char*, pool_key_to_hash> keys;
    thread_local pool the_pool;
    
    // Is the string interned already?
    const size_t h = std::hash<std::string_view>{}(src);
    
    const auto range = keys.equal_range(h);
    for(auto it = range.first; it != range.second; ++it) {
        pool_string ps(it->second);
        if (ps.view() == src) {
            return ps;
        }
    }
    
    // Not already interned; empool it and add to the 'keys' 
    const char* ptr = the_pool.empool(src);
    keys.insert({{h, ptr}});
    return pool_string(ptr);
}

/**************************************************************************************************/
