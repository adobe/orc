// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <type_traits>

// application
#include "orc/dwarf_structs.hpp"
#include "orc/hash.hpp"
#include "orc/object_file_registry.hpp"
#include "orc/string_pool.hpp"

/**************************************************************************************************/
// temp_seek will move the read pointer of the incoming reader to the specified location, execute
// the lambda, and then reset the read points to the location it was at when the routine began.
// the read pointer will be correctly reset even if the lambda throws.
template <typename F>
auto temp_seek(freader& s, std::istream::off_type offset, std::ios::seekdir dir, F&& f) {
    struct posmark {
        explicit posmark(freader& s) : _s{s}, _pos{_s.tellg()} {}
        ~posmark() { _s.seekg(_pos); }

    private:
        freader& _s;
        std::size_t _pos;
    } pm(s);

    s.seekg(offset, dir);

    if constexpr (std::is_same<std::invoke_result_t<F>, void>::value) {
        std::forward<F>(f)();
    } else {
        return std::forward<F>(f)();
    }
}

template <typename F>
auto temp_seek(freader& s, std::istream::off_type offset, F&& f) {
    return temp_seek(s, offset, std::ios::beg, std::forward<F>(f));
}

template <typename F>
auto temp_seek(freader& s, F&& f) {
    return temp_seek(s, 0, std::ios::cur, std::forward<F>(f));
}

template <typename F>
auto read_exactly(freader& s, std::size_t size, F&& f) {
    auto start = s.tellg();
    if constexpr (std::is_same<std::invoke_result_t<F, std::size_t>, void>::value) {
        std::forward<F>(f)(size);
        assert(s.tellg() == start + size);
        if (s.tellg() != start + size) throw std::runtime_error("read_exactly failure");
    } else {
        auto result = std::forward<F>(f)(size);
        assert(s.tellg() == start + size);
        if (s.tellg() != start + size) throw std::runtime_error("read_exactly failure");
        return result;
    }
}

/**************************************************************************************************/

template <typename T>
void endian_swap(T& c) {
    char* first = reinterpret_cast<char*>(&c);
    char* last = first + sizeof(T);
    while (first != last) {
        --last;
        std::swap(*first, *last);
        ++first;
    }
}

/**************************************************************************************************/

template <typename T>
T read_pod(freader& s) {
    T x;
    s.read(reinterpret_cast<char*>(&x), sizeof(T));
    return x;
}

template <>
inline bool read_pod(freader& s) {
    char x;
    s.read(reinterpret_cast<char*>(&x), 1);
    return x ? true : false;
}
/**************************************************************************************************/

std::uint32_t uleb128(freader& s);
std::int32_t sleb128(freader& s);

/**************************************************************************************************/
/*
    For functions that take values by rvalue reference (aka sink functions), it can be helpful to be
    explicit about the object being passed in. In such cases, the object can only be moved or
    copied. C++ already provides a `move` routine; this is the `copy` equivalent. It is more
    helpful than passing T(x) (which would create a copy of x) because, at a minimum, it is more
    explicit about the intent of the call.
*/
template <typename T>
constexpr std::decay_t<T> copy(T&& value) noexcept(noexcept(std::decay_t<T>{
    static_cast<T&&>(value)})) {
    static_assert(!std::is_same<std::decay_t<T>, T>::value, "explicit copy of rvalue.");
    return std::decay_t<T>{static_cast<T&&>(value)};
}

/**************************************************************************************************/

using register_dies_callback = std::function<void(dies)>;
using do_work_callback = std::function<void(std::function<void()>)>;
using empool_callback = std::function<pool_string(std::string_view)>;

struct callbacks {
    register_dies_callback _register_die;
    do_work_callback _do_work;
};

void parse_file(std::string_view object_name,
                const object_ancestry& ancestry,
                freader& s,
                std::istream::pos_type end_pos,
                callbacks callbacks);

/**************************************************************************************************/
