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

pool_string empool(std::string_view src);

struct pool_string {
    pool_string() {}
    ~pool_string() {}

    auto empty() const { return _data == nullptr; }

    std::string_view view() const {
        if (!_data) return default_view;
        std::uint32_t size = get_length(_data);
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
        return x._data == y._data;
    }

    friend inline bool operator!=(const pool_string& x, const pool_string& y) {
        return !(x == y);
    }

    friend inline auto& operator<<(std::ostream& x, const pool_string& y) {
        return x << y.view();
    }

private:
    static std::uint32_t get_length(const char* d) {
        assert(d > (const char*)1024);
        const void* bytes = d - sizeof(std::uint32_t) - sizeof(std::size_t);
        std::uint32_t s;
        std::memcpy(&s, bytes, sizeof(s));
        assert(s > 0 && s < 100000);
        return s;
    }

    static std::size_t get_hash(const char* d) {
        assert(d > (const char*)1024);
        const void* bytes = d - sizeof(std::size_t);
        std::size_t h;
        std::memcpy(&h, bytes, sizeof(h));
        return h;
    }

    friend pool_string empool(std::string_view src);
    static std::string_view default_view;

    //explicit pool_string(std::string_view s, std::size_t h) : _data( {}
    //explicit pool_string(std::string_view s) : _s{std::move(s)},
    //_h{std::hash<std::string_view>{}(_s)} {}
    explicit pool_string(const char* data) : _data(data) {
    }
    
    // NOT ALIGNED
    // Before the _data pointer:
    //      'uint32_t' length of string
    //      'size_t' hash
    // _data[]

    const char* _data{nullptr};

    //std::string_view _s;
    //std::size_t _h{0};
};

/**************************************************************************************************/
