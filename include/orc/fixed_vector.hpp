// Copyright 2025 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <array>
#include <stdexcept>
#include <algorithm>

// adobe contract checks
#include "adobe/contract_checks.hpp"

//--------------------------------------------------------------------------------------------------

namespace orc {

//--------------------------------------------------------------------------------------------------

template <class T, std::size_t N>
struct fixed_vector {
    using value_type = T;
    using array_type = std::array<T, N>;
    using size_type = typename array_type::size_type;
    using iterator = typename array_type::iterator;
    using const_iterator = typename array_type::const_iterator;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    // Constructors
    fixed_vector() = default;
    fixed_vector(const fixed_vector&) = default;
    fixed_vector& operator=(const fixed_vector&) = default;

    fixed_vector(fixed_vector&& rhs) : _a(std::move(rhs._a)), _n(rhs._n) {
        rhs._n = 0;
    }

    fixed_vector& operator=(fixed_vector&& rhs) {
        _a = std::move(rhs._a);
        _n = rhs._n;
        rhs._n = 0;
        return *this;
    }

    fixed_vector(size_type count, const T& value) {
        ADOBE_PRECONDITION(count <= N, "fixed_vector overflow");
        for (size_type i = 0; i < count; ++i) {
            push_back(value);
        }
    }

    // Element access
    T& at(size_type pos) {
        if (pos >= _n) {
            throw std::out_of_range("fixed_vector::at");
        }
        return _a[pos];
    }

    const T& at(size_type pos) const {
        if (pos >= _n) {
            throw std::out_of_range("fixed_vector::at");
        }
        return _a[pos];
    }

    T& operator[](size_type pos) { return _a[pos]; }
    const T& operator[](size_type pos) const { return _a[pos]; }

    T& front() { return _a[0]; }
    const T& front() const { return _a[0]; }

    T& back() { return _a[_n - 1]; }
    const T& back() const { return _a[_n - 1]; }

    // Capacity
    size_type size() const { return _n; }
    bool empty() const { return _n == 0; }
    size_type max_size() const { return N; }
    size_type capacity() const { return N; }

    // Modifiers
    void push_back(const T& x) {
        ADOBE_PRECONDITION(_n < N, "fixed_vector overflow");
        _a[_n++] = x;
    }

    void pop_back() {
        ADOBE_PRECONDITION(_n > 0, "fixed_vector underflow");
        back() = T();
        --_n;
    }

    void clear() {
        while (!empty()) {
            pop_back();
        }
    }

    iterator insert(iterator pos, const T& value) {
        ADOBE_PRECONDITION(_n < N - 1, "fixed_vector overflow");
        auto old_end = end();
        push_back(value);
        std::rotate(pos, old_end, end());
        return pos;
    }

    template <class Iterator>
    iterator insert(iterator pos, Iterator first, Iterator last) {
        iterator old_end = end();
        while (first != last) {
            push_back(*first++);
        }
        std::rotate(pos, old_end, end());
        return pos;
    }

    auto erase(iterator pos) {
        ADOBE_PRECONDITION(_n > 0, "fixed_vector underflow");
        std::rotate(pos, std::next(pos), end());
        back() = T();
        --_n;
        return pos;
    }

    // Iterators
    auto begin() { return _a.begin(); }
    auto begin() const { return _a.begin(); }
    auto cbegin() const { return _a.begin(); }

    auto end() { return std::next(begin(), _n); }
    auto end() const { return std::next(begin(), _n); }
    auto cend() const { return std::next(cbegin(), _n); }

    auto rbegin() { return reverse_iterator(end()); }
    auto rbegin() const { return const_reverse_iterator(end()); }
    auto crbegin() const { return const_reverse_iterator(cend()); }

    auto rend() { return reverse_iterator(begin()); }
    auto rend() const { return const_reverse_iterator(begin()); }
    auto crend() const { return const_reverse_iterator(cbegin()); }

    friend void swap(fixed_vector& lhs, fixed_vector& rhs) {
        std::swap(lhs._a, rhs._a);
        std::swap(lhs._n, rhs._n);
    }

private:
    array_type _a;
    size_type _n{0};
};

template <class T, std::size_t N>
bool operator==(const fixed_vector<T, N>& lhs, const fixed_vector<T, N>& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <class T, std::size_t N>
bool operator!=(const fixed_vector<T, N>& lhs, const fixed_vector<T, N>& rhs) {
    return !(lhs == rhs);
}

//--------------------------------------------------------------------------------------------------

} // namespace orc

//--------------------------------------------------------------------------------------------------
