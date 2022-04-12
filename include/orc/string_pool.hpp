// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <ostream>
#include <string>
#include <string_view>
#include <filesystem>

/**************************************************************************************************/

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
    life of the application. This has some useful properties.

    * A `pool_string` is one pointer in size
    * A `pool_string` is thread safe
    * Two `pool_strings` pointing to the same `_data` are always equal.
    * Becaues `pool_strings` can be in different pools, if the `_data` members are different, they may still be equal
    * `_data` is a char* to null terminated data, which is just a c string. Useful for debugging. 
    * if `_data` is null, it is intepreted as an empty string (""). If `_data` is not-null, it always is size() > 0
    
    When empooled, the hash (64 bits) and size/length (32 bits) are stored before the char* to the data.
    Note there is no memory alignment in the pool - it is fully packed - so data needs to be memcpy'd in
    and out, just in case the processor doesn't like un-aligned reads.
*/
struct pool_string {
    pool_string() {}
    ~pool_string() {}

    bool empty() const { return _data == nullptr; }

    std::string_view view() const {
        if (!_data) return default_view;
        std::uint32_t size = get_size(_data);
        return std::string_view(_data, size);
    }
    
    std::string allocate_string() const { 
        assert(_data);
        return std::string(view()); 
    }
    
    std::filesystem::path allocate_path() const { 
        assert(_data); 
        return std::filesystem::path(view()); 
    }

    size_t hash() const {
        if (!_data) return 0;
        return get_hash(_data);
    }

    friend inline bool operator==(const pool_string& x, const pool_string& y) {
        if (x._data == y._data)
            return true;
        return x.view() == y.view();
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

/**************************************************************************************************/
