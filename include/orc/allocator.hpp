// Copyright 2022 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <string>

// application
#include "orc/features.hpp"

#define ORC_PRIVATE_FEATURE_ALLOCATOR() 1
#define ORC_PRIVATE_FEATURE_ALLOCATOR_METRICS() (ORC_PRIVATE_FEATURE_ALLOCATOR() && 1)

/**************************************************************************************************/

#if ORC_FEATURE(ALLOCATOR)

/**************************************************************************************************/

namespace orc {

/**************************************************************************************************/

struct allocator_base {
#if ORC_FEATURE(ALLOCATOR_METRICS)
    static std::size_t hits();
    static std::size_t misses();
    static std::size_t total();
#endif // ORC_FEATURE(ALLOCATOR_METRICS)

protected:
    static void* alloc(size_t size);
    static void dealloc(void* p, size_t size);
};

/**************************************************************************************************/

template <class T>
struct allocator : private allocator_base {
    using value_type = T;
    using size_type = size_t;
    using pointer = T*;
    using const_pointer = const T*;

    allocator() noexcept = default;
    template <class U>
    allocator(const allocator<U>&) noexcept {};
    ~allocator() noexcept = default;

    static pointer allocate(size_type n, const void* /*hint*/ = nullptr) {
        return static_cast<pointer>(alloc(sizeof(value_type) * n)); // ignore possible overflow
    }

    static void deallocate(pointer p, size_type n) {
        dealloc(p, sizeof(value_type) * n); // ignore possible overflow
    }

    // any two allocator<T>'s are the same
    friend bool operator==(const allocator<T>&, const allocator<T>&) { return true; }
    friend bool operator!=(const allocator<T>&, const allocator<T>&) { return false; }
};

/**************************************************************************************************/

} // namespace orc

/**************************************************************************************************/

#endif // ORC_FEATURE(ALLOCATOR)

/**************************************************************************************************/
