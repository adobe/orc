// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <iostream>

// application
#include "orc/parse_file.hpp"

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

constexpr cpu_type_t CPU_ARCH_ABI64 = 0x01000000;
constexpr cpu_type_t CPU_ARCH_ABI64_32 = 0x02000000;
constexpr cpu_type_t CPU_TYPE_X86 = 7;
constexpr cpu_type_t CPU_TYPE_ARM = 12;
constexpr cpu_type_t CPU_TYPE_X86_64 = CPU_TYPE_X86 | CPU_ARCH_ABI64;
constexpr cpu_type_t CPU_TYPE_ARM64 = CPU_TYPE_ARM | CPU_ARCH_ABI64;
constexpr cpu_type_t CPU_TYPE_ARM64_32 = CPU_TYPE_ARM | CPU_ARCH_ABI64_32;

struct section_64 {
    char          sectname[16];
    char          segname[16];
    std::uint64_t addr;
    std::uint64_t size;
    std::uint32_t offset;
    std::uint32_t align;
    std::uint32_t reloff;
    std::uint32_t nreloc;
    std::uint32_t flags;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
    std::uint32_t reserved3;
};

using vm_prot_t = int;

struct segment_command_64 {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    char          segname[16];
    std::uint64_t vmaddr;
    std::uint64_t vmsize;
    std::uint64_t fileoff;
    std::uint64_t filesize;
    vm_prot_t     maxprot;
    vm_prot_t     initprot;
    std::uint32_t nsects;
    std::uint32_t flags;
};

/**************************************************************************************************/

void read_macho(std::string object_name,
                freader s,
                std::istream::pos_type end_pos,
                file_details details,
                callbacks callbacks);

/**************************************************************************************************/
