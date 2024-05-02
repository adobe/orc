// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/macho.hpp"

// mach-o
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/stab.h>

// tbb
#include <tbb/concurrent_map.h>

// stlab
#include <stlab/concurrency/immediate_executor.hpp>

// application
#include "orc/dwarf.hpp"
#include "orc/object_file_registry.hpp"
#include "orc/orc.hpp" // for cerr_safe
#include "orc/settings.hpp"
#include "orc/str.hpp"

/**************************************************************************************************/

namespace {

/**************************************************************************************************/
/*
    This structure holds state while the Mach-O files are being read. Its goal is to 1) populate its
    dwarf field (for die processing within `dwarf.cpp`, or 2) derive dylib dependencies enumerated
    in the Mach-O segments of the file being read. The "switch" for which mode the reader is in is
    based on the callbacks it is given. Since die scanning and dylib scanning are mutually
    exclusive, the callbacks provided determine which path the `macho_reader` should take.
*/
struct macho_reader {
    macho_reader(std::uint32_t ofd_index,
                 freader&& s,
                 file_details&& details,
                 callbacks&& callbacks) :
        _process_die_mode(static_cast<bool>(callbacks._register_die)),
        _derive_dylib_mode(static_cast<bool>(callbacks._derived_dependency)),
        _ofd_index(ofd_index),
        _s(std::move(s)),
        _details(std::move(details)),
        _derived_dependency(std::move(callbacks._derived_dependency)),
        _dwarf(ofd_index, copy(_s), copy(_details), std::move(callbacks._register_die)) {
        if (_process_die_mode ^ _derive_dylib_mode) {
            cerr_safe([&](auto& s) { s << "Exactly one of die or dylib scanning is allowed.\n"; });
            std::terminate();
        }
        populate_dwarf();
    }

    struct dwarf& dwarf() & { return _dwarf; }
    struct dwarf&& dwarf() && { return std::move(_dwarf); }

    void derive_dependencies();

    const bool _process_die_mode{false};
    const bool _derive_dylib_mode{false};

private:
    void populate_dwarf();
    void read_load_command();
    void read_lc_segment_64();
    void read_lc_segment_64_section();
    void read_lc_load_dylib();
    void read_lc_rpath();
    void read_lc_symtab();
    void read_stabs(std::uint32_t symbol_count, std::uint32_t string_offset);

