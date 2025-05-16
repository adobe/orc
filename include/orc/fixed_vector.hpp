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

/**
 * @brief A fixed-size vector container that provides a subset of `std::vector` functionality
 * 
 * @tparam T The type of elements stored in the vector
 * @tparam N The maximum number of elements the vector can hold
 * 
 * This container provides a fixed-size alternative to `std::vector` with similar interface.
 * It guarantees that memory is allocated on the stack and never reallocates.
 * Operations that would exceed the fixed capacity `N` will terminate the program.
 */
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

    /**
     * @brief Move constructor
     * 
     * @param rhs The `fixed_vector` to move from
     * 
     * @post `rhs` is left in an empty state
     */
    fixed_vector(fixed_vector&& rhs) : _a(std::move(rhs._a)), _n(rhs._n) {
        rhs._n = 0;
    }

    /**
     * @brief Move assignment operator
     * 
     * @param rhs The `fixed_vector` to move from
     * @return Reference to this `fixed_vector`
     * 
     * @post `rhs` is left in an empty state
     */
    fixed_vector& operator=(fixed_vector&& rhs) {
        _a = std::move(rhs._a);
        _n = rhs._n;
        rhs._n = 0;
        return *this;
    }

    /**
     * @brief Constructs a fixed_vector with count copies of value
     * 
     * @param count Number of elements to create
     * @param value Value to initialize elements with
     * 
     * @pre count <= N
     * @note If count > N, the program will terminate.
     */
    fixed_vector(size_type count, const T& value) {
        ADOBE_PRECONDITION(count <= N, "fixed_vector overflow");
        for (size_type i = 0; i < count; ++i) {
            push_back(value);
        }
    }

    /**
     * @brief Access element at specified position with bounds checking
     * 
     * @param pos Position of the element to return
     * @return Reference to the requested element
     * 
     * @throw std::out_of_range if pos >= size()
     */
    T& at(size_type pos) {
        if (pos >= _n) {
            throw std::out_of_range("fixed_vector::at");
        }
        return _a[pos];
    }

    /**
     * @brief Access element at specified position with bounds checking (const version)
     * 
     * @param pos Position of the element to return
     * @return Const reference to the requested element
     * 
     * @throw std::out_of_range if pos >= size()
     */
    const T& at(size_type pos) const {
        if (pos >= _n) {
            throw std::out_of_range("fixed_vector::at");
        }
        return _a[pos];
    }

    /**
     * @brief Access element at specified position without bounds checking
     * 
     * @param pos Position of the element to return
     * @return Reference to the requested element
     * 
     * @pre pos < size()
     */
    T& operator[](size_type pos) { return _a[pos]; }

    /**
     * @brief Access element at specified position without bounds checking (const version)
     * 
     * @param pos Position of the element to return
     * @return Const reference to the requested element
     * 
     * @pre pos < size()
     */
    const T& operator[](size_type pos) const { return _a[pos]; }

    /**
     * @brief Returns reference to the first element
     * 
     * @return Reference to the first element
     * 
     * @pre !empty()
     */
    T& front() { 
        ADOBE_PRECONDITION(!empty(), "fixed_vector is empty");
        return _a[0];
    }

    /**
     * @brief Returns const reference to the first element
     * 
     * @return Const reference to the first element
     * 
     * @pre !empty()
     */
    const T& front() const {
        ADOBE_PRECONDITION(!empty(), "fixed_vector is empty");
        return _a[0];
    }

    /**
     * @brief Returns reference to the last element
     * 
     * @return Reference to the last element
     * 
     * @pre !empty()
     */
    T& back() { 
        ADOBE_PRECONDITION(!empty(), "fixed_vector is empty");
        return _a[_n - 1];
    }

    /**
     * @brief Returns const reference to the last element
     * 
     * @return Const reference to the last element
     * 
     * @pre !empty()
     */
    const T& back() const { 
        ADOBE_PRECONDITION(!empty(), "fixed_vector is empty");
        return _a[_n - 1];
    }

    // Capacity
    size_type size() const { return _n; }
    bool empty() const { return _n == 0; }
    size_type max_size() const { return N; }
    size_type capacity() const { return N; }

    /**
     * @brief Adds an element to the end
     * 
     * @param x Value to append
     * 
     * @pre size() < N, otherwise the program will terminate.
     */
    void push_back(const T& x) {
        ADOBE_PRECONDITION(_n < N, "fixed_vector overflow");
        _a[_n++] = x;
    }

    /**
     * @brief Removes the last element
     * 
     * @pre !empty(), otherwise the program will terminate.
     * @post The last element is destroyed and size() is decremented by 1
     */
    void pop_back() {
        ADOBE_PRECONDITION(_n > 0, "fixed_vector underflow");
        back() = T();
        --_n;
    }

    /**
     * @brief Removes all elements
     * 
     * @post size() == 0
     */
    void clear() {
        while (!empty()) {
            pop_back();
        }
    }

    /**
     * @brief Inserts value before pos
     * 
     * @param pos Iterator before which the content will be inserted
     * @param value Element value to insert
     * @return Iterator pointing to the inserted value
     * 
     * @pre size() < N, otherwise the program will terminate.
     */
    iterator insert(iterator pos, const T& value) {
        auto old_end = end();
        push_back(value);
        std::rotate(pos, old_end, end());
        return pos;
    }

    /**
     * @brief Inserts elements from range [first, last) before pos
     * 
     * @param pos Iterator before which the content will be inserted
     * @param first Iterator to the first element to insert
     * @param last Iterator past the last element to insert
     * @return Iterator pointing to the first inserted element
     * 
     * @pre size() + std::distance(first, last) <= N, otherwise the program will terminate.
     */
    template <class Iterator>
    iterator insert(iterator pos, Iterator first, Iterator last) {
        iterator old_end = end();
        while (first != last) {
            push_back(*first++);
        }
        std::rotate(pos, old_end, end());
        return pos;
    }

    /**
     * @brief Removes element at pos
     * 
     * @param pos Iterator to the element to remove
     * @return Iterator following the last removed element
     * 
     * @pre !empty(), otherwise the program will terminate.
     * @post size() is decremented by 1
     */
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

/**
 * @brief Equality comparison operator
 * 
 * @param lhs First fixed_vector to compare
 * @param rhs Second fixed_vector to compare
 * @return true if the vectors have the same size and elements, false otherwise
 */
template <class T, std::size_t N>
bool operator==(const fixed_vector<T, N>& lhs, const fixed_vector<T, N>& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

/**
 * @brief Inequality comparison operator
 * 
 * @param lhs First fixed_vector to compare
 * @param rhs Second fixed_vector to compare
 * @return true if the vectors are not equal, false otherwise
 */
template <class T, std::size_t N>
bool operator!=(const fixed_vector<T, N>& lhs, const fixed_vector<T, N>& rhs) {
    return !(lhs == rhs);
}

//--------------------------------------------------------------------------------------------------

} // namespace orc

//--------------------------------------------------------------------------------------------------
