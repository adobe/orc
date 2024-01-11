// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/string_pool.hpp"

// stdc++
#include <array>
#include <cassert>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// tbb
#include <tbb/concurrent_unordered_map.h>

// application
#include "orc/features.hpp"
#include "orc/hash.hpp"
#include "orc/memory.hpp"
#include "orc/tracy.hpp"

/*static*/ std::string_view pool_string::default_view("");

#define ORC_PRIVATE_FEATURE_PROFILE_POOL_MEMORY() (ORC_PRIVATE_FEATURE_TRACY() && 0)
#define ORC_PRIVATE_FEATURE_PROFILE_POOL_MUTEXES() (ORC_PRIVATE_FEATURE_TRACY() && 1)

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

std::size_t string_view_hash(std::string_view s) { return orc::murmur3_64(s.data(), static_cast<std::uint32_t>(s.length())); }

/**************************************************************************************************/

// Data is backed and not aligned.
// Before the _data pointer:
//      'uint32_t' length of string
//      'size_t' hash
// The _data pointer is returned to a null terminated string, to make debugging easier
// get_size() and get_hash() unpack this data as needed.
//
struct pool {
    char* _p{nullptr};
    std::size_t _n{0};
    std::size_t _size{0};
    std::size_t _wasted{0};
#if ORC_FEATURE(PROFILE_POOL_MEMORY)
    const char* _id{nullptr};
#endif // ORC_FEATURE(PROFILE_POOL_MEMORY)

    using ponds_type = std::vector<std::unique_ptr<char[]>>;
#if ORC_FEATURE(LEAKY_MEMORY)
    ponds_type& _ponds{orc::make_leaky<ponds_type>()};
#else
    ponds_type _ponds{orc::make_leaky<ponds_type>()};
#endif // ORC_FEATURE(LEAKY_MEMORY)

    const char* empool(std::string_view incoming) {
        const uint32_t sz = (uint32_t)incoming.size();
        const uint32_t tsz = sz + sizeof(uint32_t) + sizeof(size_t) + 1;

        if (_n < tsz) {
            _wasted += _n;

            // grow the pool's ponds exponentially. This will strike a balance between
            // the cost of memory allocations required and making sure we have enough
            // space for this and future strings.
            _n = std::max<std::size_t>(_size * 2, tsz);
            _ponds.push_back(std::make_unique<char[]>(_n));
            _p = _ponds.back().get();
            _size += _n;

#if ORC_FEATURE(PROFILE_POOL_MEMORY)
            assert(_id);
            tracy::Profiler::PlotData(_id, static_cast<int64_t>(_size));
#endif // ORC_FEATURE(PROFILE_POOL_MEMORY)
        }

        const std::size_t h = string_view_hash(incoming);
        // Memory isn't aligned - need to memcpy to pack the data
        std::memcpy(_p, &sz, sizeof(uint32_t));
        std::memcpy(_p + sizeof(uint32_t), &h, sizeof(size_t));
        std::memcpy(_p + sizeof(uint32_t) + sizeof(size_t), incoming.data(), sz);
        *(_p + tsz - 1) = 0; // null terminate for debugging

        const char* result = _p + sizeof(uint32_t) + sizeof(size_t);
        _n -= tsz;
        _p += tsz;
        return result;
    }
};

/**************************************************************************************************/

#if ORC_FEATURE(PROFILE_POOL_MUTEXES)
    using string_pool_mutex = LockableBase(std::mutex);
#else
    using string_pool_mutex = std::mutex;
#endif // ORC_FEATURE(PROFILE_POOL_MUTEXES)

/**************************************************************************************************/

auto& pool_mutex(std::size_t index) {
    assert(index < string_pool_count_k);
#if ORC_FEATURE(PROFILE_POOL_MUTEXES)
    // I've been banging my head against this for a while, and this is the sadistic solution I've
    // come up with. These are supposed to be a straightforward array of mutexes, of which one
    // is picked when empooling a string, in order to reduce lock contention and blocking across
    // the threads. The non-Tracy variant is what we want, but the Tracy variant requires a
    // constructor parameter and is non-movable and non-copyable, so we have to construct them
    // in place, which makes them very difficult to construct as a contiguous collection. So
    // we allocate them dynamically and collect their pointers as a contiguous sequence, then
    // index and dereference one of the pointers.
    static string_pool_mutex** mutexes = []{
        static std::vector<string_pool_mutex*> result;
        for (std::size_t i(0); i < string_pool_count_k; ++i) {
            static constexpr tracy::SourceLocationData srcloc { nullptr, "pool_mutex", TracyFile, TracyLine, 0 };
            result.emplace_back(new string_pool_mutex(&srcloc));
        }
        return &result[0];
    }();
    return *mutexes[index];
#else
    static std::mutex mutexes[string_pool_count_k];
    return mutexes[index];
#endif // ORC_FEATURE(PROFILE_POOL_MUTEXES)
}

