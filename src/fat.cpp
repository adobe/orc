// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/fat.hpp"

// system
#include <mach-o/fat.h>

/**************************************************************************************************/

void read_fat(const std::string& object_name,
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
        }

        temp_seek(s, offset, [&] {
            parse_file(object_name, s, s.tellg() + static_cast<std::streamoff>(size), callbacks);
        });
    }
}

/**************************************************************************************************/
