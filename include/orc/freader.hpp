// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <filesystem>
#include <istream>

/**************************************************************************************************/
// very minimal file reader. Uses mmap to bring the file into memory, and subsequently unmaps it
// when the reader destructs. Doesn't do any kinds of bounds checking while reading (that's a
// responsibility of the user at this point, but we could change it if it's more valuable to do so
// here.)
struct freader {
    using off_type = std::istream::off_type;

    freader() = default;

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
        for (; *_p; ++_p) {
        }
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