auto& pool(std::size_t index) {
    static struct pool* pools = []{
        static struct pool result[string_pool_count_k];

#if ORC_FEATURE(PROFILE_POOL_MEMORY)
        for (std::size_t i(0); i < string_pool_count_k; ++i) {
            const char* pool_id = orc::tracy::format_unique("string_pool %zu", i);
            TracyPlotConfig(pool_id, tracy::PlotFormatType::Memory, true, true, 0);
            result[i]._id = pool_id;
        }
#endif // ORC_FEATURE(PROFILE_POOL_MEMORY)

        return result;
    }();
    return pools[index];
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

std::size_t pool_string::get_size(const char* d) {
    assert(d);
    const void* bytes = d - sizeof(std::uint32_t) - sizeof(std::size_t);
    std::uint32_t s = orc::unaligned_read<std::uint32_t>(bytes);
    assert(s > 0);      // required, else should have been _data == nullptr
    assert(s < 100000); // sanity check
    return s;
}

std::size_t pool_string::get_hash(const char* d) {
    assert(d);
    const void* bytes = d - sizeof(std::size_t);
    return orc::unaligned_read<std::size_t>(bytes);
}

pool_string empool(std::string_view src) {
    ZoneScoped;
    ZoneColor(tracy::Color::ColorType::Green); // cache hit
    ZoneText(src.data(), src.size());

    // A pool_string is empty iff _data = nullptr
    // So this creates an empty pool_string (as opposed to an empty string_view, where
    // default_view would be returned.)
    if (src.empty()) {
        return pool_string(nullptr);
    }

    static decltype(auto) keys =
        orc::make_leaky<tbb::concurrent_unordered_map<size_t, const char*>>();

    const std::size_t h = string_view_hash(src);

    auto find_key = [&](std::size_t h) -> const char* {
        const auto found = keys.find(h);
        return found == keys.end() ? nullptr ; found->second;
    };

    if (const char* c = find_key(h)) {
        pool_string ps(c);
        assert(ps.view() == src);
        return ps;
    }

    const int index = h % string_pool_count_k;
    std::lock_guard<string_pool_mutex> pool_guard(pool_mutex(index));

    // Now that we have the lock, do the search again in case another thread empooled the string
    // while we were waiting for the lock.
    if (const char* c = find_key(h)) {
        ZoneColor(tracy::Color::ColorType::Orange); // cache "half-hit"

        pool_string ps(c);
        assert(ps.view() == src);
        return ps;
    }

    // Not already interned; empool it and add to the 'keys'
    // The pools are not threadsafe, so we need one per mutex
    const char* ptr = pool(index).empool(src);
    assert(ptr);
    keys.insert(std::make_pair(h, ptr));

    ZoneColor(tracy::Color::ColorType::Red); // cache miss

    return pool_string(ptr);
}

/**************************************************************************************************/

std::array<std::size_t, string_pool_count_k> string_pool_sizes() {
    std::array<std::size_t, string_pool_count_k> result;

    for (std::size_t i(0); i < string_pool_count_k; ++i) {
        std::lock_guard<string_pool_mutex> pool_guard(pool_mutex(i));
        result[i] = pool(i)._size;
    }

    return result;
}

/**************************************************************************************************/

std::array<std::size_t, string_pool_count_k> string_pool_wasted() {
    std::array<std::size_t, string_pool_count_k> result;

    for (std::size_t i(0); i < string_pool_count_k; ++i) {
        std::lock_guard<string_pool_mutex> pool_guard(pool_mutex(i));
        // Add the accumulated waste from previous ponds to the current pond's unused space.
        result[i] = pool(i)._wasted + pool(i)._n;
    }

    return result;
}

/**************************************************************************************************/
