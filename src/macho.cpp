// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/macho.hpp"

// system
#include <mach-o/loader.h>

// application
#include "orc/dwarf.hpp"
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
        default:
            // std::cerr << "Unhandled load command: " << command.cmd << '\n';
            s.seekg(command.cmdsize, std::ios::cur);
    }
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

void read_macho(std::string object_name,
                freader s,
                std::istream::pos_type end_pos,
                file_details details,
                callbacks callbacks) {
    callbacks._do_work([_object_name = std::move(object_name),
                        _s = std::move(s),
                        _details = std::move(details),
                        _callbacks = std::move(callbacks)]() mutable {
        ++globals::instance()._object_file_count;

        std::size_t load_command_sz{0};

        if (_details._is_64_bit) {
            auto header = read_pod<mach_header_64>(_s);
            if (_details._needs_byteswap) {
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
            auto header = read_pod<mach_header>(_s);
            if (_details._needs_byteswap) {
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
        dwarf dwarf(_object_name, _s, std::move(_details), std::move(_callbacks));

        for (std::size_t i = 0; i < load_command_sz; ++i) {
            read_load_command(_s, _details, dwarf);
        }

        dwarf.process();
    });
}

/**************************************************************************************************/
