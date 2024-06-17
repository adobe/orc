// Copyright 2022 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <iostream>

/**************************************************************************************************/

namespace orc {

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
    return seed ^ (x + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

template <class T, class... Args>
inline std::size_t hash_combine(std::size_t seed, const T& x, Args&&... args) {
    // This routine reduces the argument count by 1 by hashing `x` into the seed, and calling
    // hash_combine on the remaining arguments and the new seed. Eventually Args will disintegrate
    // into a single parameter, at which point the compiler will call the above routine, not this
    // one (which has more than two args), and the compile-time recursion will stop.
    return hash_combine(hash_combine(seed, x), std::forward<Args>(args)...);
}

/**************************************************************************************************/
// An implementation of MurmurHash3 from SMHasher on GitHub, since modified and used here.
struct murmur_hash {
    static_assert(sizeof(std::size_t) == sizeof(std::uint64_t));
    std::size_t hi{0};
    std::size_t lo{0};
};

murmur_hash murmur3(const void* key, int len, uint32_t seed = 0);

// 64-bit combination of the 128 bit murmur hash. May need to punt on this one if it collides too
// much, but 64 bits is easier to handle than 128.
inline std::size_t murmur3_64(const void* key, int len, uint32_t seed = 0) {
    auto result = murmur3(key, len, seed);
    return hash_combine(result.hi, result.lo);
}

/**************************************************************************************************/

} // namespace orc

/**************************************************************************************************/
