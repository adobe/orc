// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/macho.hpp"

// mach-o
#include <mach-o/loader.h>

// tbb
#include <tbb/concurrent_map.h>

// application
#include "orc/dwarf.hpp"
#include "orc/object_file_registry.hpp"
#include "orc/settings.hpp"
#include "orc/str.hpp"

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

void read_lc_segment_64_section(freader& s, const file_details& details, dwarf& dwarf) {
    auto section = read_pod<section_64>(s);
    if (details._needs_byteswap) {
        // endian_swap(section.sectname[16]);
        // endian_swap(section.segname[16]);
        endian_swap(section.addr);
        endian_swap(section.size);
        endian_swap(section.offset);
        endian_swap(section.align);
        endian_swap(section.reloff);
        endian_swap(section.nreloc);
        endian_swap(section.flags);
        // endian_swap(section.reserved1);
        // endian_swap(section.reserved2);
        // endian_swap(section.reserved3);
    }

    if (rstrip(section.segname) != "__DWARF") return;

    dwarf.register_section(rstrip(section.sectname), details._offset + section.offset,
                           section.size);
}

/**************************************************************************************************/

void read_lc_segment_64(freader& s, const file_details& details, dwarf& dwarf) {
    auto lc = read_pod<segment_command_64>(s);
    if (details._needs_byteswap) {
        endian_swap(lc.cmd);
        endian_swap(lc.cmdsize);
        // endian_swap(lc.segname);
        endian_swap(lc.vmaddr);
        endian_swap(lc.vmsize);
        endian_swap(lc.fileoff);
        endian_swap(lc.filesize);
        endian_swap(lc.maxprot);
        endian_swap(lc.initprot);
        endian_swap(lc.nsects);
        endian_swap(lc.flags);
    }

    for (std::size_t i = 0; i < lc.nsects; ++i) {
        read_lc_segment_64_section(s, details, dwarf);
    }
}

/**************************************************************************************************/

void read_lc_load_dylib(freader& s, const file_details& details, dwarf& dwarf) {
    const auto command_start = s.tellg();
    auto lc = read_pod<dylib_command>(s);
    if (details._needs_byteswap) {
        endian_swap(lc.cmd);
        endian_swap(lc.cmdsize);
        endian_swap(lc.dylib.name.offset);
        endian_swap(lc.dylib.timestamp);
        endian_swap(lc.dylib.current_version); // sufficient?
        endian_swap(lc.dylib.compatibility_version); // sufficient?
    }

    const std::string_view dylib_path = s.read_c_string_view();
    dwarf.register_dylib(std::string(dylib_path));

    const auto padding = lc.cmdsize - (s.tellg() - command_start);
    s.seekg(padding, std::ios::cur);
}

/**************************************************************************************************/

void read_lc_rpath(freader& s, const file_details& details, dwarf& dwarf) {
    const auto command_start = s.tellg();
    auto lc = read_pod<rpath_command>(s);
    if (details._needs_byteswap) {
        endian_swap(lc.cmd);
        endian_swap(lc.cmdsize);
        endian_swap(lc.path.offset);
    }

    const std::string_view dylib_path = s.read_c_string_view();
    dwarf.register_rpath(std::string(dylib_path));

    const auto padding = lc.cmdsize - (s.tellg() - command_start);
    s.seekg(padding, std::ios::cur);
}

/**************************************************************************************************/

struct symtab_command {
	std::uint32_t cmd;
	std::uint32_t cmdsize;
	std::uint32_t symoff;
	std::uint32_t nsyms;
	std::uint32_t stroff;
	std::uint32_t strsize;
};

/*
    The symtab_command contains the offsets and sizes of the link-edit 4.3BSD
    "stab" style symbol table information as described in the header files
    <nlist.h> and <stab.h>.
*/
void read_stabs(freader& s,
                const file_details& details,
                dwarf& dwarf,
                std::uint32_t symbol_count) {
}

void read_lc_symtab(freader& s, const file_details& details, dwarf& dwarf) {
    auto lc = read_pod<symtab_command>(s);
    if (details._needs_byteswap) {
        endian_swap(lc.cmd);
        endian_swap(lc.cmdsize);
        endian_swap(lc.symoff);
        endian_swap(lc.nsyms);
        endian_swap(lc.stroff);
        endian_swap(lc.strsize);
    }

    temp_seek(s, lc.symoff, [&](){
        read_stabs(s, details, dwarf, lc.nsyms);
    });
}

/**************************************************************************************************/

struct load_command {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
};

/**************************************************************************************************/

