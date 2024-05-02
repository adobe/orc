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

// application
#include "orc/async.hpp"
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
    exclusive, the callbacks provided determine which path the `macho_reader` should take. There is
    actually a third mode, which is during the ODRV reporting. In that mode we are neither scanning
    for dylibs nor DIEs, but are gathering more details about DIEs that we need to report on. In
    that case, both `_register_die_mode` and `_derive_dylib_mode` will be `false`.
*/
struct macho_reader {
    macho_reader(std::uint32_t ofd_index,
                 freader&& s,
                 file_details&& details,
                 callbacks&& callbacks)
        : _register_die_mode(static_cast<bool>(callbacks._register_die)),
          _derive_dylib_mode(static_cast<bool>(callbacks._derived_dependency)),
          _ofd_index(ofd_index), _s(std::move(s)), _details(std::move(details)),
          _derived_dependency(std::move(callbacks._derived_dependency)),
          _dwarf(ofd_index, copy(_s), copy(_details), std::move(callbacks._register_die)) {
        if (_register_die_mode && _derive_dylib_mode) {
            cerr_safe([&](auto& s) { s << "Only one of die or dylib scanning is allowed.\n"; });
            std::terminate();
        }
        populate_dwarf();
    }

    struct dwarf& dwarf() & {
        return _dwarf;
    }
    struct dwarf&& dwarf() && {
        return std::move(_dwarf);
    }

    void derive_dependencies();

    const bool _register_die_mode{false};
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
        endian_swap(lc.dylib.current_version);       // sufficient?
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
    std::vector<std::filesystem::path> additional_object_files;

    while (symbol_count--) {
        std::uint32_t entry_string_offset{0};

        if (_details._is_64_bit) {
            auto entry = read_pod<nlist_64>(_s);
            if (entry.n_type != N_OSO) continue;
            entry_string_offset = entry.n_un.n_strx;
        } else {
            auto entry = read_pod<struct nlist>(_s);
            if (entry.n_type != N_OSO) continue;
            entry_string_offset = entry.n_un.n_strx;
        }

        // TODO: Comparing the modified file time ensures the object file has not changed
        // since the application binary was linked. We should compare this time given to
        // us against the modified time of the file on-disk.
        // const auto modified_time = entry.n_value;

        std::filesystem::path path =
            temp_seek(_s, _details._offset + string_offset + entry_string_offset,
                      [&]() { return _s.read_c_string_view(); });

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

    temp_seek(_s, _details._offset + lc.symoff, [&]() { read_stabs(lc.nsyms, lc.stroff); });
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
            read_lc_segment_64();
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

std::optional<std::filesystem::path> resolve_dylib(std::string raw_path,
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
            std::optional<std::filesystem::path> candidate =
                resolve_dylib(tmp, executable_path, loader_path, rpaths);
            if (candidate && exists(*candidate)) {
                return candidate;
            }
        }

        cerr_safe([&](auto& s) { s << "Could not find dependent library: " + raw_path + "\n"; });
        return std::nullopt;
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

#warning `executable_path` somehow needs to make its way from `macho_derive_dylibs` to here.

    std::filesystem::path executable_path =
        object_file_ancestry(_ofd_index)._ancestors[0].allocate_path().parent_path();
    std::filesystem::path loader_path = executable_path;

    std::vector<std::filesystem::path> resolved_dylibs;
    for (const auto& raw_dylib : _unresolved_dylibs) {
        auto resolved = resolve_dylib(raw_dylib, executable_path, loader_path, _rpaths);
        if (!resolved) continue;
        resolved_dylibs.emplace_back(std::move(*resolved));
    }

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
    orc::do_work([_ancestry = std::move(ancestry), _s = std::move(s), _details = std::move(details),
                  _callbacks = callbacks]() mutable {
        std::uint32_t ofd_index =
            static_cast<std::uint32_t>(object_file_register(std::move(_ancestry), copy(_details)));
        macho_reader macho(ofd_index, std::move(_s), std::move(_details), copy(_callbacks));

        if (macho._register_die_mode) {
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

    callbacks callbacks{
        std::move(callback),
    };

    return macho_reader(ofd_index, std::move(s), copy(entry._details), std::move(callbacks))
        .dwarf();
}

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

template <typename C>
void move_append(C& dst, C&& src) {
    dst.insert(dst.end(), std::move_iterator(src.begin()), std::move_iterator(src.end()));
    src.clear();
}

/**************************************************************************************************/

std::vector<std::filesystem::path> make_sorted_unique(std::vector<std::filesystem::path>&& files) {
    // eliminate duplicate object files, if any. The discovered order of these things shouldn't
    // matter for the purposes of additional dylib scans or the ODR scan at the end.
    std::sort(files.begin(), files.end());
    auto new_end = std::unique(files.begin(), files.end());
    files.erase(new_end, files.end());
    return files;
}

/**************************************************************************************************/
// We have to assume that dependencies will circle back on themselves at some point, so we must have
// a criteria to know when we are "done". The way we do this is to check the size of the file list
// on every iteration. The moment the resulting list stops growing, we know we're done.
#if 0
std::vector<std::filesystem::path> recursive_dylib_scan(std::vector<std::filesystem::path>&& files) {
    std::vector<std::filesystem::path> result = files;
    std::vector<std::filesystem::path> last_pass = std::move(files);

    while (true) {
        last_pass = macho_derive_dylibs(last_pass);
        const auto last_result_size = result.size();
        move_append(result, copy(last_pass));
        result = make_sorted_unique(std::move(result));
        if (result.size() == last_result_size) break;
    }

    return result;
}
#endif
/**************************************************************************************************/

void macho_derive_dylibs(const std::filesystem::path& executable_path,
                         std::mutex& mutex,
                         std::vector<std::filesystem::path>& result) {
    if (!exists(executable_path)) {
        cerr_safe(
            [&](auto& s) { s << "file " << executable_path.string() << " does not exist\n"; });
        return;
    }

    freader input(executable_path);
    callbacks callbacks = {register_dies_callback(), [&_mutex = mutex, &_result = result](
                                                         std::vector<std::filesystem::path>&& p) {
                               if (p.empty()) return;
                               std::lock_guard<std::mutex> m(_mutex);
                               move_append(_result, std::move(p));
                           }};

    parse_file(executable_path.string(), object_ancestry(), input, input.size(),
               std::move(callbacks));
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

std::vector<std::filesystem::path> macho_derive_dylibs(
    const std::vector<std::filesystem::path>& root_binaries) {
    std::mutex result_mutex;
    std::vector<std::filesystem::path> result = root_binaries;

    for (const auto& input_path : root_binaries) {
        macho_derive_dylibs(input_path, result_mutex, result);
    }

    orc::block_on_work();

    return make_sorted_unique(std::move(result));
}

/**************************************************************************************************/
