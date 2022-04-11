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
    Add a string to the pool. This is not thread safe.

    A previous implementatino of empool() used a mutex (as a test) which reduced ORC's memoory
    consumption from 83GB to 47GB. That's pretty good, but there is work (and complexity) to
    making the string pool thread safe.

    Surprisingly, using a string pool *per thread* reduces the memory usage from 83GB to 53GB,
    for no noticeable performance impact. A result I find somewhat counter-intuitive. Theory 
    as to why the simple per thread solution works as well as it does:

    * the highly used `struct die` is much smaller. (`pool_string` is now one pointer instead of 3, and a `std::string` became a `pool_string`)
    * the threads have good coherency; a given thread is generally processing a compilation unit, and the same strings repeat
    * and (in both solutions) the memory to store strings is greatly reduced, since they are stored in un-aligned blocks of packed memory
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
    static std::size_t get_size(const char* d) {
        const void* bytes = d - sizeof(std::uint32_t) - sizeof(std::size_t);
        std::uint32_t s;
        std::memcpy(&s, bytes, sizeof(s));
        assert(s > 0);         // required, else should have been _data == nullptr
        assert(s < 100000);    // sanity check
        return s;
    }

    static std::size_t get_hash(const char* d) {
        const void* bytes = d - sizeof(std::size_t);
        std::size_t h;
        std::memcpy(&h, bytes, sizeof(h));
        return h;
    }

    friend pool_string empool(std::string_view src);
    static std::string_view default_view; // an empty string

    explicit pool_string(const char* data) : _data(data) {
    }
    
    // NOT ALIGNED
    // Before the _data pointer:
    //      'uint32_t' length of string
    //      'size_t' hash
    // _data[] null terminated to ease debugging
    const char* _data{nullptr};
};

/**************************************************************************************************/
