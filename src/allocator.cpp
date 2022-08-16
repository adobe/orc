// Copyright 2022 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/allocator.hpp"

// stdc++
#include <cassert>
#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>

// application

/**************************************************************************************************/

#if ORC_FEATURE(ALLOCATOR)

/**************************************************************************************************/

namespace {

/**************************************************************************************************/
#if ORC_FEATURE(ALLOCATOR_METRICS)
std::atomic_size_t hit_count_g{0};
std::atomic_size_t alloc_count_g{0};
#endif // ORC_FEATURE(ALLOCATOR_METRICS)
/**************************************************************************************************/

struct alloc_info {
    std::vector<void*> _recycled_pointers;
};

/**************************************************************************************************/

auto& alloc_size_map() {
    // maps alloc size to recycled pointers of that size
    // pointer avoids thread shutdown crashes, and deliberate leaks allow quick shutdown
    thread_local auto* p = new std::unordered_map<size_t, alloc_info>;
    return *p;
}

/**************************************************************************************************/

static constexpr size_t alloc_threshold_k = 1024; // recycling kicks in when malloc size <= this

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

namespace orc {

/**************************************************************************************************/

void* allocator_base::alloc(size_t size) {
#if ORC_FEATURE(ALLOCATOR_METRICS)
    ++alloc_count_g;
#endif // ORC_FEATURE(ALLOCATOR_METRICS)

    if (size > alloc_threshold_k) return malloc(size);

    auto& size_map = alloc_size_map();
    auto iter = size_map.find(size);

    // nothing available to recycle, so allocate space
    if (iter == size_map.end()) return malloc(size);

    alloc_info& info = iter->second;

    // nothing available to recycle, so allocate space
    if (info._recycled_pointers.empty()) return malloc(size);

#if ORC_FEATURE(ALLOCATOR_METRICS)
    ++hit_count_g;
#endif // ORC_FEATURE(ALLOCATOR_METRICS)

    // use a recycled pointer from the _recycled_pointers vector
    void* result = info._recycled_pointers.back();
    info._recycled_pointers.pop_back();

    return result;
}

/**************************************************************************************************/

void allocator_base::dealloc(void* p, size_t size) {
    if (size > alloc_threshold_k) return free(p);

    auto& size_map = alloc_size_map();
    auto iter = size_map.find(size);

    if (iter == size_map.end()) {
        // ensure an alloc_info has been associated with this size
        auto inserted = size_map.insert(std::make_pair(size, alloc_info()));
        assert(inserted.second);
        iter = std::move(inserted.first);
    }

    assert(iter != size_map.end());

    iter->second._recycled_pointers.push_back(p);
}

/**************************************************************************************************/
#if ORC_FEATURE(ALLOCATOR_METRICS)
std::size_t allocator_base::hits() { return hit_count_g; }
std::size_t allocator_base::misses() { return alloc_count_g - hit_count_g; }
std::size_t allocator_base::total() { return alloc_count_g; }
#endif // ORC_FEATURE(ALLOCATOR_METRICS)
/**************************************************************************************************/

} // namespace orc

/**************************************************************************************************/

#endif // ORC_FEATURE(ALLOCATOR)

/**************************************************************************************************/