    const std::uint32_t _ofd_index{0};
    freader _s;
    const file_details _details;
    derived_dependency_callback _derived_dependency;
    std::vector<std::string> _unresolved_dylibs;
    std::vector<std::string> _rpaths;
    struct dwarf _dwarf; // must be last
};

/**************************************************************************************************/

void macho_reader::read_lc_segment_64_section() {
    auto section = read_pod<section_64>(_s);
    if (_details._needs_byteswap) {
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

    _dwarf.register_section(rstrip(section.sectname), _details._offset + section.offset,
                            section.size);
}

/**************************************************************************************************/

void macho_reader::read_lc_segment_64() {
    auto lc = read_pod<segment_command_64>(_s);
    if (_details._needs_byteswap) {
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
        read_lc_segment_64_section();
    }
}

/**************************************************************************************************/

void macho_reader::read_lc_load_dylib() {
    const auto command_start = _s.tellg();
    auto lc = read_pod<dylib_command>(_s);
    if (_details._needs_byteswap) {
        endian_swap(lc.cmd);
        endian_swap(lc.cmdsize);
        endian_swap(lc.dylib.name.offset);
        endian_swap(lc.dylib.timestamp);
        endian_swap(lc.dylib.current_version); // sufficient?
        endian_swap(lc.dylib.compatibility_version); // sufficient?
    }

    _unresolved_dylibs.emplace_back(_s.read_c_string_view());

    const auto padding = lc.cmdsize - (_s.tellg() - command_start);
    _s.seekg(padding, std::ios::cur);
}

/**************************************************************************************************/

void macho_reader::read_lc_rpath() {
    const auto command_start = _s.tellg();
    auto lc = read_pod<rpath_command>(_s);
    if (_details._needs_byteswap) {
        endian_swap(lc.cmd);
        endian_swap(lc.cmdsize);
        endian_swap(lc.path.offset);
    }

    _rpaths.emplace_back(_s.read_c_string_view());

    const auto padding = lc.cmdsize - (_s.tellg() - command_start);
    _s.seekg(padding, std::ios::cur);
}

/**************************************************************************************************/
/*
    This is specifically in relation to the dylib scanning mode, where we're looking at a final
    linked artifact that enumerates the dylibs it depends upon.

    Debug builds on macOS do not embed symbol information into the binary by default. Rather, there
    are "debug maps" that link from the artifact to the `.o` files used to make it where the symbol
    information resides. At the time the application is debugged, the debug maps are used to derive
    the symbols of the application by pulling them from the relevant object files.

    Because of this funky artifact->debug map->object file relationship, ORC must also support debug
    maps in order to derive and scan the symbols present in a linked artifact. This also means the
    final linked binary is not sufficient for a scan; you _also_ need its associated object files
    present, _and_ in the location specified by the debug map.

    Apple's "Lazy" DWARF Scheme: https://wiki.dwarfstd.org/Apple%27s_%22Lazy%22_DWARF_Scheme.md
    See: https://stackoverflow.com/a/12827463/153535
    See: https://sourceware.org/gdb/current/onlinedocs/stabs.html/
*/
void macho_reader::read_stabs(std::uint32_t symbol_count, std::uint32_t string_offset) {
    if (!_details._is_64_bit) throw std::runtime_error("Need support for non-64-bit STABs.");
    std::vector<std::filesystem::path> additional_object_files;
    while (symbol_count--) {
        auto entry = read_pod<nlist_64>(_s);
        if (entry.n_type != N_OSO) continue;
        // TODO: Comparing the modified file time ensures the object file has not changed
        // since the application binary was linked. We should compare this time given to
        // us against the modified time of the file on-disk.
        // const auto modified_time = entry.n_value;

        std::filesystem::path path = temp_seek(_s, _details._offset + string_offset + entry.n_un.n_strx, [&](){
            return _s.read_c_string_view();
        });

        // Some entries have been observed to contain the `.o` file as a parenthetical to the
        // `.a` file that contains it. e.g., `/path/to/bar.a(foo.o)`. For our purposes we'll
        // trim off the parenthetical and include the entire `.a` file. Although this could
        // introduce extra symbols, they are likely to be included by other STAB entries
        // anyhow.
        //
        // TL;DR: If the filename has an open parentheses in it, remove it and all that
        // comes after it.
        std::string filename = path.filename().string();
        if (const auto pos = filename.find('('); pos != std::string::npos) {
            path = path.parent_path() / filename.substr(0, pos);
        }

        additional_object_files.push_back(std::move(path));
    }
    // don't think I need these here, as we should sort/make unique the total set
    // of additional object files within `orc_process`. Saving until I'm sure.
    //
    // std::sort(additional_object_files.begin(), additional_object_files.end());
    // auto new_end = std::unique(additional_object_files.begin(), additional_object_files.end());
    // additional_object_files.erase(new_end, additional_object_files.end());
    _derived_dependency(std::move(additional_object_files));
}

void macho_reader::read_lc_symtab() {
    auto lc = read_pod<symtab_command>(_s);
    if (_details._needs_byteswap) {
        endian_swap(lc.cmd);
        endian_swap(lc.cmdsize);
        endian_swap(lc.symoff);
        endian_swap(lc.nsyms);
        endian_swap(lc.stroff);
        endian_swap(lc.strsize);
    }

    temp_seek(_s, _details._offset + lc.symoff, [&](){
        read_stabs(lc.nsyms, lc.stroff);
    });
}

/**************************************************************************************************/

void macho_reader::read_load_command() {
    auto command = temp_seek(_s, [&] {
        auto command = read_pod<load_command>(_s);
        if (_details._needs_byteswap) {
            endian_swap(command.cmd);
            endian_swap(command.cmdsize);
        }
        return command;
    });

    switch (command.cmd) {
        case LC_SEGMENT_64: {
            if (_process_die_mode) {
                read_lc_segment_64();
            }
        } break;
        case LC_LOAD_DYLIB: {
            if (_derive_dylib_mode) {
                read_lc_load_dylib();
            }
        } break;
        case LC_RPATH: {
            if (_derive_dylib_mode) {
                read_lc_rpath();
            }
        } break;
        case LC_SYMTAB: {
            if (_derive_dylib_mode) {
                read_lc_symtab();
            }
        } break;
        default: {
            _s.seekg(command.cmdsize, std::ios::cur);
        } break;
    }
}

/**************************************************************************************************/

std::filesystem::path resolve_dylib(std::string raw_path,
                                    const std::filesystem::path& executable_path,
                                    const std::filesystem::path& loader_path,
                                    const std::vector<std::string>& rpaths) {
    constexpr std::string_view executable_path_k = "@executable_path";
    constexpr std::string_view loader_path_k = "@loader_path";
    constexpr std::string_view rpath_k = "@rpath";

    if (raw_path.starts_with(executable_path_k)) {
        raw_path.replace(0, executable_path_k.size(), executable_path.string());
    } else if (raw_path.starts_with(loader_path_k)) {
        raw_path.replace(0, loader_path_k.size(), loader_path.string());
    } else if (raw_path.starts_with(rpath_k)) {
        // search rpaths until the desired dylib is actually found.
        for (const auto& rpath : rpaths) {
            std::string tmp = raw_path;
            tmp.replace(0, rpath_k.size(), rpath);
            std::filesystem::path candidate = resolve_dylib(tmp, executable_path, loader_path, rpaths);
            if (exists(candidate)) {
                return candidate;
            }
        }
        throw std::runtime_error("Could not find dependent library: " + raw_path);
    }

    return raw_path;
}

/**************************************************************************************************/

void macho_reader::derive_dependencies() {
    // See https://itwenty.me/posts/01-understanding-rpath/
    // `@executable_path` resolves to the path of the directory containing the executable.
    // `@loader_path` resolves to the path of the client doing the loading.
    // For executables, `@loader_path` and `@executable_path` mean the same thing.
    // TODO: (fosterbrereton) We're going to have to nest this search, aren't we?
    // If so, that means we'll need to track the originating file and use it as
    // `executable_path`, and then `loader_path` will follow wherever the nesting goes.

    std::filesystem::path executable_path = object_file_ancestry(_ofd_index)._ancestors[0].allocate_path().parent_path();
    std::filesystem::path loader_path = executable_path;
    std::vector<std::filesystem::path> resolved_dylibs;
    std::transform(_unresolved_dylibs.begin(), _unresolved_dylibs.end(), std::back_inserter(resolved_dylibs), [&](const auto& raw_dylib){
        return resolve_dylib(raw_dylib, executable_path, loader_path, _rpaths);
    });

    // Send these back to the main engine for ODR scanning processing.
    _derived_dependency(std::move(resolved_dylibs));
}

/**************************************************************************************************/

void macho_reader::populate_dwarf() {
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

    for (std::size_t i = 0; i < load_command_sz; ++i) {
        read_load_command();
    }
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
        std::uint32_t ofd_index = static_cast<std::uint32_t>(object_file_register(std::move(_ancestry), copy(_details)));
        macho_reader macho(ofd_index, std::move(_s), std::move(_details), copy(_callbacks));

        if (macho._process_die_mode) {
            ++globals::instance()._object_file_count;
            macho.dwarf().process_all_dies();
        } else if (macho._derive_dylib_mode) {
            macho.derive_dependencies();
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

    return macho_reader(ofd_index, std::move(s), copy(entry._details), std::move(callbacks)).dwarf();
}

/**************************************************************************************************/

std::vector<std::filesystem::path> macho_derive_dylibs(const std::filesystem::path& root_binary) {
    if (!exists(root_binary)) {
        cerr_safe([&](auto& s) { s << "file " << root_binary.string() << " does not exist\n"; });
        return std::vector<std::filesystem::path>();
    }

    freader input(root_binary);
    std::vector<std::filesystem::path> result;
    callbacks callbacks = {
        register_dies_callback(),
        stlab::immediate_executor, // don't subdivide or reschedule sub-work during this scan.
        [&_result = result](std::vector<std::filesystem::path>&& p){
            move_append(_result, p);
        }
    };

    parse_file(root_binary.string(), object_ancestry(), input, input.size(), std::move(callbacks));

    return result;
}

/**************************************************************************************************/
