// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

/**************************************************************************************************/

constexpr std::uint32_t MH_MAGIC = 0xfeedface;
constexpr std::uint32_t MH_CIGAM = 0xcefaedfe;
constexpr std::uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr std::uint32_t MH_CIGAM_64 = 0xcffaedfe;
constexpr std::uint32_t FAT_MAGIC = 0xcafebabe;
constexpr std::uint32_t FAT_CIGAM = 0xbebafeca;
constexpr std::uint32_t FAT_MAGIC_64 = 0xcafebabf;
constexpr std::uint32_t FAT_CIGAM_64 = 0xbfbafeca;

using cpu_type_t = int;
using cpu_subtype_t = int;

constexpr cpu_type_t CPU_ARCH_ABI64 = 0x01000000;
constexpr cpu_type_t CPU_ARCH_ABI64_32 = 0x02000000;
constexpr cpu_type_t CPU_TYPE_X86 = 7;
constexpr cpu_type_t CPU_TYPE_ARM = 12;
constexpr cpu_type_t CPU_TYPE_X86_64 = CPU_TYPE_X86 | CPU_ARCH_ABI64;
constexpr cpu_type_t CPU_TYPE_ARM64 = CPU_TYPE_ARM | CPU_ARCH_ABI64;
constexpr cpu_type_t CPU_TYPE_ARM64_32 = CPU_TYPE_ARM | CPU_ARCH_ABI64_32;

/**************************************************************************************************/
