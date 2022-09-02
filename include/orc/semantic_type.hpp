// Copyright 2022 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <utility>

/**************************************************************************************************/

namespace orc {

/**************************************************************************************************/
// A semantic type (also known as a phantom type or a named type) is one that is distinguished from
// other types by its name alone. In other words, the type carries specific meaning in its name.

template <class T, class Meaning>
struct semantic {
    constexpr explicit semantic(T const& value) : _value(value) {}
    constexpr explicit semantic(T&& value) : _value(std::move(value)) {}

    constexpr T& operator*() & { return _value; }
    constexpr T const& operator*() const& { return _value; }
    constexpr T&& operator*() && { return std::move(_value); }
    constexpr const T&& operator*() const&& { return std::move(_value); }

    friend inline bool operator==(const semantic& x, const semantic& y) { return *x == *y; }
    friend inline bool operator!=(const semantic& x, const semantic& y) { return !(x == y); }

private:
    T _value{T()};
};

/**************************************************************************************************/

} // namespace orc

/**************************************************************************************************/
