// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <cassert>
#include <ostream>
#include <string>
#include <string_view>
#include <filesystem>

#include "orc/features.hpp"

//--------------------------------------------------------------------------------------------------

struct pool_string;

/*
    Stores interned strings. Thread safe in that the pool resources are per thread. 

    A string pool per thread reduces the total memory usage from 83GB to 53GB. It also
    significantly improves performance. (This is the result for the application as a whole. That
    a per-thread memory pool works so well is perhaps counter-intuitive.)
*/
pool_string empool(std::string_view src);

/*
    A pool_string is an interned string. Once created, the pointer `_data` is immutable for the
    life of the application. All the pool_strings are stored in one pool, so they are unique.
    This has some useful properties.

    * A `pool_string` is one pointer in size
    * A `pool_string` is thread safe
    * Two `pool_strings` pointing to the same `_data` are always equal, and if the `_data` is different,
      they are not equal.<
    * `_data` is a char* to null terminated data, which is just a c string. Useful for debugging.
    * if `_data` is null, it is intepreted as an empty string (""). If `_data` is not-null, it always is size() > 0
    
    When empooled, the hash (64 bits) and size/length (32 bits) are stored before the char* to the data.
    Note there is no memory alignment in the pool - it is fully packed - so data needs to be memcpy'd in
    and out, just in case the processor doesn't like un-aligned reads.
*/
struct pool_string {
    pool_string() = default;
    ~pool_string() = default;

    bool empty() const { return _data == nullptr || size() == 0; }

    explicit operator bool() const { return !empty(); }

    std::string_view view() const {
        // a string_view is empty iff _data is a nullptr
        if (!_data) return default_view;
        return std::string_view(_data, get_size(_data));
    }
    
    std::string allocate_string() const { 
        return std::string(view()); 
    }
    
    std::filesystem::path allocate_path() const { 
        return std::filesystem::path(view()); 
    }

    std::size_t hash() const {
        if (!_data) return 0;
        return get_hash(_data);
    }

    std::size_t size() const {
        if (!_data) return 0;
        return get_size(_data);
    }

    friend inline bool operator==(const pool_string& x, const pool_string& y) {
        bool equal = x._data == y._data;
        assert(equal == (x.view() == y.view()));
        return equal;
    }

    friend inline bool operator!=(const pool_string& x, const pool_string& y) {
        return !(x == y);
    }

    friend inline auto& operator<<(std::ostream& x, const pool_string& y) {
        return x << y.view();
    }

private:
    static std::size_t get_size(const char* d);
    static std::size_t get_hash(const char* d);

    friend pool_string empool(std::string_view src);
    static std::string_view default_view; // an empty string return if the _data pointer is null

    explicit pool_string(const char* data) : _data(data) {}
    
    const char* _data{nullptr};
};

// pool_string is just a pointer with methods. It needs to be small as strings are a large part
// of ORC's considerable memory usage. pool_string doesn't have a copy constructor or move semantics. 
// Copying and low memory usage depend on pool_string being really a pointer, so double check that here,
// and don't remove this unless you are careful about performance of large projects.
static_assert(sizeof(pool_string) <= sizeof(intptr_t), "pool_string is design to be as small and fast to copy as a pointer.");

//--------------------------------------------------------------------------------------------------
