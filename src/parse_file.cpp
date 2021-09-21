// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/parse_file.hpp"

// application config
#include "orc/features.hpp"

// stdc++
#include <bit>
#include <cstdio>

// system
#include <fcntl.h> // open
#include <sys/mman.h>
#include <unistd.h> // close#if ORC_FEATURE(MACH_O)

#if ORC_FEATURE(MACH_O)
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#else
#define	MH_MAGIC 0xfeedface
#define MH_CIGAM 0xcefaedfe
#define MH_MAGIC_64 0xfeedfacf
#define MH_CIGAM_64 0xcffaedfe
#define FAT_MAGIC 0xcafebabe
#define FAT_CIGAM 0xbebafeca
#define FAT_MAGIC_64 0xcafebabf
#define FAT_CIGAM_64 0xbfbafeca
#endif // ORC_FEATURE(MACH_O)

// application
#include "orc/ar.hpp"
#include "orc/fat.hpp"
#include "orc/macho.hpp"

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

file_details detect_file(freader& s) {
    return temp_seek(s, [&] {
        std::uint32_t header;
        file_details result;

        result._offset = s.tellg();

        s.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (header == MH_MAGIC || header == MH_CIGAM || header == MH_MAGIC_64 ||
            header == MH_CIGAM_64) {
            result._format = file_details::format::macho;
        } else if (header == 'ra<!' || header == '!<ar') {
            result._format = file_details::format::ar;
        } else if (header == FAT_MAGIC || header == FAT_CIGAM || header == FAT_MAGIC_64 ||
                   header == FAT_CIGAM_64) {
            result._format = file_details::format::fat;
        }

        result._is_64_bit = header == MH_MAGIC_64 || header == MH_CIGAM_64 ||
                            header == FAT_MAGIC_64 || header == FAT_CIGAM_64;

        if constexpr (std::endian::native == std::endian::little) {
            result._needs_byteswap = header == MH_CIGAM || header == MH_CIGAM_64 ||
                                     header == FAT_CIGAM || header == FAT_CIGAM_64 ||
                                     header == 'ra<!';
        } else {
            result._needs_byteswap = header == MH_MAGIC || header == MH_MAGIC_64 ||
                                     header == FAT_MAGIC || header == FAT_MAGIC_64 ||
                                     header == '!<ar';
        }

        if (result._format == file_details::format::macho) {
            std::uint32_t cputype{0};
            s.read(reinterpret_cast<char*>(&cputype), sizeof(cputype));
            if (result._needs_byteswap) {
                endian_swap(cputype);
            }
#if ORC_FEATURE(MACH_O)
            assert(((cputype & CPU_ARCH_ABI64) != 0) == result._is_64_bit);
            if (cputype == CPU_TYPE_X86) {
                result._arch = arch::x86;
            } else if (cputype == CPU_TYPE_X86_64) {
                result._arch = arch::x86_64;
            } else if (cputype == CPU_TYPE_ARM) {
                result._arch = arch::arm;
            } else if (cputype == CPU_TYPE_ARM64) {
                result._arch = arch::arm64;
            } else if (cputype == CPU_TYPE_ARM64_32) {
                result._arch = arch::arm64;
            } else {
                std::cerr << "WARN: Unknown Mach-O cputype\n";
            }
#endif // ORC_FEATURE(MACH_O)
        }

        return result;
    });
}

/**************************************************************************************************/

auto mmap_file(const std::filesystem::path& p) {
    // using result_type = std::unique_ptr<char, std::function<void(char*)>>;
    using result_type = std::shared_ptr<char>;
    auto size = std::filesystem::file_size(p);
    int fd = open(p.string().c_str(), O_RDONLY);
    void* ptr = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) return result_type();
    auto deleter = [_sz = size](char* x){ munmap(x, _sz); };
    return result_type(static_cast<char*>(ptr), std::move(deleter));
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/
// See https://en.wikipedia.org/wiki/LEB128
std::uint32_t uleb128(freader& s) {
    std::uint32_t result{0};
    std::size_t shift{0};

    while (true) {
        auto c = s.get();
        result |= (c & 0x7f) << shift;
        if (!(c & 0x80)) return result;
        shift += 7;
    }
}

/**************************************************************************************************/

std::int32_t sleb128(freader& s) {
    std::int32_t result{0};
    std::size_t shift{0};
    bool sign{false};

    while (true) {
        auto c = s.get();
        result |= (c & 0x7f) << shift;
        shift += 7;
        if (!(c & 0x80)) {
            sign = c & 0x40;
            break;
        }
    }

    constexpr auto size_k{sizeof(result) * 8};

    if (sign && shift < size_k) {
        result |= - (1 << shift);
    }

    return result;
}

/**************************************************************************************************/

void parse_file(const std::string& object_name,
                freader& s,
                std::istream::pos_type end_pos,
                callbacks callbacks) {
    auto detection = detect_file(s);

    switch (detection._format) {
        case file_details::format::unknown:
            throw std::runtime_error("unknown format");
        case file_details::format::macho:
            return read_macho(object_name, s, end_pos, std::move(detection), std::move(callbacks));
        case file_details::format::ar:
            return read_ar(object_name, s, end_pos, std::move(detection), std::move(callbacks));
        case file_details::format::fat:
            return read_fat(object_name, s, end_pos, std::move(detection), std::move(callbacks));
    }
}

/**************************************************************************************************/

freader::freader(const std::filesystem::path& p) :
    _buffer(mmap_file(p)),
    _f(_buffer.get()),
    _p(_f),
    _l(_p + std::filesystem::file_size(p)) {}

/**************************************************************************************************/
