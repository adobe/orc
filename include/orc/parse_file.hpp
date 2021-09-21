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
#include "orc/string_pool.hpp"

/**************************************************************************************************/
// This is an extension of the famous boost hash_combine routine, extending it to take a variable
// number of input items to hash and then combine together. It's a pack compression of the following
// kind of expansion:
//
// template <class N0, class N1, ..., class NM>
// inline std::size_t hash_combine(std::size_t seed,
//                                 const N0& n0,
//                                 const N1& n1,
//                                 ...
//                                 const NM& nm) {
//     auto h0 = seed ^ std::hash<N0>{}(n0) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
//     auto h1 = h0   ^ std::hash<N1>{}(n1) + 0x9e3779b9 + (h0   << 6) + (h0   >> 2);
//     ...
//     auto hm_1 = ... // the hash from the (NM-1)-th step
//     auto hm = hm_1 ^ std::hash<NM>{}(nm) + 0x9e3779b9 + (hm_1 << 6) + (hm_1 >> 2);
//     return hm;
// }

template <class T>
inline std::size_t hash_combine(std::size_t seed, const T& x) {
    // This is the terminating hash_combine call when there's only one item left to hash into the
    // seed. It also serves as the combiner the other routine variant uses to generate its new
    // seed.
    return seed ^ std::hash<T>{}(x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <class T, class ... Args>
inline std::size_t hash_combine(std::size_t seed, const T& x, Args&&... args) {
    // This routine reduces the argument count by 1 by hashing `x` into the seed, and calling
    // hash_combine on the remaining arguments and the new seed. Eventually Args will disintegrate
    // into a single parameter, at which point the compiler will call the above routine, not this
    // one (which has more than two args), and the compile-time recursion will stop.
    return hash_combine(hash_combine(seed, x), std::forward<Args>(args)...);
}

/**************************************************************************************************/
// very minimal file reader. Uses mmap to bring the file into memory, and subsequently unmaps it
// when the reader destructs. Doesn't do any kinds of bounds checking while reading (that's a
// responsibility of the user at this point, but we could change it if it's more valuable to do so
// here.)
struct freader {
    using off_type = std::istream::off_type;

    explicit freader(const std::filesystem::path& p);

    // `<=` here because sometimes we jump to one past the end of the buffer right before stopping.
    explicit operator bool() const { return static_cast<bool>(_buffer) && _p <= _l; }

    std::size_t size() const { return _l - _p; }

    std::size_t tellg() const { return _p - _f; }

    void seekg(std::istream::off_type offset) {
        assert(*this);
        _p = _f + offset;
    }

    void seekg(std::istream::off_type offset, std::ios::seekdir dir) {
        assert(*this);
        switch (dir) {
            case std::ios::beg: {
                _p = _f + offset;
            } break;
            case std::ios::cur: {
                _p += offset;
            } break;
            case std::ios::end: {
                _p = _f + (size() - offset);
            } break;
            default: {
                // GNU's libstdc++ has an end-of-options marker that the compiler
                // will complain about as being unhandled here. It should *never*
                // be used as a valid value for this enumeration.
                assert(false);
            } break;
        }
    }

    void read(char* p, std::size_t n) {
        assert(*this);
        std::memcpy(p, _p, n);
        _p += n;
    }

    char get() {
        assert(*this);
        return *_p++;
    }

    std::string_view read_c_string_view() {
        assert(*this);
        auto f = _p;
        for (; *_p; ++_p) {}
        auto n = _p++ - f;
        return std::string_view(f, n);
    }

private:
    std::shared_ptr<char> _buffer;
    char* _f{0};
    char* _p{0};
    char* _l{0};
};

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

struct file_details {
    enum class format {
        unknown,
        macho,
        ar,
        fat,
    };
    std::size_t _offset{0};
    format _format{format::unknown};
    arch _arch{arch::unknown};
    bool _is_64_bit{false};
    bool _needs_byteswap{false};
};

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

/**************************************************************************************************/

std::uint32_t uleb128(freader& s);
std::int32_t sleb128(freader& s);

/**************************************************************************************************/

using dies = std::vector<die>;
using register_dies_callback = std::function<void(dies)>;
using do_work_callback = std::function<void(std::function<void()>)>;
using empool_callback = std::function<pool_string(std::string_view)>;

struct callbacks {
    register_dies_callback _register_die;
    do_work_callback _do_work;
    empool_callback _empool;
};

void parse_file(const std::string& object_name,
                freader& s,
                std::istream::pos_type end_pos,
                callbacks callbacks);

/**************************************************************************************************/
