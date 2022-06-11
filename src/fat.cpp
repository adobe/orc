// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/fat.hpp"// application
#include "orc/mach_types.hpp"

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

struct fat_header {
    std::uint32_t magic{0};
    std::uint32_t nfat_arch{0};
};

struct fat_arch {
    cpu_type_t cputype{0};
    cpu_subtype_t cpusubtype{0};
    std::uint32_t offset{0};
    std::uint32_t size{0};
    std::uint32_t align{0};
};

struct fat_arch_64 {
    cpu_type_t cputype{0};
    cpu_subtype_t cpusubtype{0};
    std::uint64_t offset{0};
    std::uint64_t size{0};
    std::uint32_t align{0};
    std::uint32_t reserved{0};
};

/**************************************************************************************************/

const char* cputype_to_string(cpu_type_t cputype) {
    switch (cputype) {
        case CPU_TYPE_X86: return "arch.x86";
        case CPU_TYPE_ARM: return "arch.arm";
        case CPU_TYPE_X86_64: return "arch.x86_64";
        case CPU_TYPE_ARM64: return "arch.arm64";
        case CPU_TYPE_ARM64_32: return "arch.arm64_32";
        default: return "arch.unknown";
    }
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

void read_fat(object_ancestry&& ancestry,
              freader& s,
              std::istream::pos_type end_pos,
              file_details details,
              callbacks callbacks) {
    auto header = read_pod<fat_header>(s);
    if (details._needs_byteswap) {
        endian_swap(header.magic);
        endian_swap(header.nfat_arch);
    }

    assert(header.magic == FAT_MAGIC || header.magic == FAT_MAGIC_64);
    const bool is_64_bit = header.magic == FAT_MAGIC_64;

    // REVISIT: (fbrereto) opportunity to parallelize here.
    for (std::size_t i = 0; i < header.nfat_arch; ++i) {
        std::size_t offset{0};
        std::size_t size{0};
        cpu_type_t cputype{0};
    
        if (is_64_bit) {
            auto arch = read_pod<fat_arch_64>(s);
            if (details._needs_byteswap) {
                endian_swap(arch.cputype);
                endian_swap(arch.cpusubtype);
                endian_swap(arch.offset);
                endian_swap(arch.size);
                endian_swap(arch.align);
            }
            offset = arch.offset;
            size = arch.size;
            cputype = arch.cputype;
        } else {
            auto arch = read_pod<fat_arch>(s);
            if (details._needs_byteswap) {
                endian_swap(arch.cputype);
                endian_swap(arch.cpusubtype);
                endian_swap(arch.offset);
                endian_swap(arch.size);
                endian_swap(arch.align);
            }
            offset = arch.offset;
            size = arch.size;
            cputype = arch.cputype;
        }

        temp_seek(s, offset, [&] {
            parse_file(cputype_to_string(cputype),
                       ancestry,
                       s,
                       s.tellg() + static_cast<std::streamoff>(size),
                       callbacks);
        });
    }
}

/**************************************************************************************************/