void read_load_command(freader& s, const file_details& details, dwarf& dwarf) {
    auto command = temp_seek(s, [&] {
        auto command = read_pod<load_command>(s);
        if (details._needs_byteswap) {
            endian_swap(command.cmd);
            endian_swap(command.cmdsize);
        }
        return command;
    });

    switch (command.cmd) {
        case LC_SEGMENT_64:
            read_lc_segment_64(s, details, dwarf);
            break;
        case LC_LOAD_DYLIB:
            read_lc_load_dylib(s, details, dwarf);
            break;
        case LC_RPATH:
            read_lc_rpath(s, details, dwarf);
            break;
        case LC_SYMTAB:
            read_lc_symtab(s, details, dwarf);
            break;
        default:
            s.seekg(command.cmdsize, std::ios::cur);
    }
}

/**************************************************************************************************/

struct mach_header_64 {
    std::uint32_t magic{0};
    cpu_type_t cputype{0};
    cpu_subtype_t cpusubtype{0};
    std::uint32_t filetype{0};
    std::uint32_t ncmds{0};
    std::uint32_t sizeofcmds{0};
    std::uint32_t flags{0};
    std::uint32_t reserved{0};
};

struct mach_header {
    std::uint32_t magic;
    cpu_type_t cputype;
    cpu_subtype_t cpusubtype;
    std::uint32_t filetype;
    std::uint32_t ncmds;
    std::uint32_t sizeofcmds;
    std::uint32_t flags;
};

/**************************************************************************************************/

dwarf dwarf_from_macho(std::uint32_t ofd_index,
                       freader&& s,
                       file_details&& details,
                       callbacks&& callbacks) {
    std::size_t load_command_sz{0};

    if (details._is_64_bit) {
        auto header = read_pod<mach_header_64>(s);
        if (details._needs_byteswap) {
            endian_swap(header.magic);
            endian_swap(header.cputype);
            endian_swap(header.cpusubtype);
            endian_swap(header.filetype);
            endian_swap(header.ncmds);
            endian_swap(header.sizeofcmds);
            endian_swap(header.flags);
            endian_swap(header.reserved);
        }
        load_command_sz = header.ncmds;
    } else {
        auto header = read_pod<mach_header>(s);
        if (details._needs_byteswap) {
            endian_swap(header.magic);
            endian_swap(header.cputype);
            endian_swap(header.cpusubtype);
            endian_swap(header.filetype);
            endian_swap(header.ncmds);
            endian_swap(header.sizeofcmds);
            endian_swap(header.flags);
        }
        load_command_sz = header.ncmds;
    }

    // REVISIT: (fbrereto) I'm not happy that dwarf is an out-arg to read_load_command.
    // Maybe pass in some kind of lambda that'll get called when a relevant DWARF section
    // is found? A problem for later...
    dwarf dwarf(ofd_index, copy(s), copy(details), std::move(callbacks));

    for (std::size_t i = 0; i < load_command_sz; ++i) {
        read_load_command(s, details, dwarf);
    }

    return dwarf;
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

void read_macho(object_ancestry&& ancestry,
                freader s,
                std::istream::pos_type end_pos,
                file_details details,
                callbacks callbacks) {
    callbacks._do_work([_ancestry = std::move(ancestry), _s = std::move(s),
                        _details = std::move(details),
                        _callbacks = callbacks]() mutable {
        const bool process_die_mode = static_cast<bool>(_callbacks._register_die);
        const bool derive_dylib_mode = static_cast<bool>(_callbacks._derived_dependency);

        if (process_die_mode) {
            ++globals::instance()._object_file_count;
        }

        std::uint32_t ofd_index = static_cast<std::uint32_t>(object_file_register(std::move(_ancestry), copy(_details)));
        dwarf dwarf = dwarf_from_macho(ofd_index, std::move(_s), std::move(_details), copy(_callbacks));

        if (process_die_mode) {
            dwarf.process_all_dies();
        } else if (derive_dylib_mode) {
            dwarf.derive_dependencies();
        } else {
            // If we're here, Something Bad has happened.
            std::terminate();
        }
    });
}

/**************************************************************************************************/

dwarf dwarf_from_macho(std::uint32_t ofd_index, register_dies_callback&& callback) {
    const auto& entry = object_file_fetch(ofd_index);
    freader s(entry._ancestry.begin()->allocate_path());

    s.seekg(entry._details._offset);

    callbacks callbacks {
        std::move(callback),
    };

    return dwarf_from_macho(ofd_index, std::move(s), copy(entry._details),
                            std::move(callbacks));
}

/**************************************************************************************************/

std::vector<std::filesystem::path> macho_derive_dylibs(const std::filesystem::path& root_binary) {
    return std::vector<std::filesystem::path>();
}

/**************************************************************************************************/
