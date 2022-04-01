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
    pool_string() = default;

    auto empty() const { return _s.empty(); }

    std::string allocate_string() const { return std::string(_s); }
    std::filesystem::path allocate_path() const { return std::filesystem::path(_s); }

    auto hash() const { return _h; }
    auto string() const { return _s; }

    friend inline bool operator==(const pool_string& x, const pool_string& y) {
        return x._h == y._h;
    }

    friend inline bool operator!=(const pool_string& x, const pool_string& y) {
        return !(x == y);
    }

    friend inline auto& operator<<(std::ostream& x, const pool_string& y) {
        return x << y._s;
    }

private:
    friend pool_string empool(std::string_view src);

    explicit pool_string(std::string_view s, std::size_t h) : _s{std::move(s)}, _h{h} {}
    explicit pool_string(std::string_view s) : _s{std::move(s)},
        _h{std::hash<std::string_view>{}(_s)} {}

    std::string_view _s;
    std::size_t _h{0};
};

/**************************************************************************************************/
