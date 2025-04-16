// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/macho.hpp"

// stdc++
#include <sstream>

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
#include "orc/tracy.hpp"

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
                 macho_params&& params)
        : _ofd_index(ofd_index), _s(std::move(s)), _details(std::move(details)),
          _params(std::move(params)), _dwarf(ofd_index, copy(_s), copy(_details)) {
        if (params._mode == macho_reader_mode::invalid) {
            cerr_safe([&](auto& s) { s << "Invalid reader mode.\n"; });
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

    bool register_dies_mode() const { return _params._mode == macho_reader_mode::register_dies; }
    bool derive_dylibs_mode() const { return _params._mode == macho_reader_mode::derive_dylibs; }
    // bool odrv_reporting_mode() const { return _params._mode == macho_reader_mode::odrv_reporting; }

    void derive_dependencies();

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
    const macho_params _params;
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

    _params._register_dependencies(std::move(additional_object_files));
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

    // clang-format off
    if (derive_dylibs_mode()) {
        switch (command.cmd) {
            case LC_SEGMENT_64: { read_lc_segment_64(); } break;
            case LC_LOAD_DYLIB: { read_lc_load_dylib(); } break;
            case LC_RPATH: { read_lc_rpath(); } break;
            case LC_SYMTAB: { read_lc_symtab(); } break;
            default: { _s.seekg(command.cmdsize, std::ios::cur); } break;
        }
    } else {
        switch (command.cmd) {
            case LC_SEGMENT_64: { read_lc_segment_64(); } break;
            default: { _s.seekg(command.cmdsize, std::ios::cur); } break;
        }
    }
    // clang-format on
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

        if (log_level_at_least(settings::log_level::verbose)) {
            cerr_safe(
                [&](auto& s) { s << "Could not find dependent library: " + raw_path + "\n"; });
        }

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

    const std::filesystem::path loader_path =
        object_file_ancestry(_ofd_index)._ancestors[0].allocate_path().parent_path();

    std::vector<std::filesystem::path> resolved_dylibs;
    for (const auto& raw_dylib : _unresolved_dylibs) {
        auto resolved = resolve_dylib(raw_dylib, _params._executable_path, loader_path, _rpaths);
        if (!resolved) continue;
        resolved_dylibs.emplace_back(std::move(*resolved));
    }

    // Send these new-found dependencies to the main engine for ODR scanning.
    _params._register_dependencies(std::move(resolved_dylibs));
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
                macho_params params) {
    orc::do_work([_ancestry = std::move(ancestry), _s = std::move(s), _details = std::move(details),
                  _params = std::move(params)]() mutable {
        ZoneScopedN("read_macho");
#if ORC_FEATURE(TRACY)
        std::stringstream ss;
        ss << _ancestry;
        const auto path_annot = std::move(ss).str();
        ZoneText(path_annot.c_str(), path_annot.size());
#endif // ORC_FEATURE(TRACY)

        std::uint32_t ofd_index =
            static_cast<std::uint32_t>(object_file_register(std::move(_ancestry), copy(_details)));
        macho_reader macho(ofd_index, std::move(_s), std::move(_details), std::move(_params));

        if (macho.register_dies_mode()) {
            ++globals::instance()._metrics._object_file_count;
            macho.dwarf().process_all_dies();
        } else if (macho.derive_dylibs_mode()) {
            macho.derive_dependencies();
        } else {
            // If we're here, Something Bad has happened.
            std::terminate();
        }
    });
}

/**************************************************************************************************/

dwarf dwarf_from_macho(std::uint32_t ofd_index, macho_params params) {
    const auto& entry = object_file_fetch(ofd_index);
    freader s(entry._ancestry.begin()->allocate_path());

    s.seekg(entry._details._offset);

    return macho_reader(ofd_index, std::move(s), copy(entry._details), std::move(params)).dwarf();
}

/**************************************************************************************************/

namespace {

/**************************************************************************************************/
// Append `src` to the end of `dst` by destructively moving the items out of `src`.
// Preconditions:
//     - `dst` and `src` are not the same container.
// Postconditions:
//     - All elements in `src` will be moved-from.
//     - `src` itself will still be valid, even if the elements within it are not.
template <typename C>
void move_append(C& dst, C&& src) {
    dst.insert(dst.end(), std::move_iterator(src.begin()), std::move_iterator(src.end()));
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
// For an incoming `input_path`, this scans just that input and returns any dylibs it depends on.
// It does not scan those dylibs for additional dependencies. It also does not include `input_path`
// as one of the returned dependencies.
std::vector<std::filesystem::path> derive_immediate_dylibs(
    const std::filesystem::path& executable_path, const std::filesystem::path& input_path) {
    ZoneScoped;

    // `input_path` is not `loader_path` because it points to the binary to be scanned.
    // Therefore, `loader_path` should be the directory that contains `input_path`.
    if (!exists(input_path)) {
        if (log_level_at_least(settings::log_level::verbose)) {
            cerr_safe([&](auto& s) {
                s << "verbose: file " << input_path.string() << " does not exist\n";
            });
        }
        return std::vector<std::filesystem::path>();
    }

#if ORC_FEATURE(TRACY)
    const auto path_annot = input_path.string();
    ZoneText(path_annot.c_str(), path_annot.size());
#endif // ORC_FEATURE(TRACY)

    TracyLockable(std::mutex, dylib_result_mutex);
    std::vector<std::filesystem::path> result;
    freader input(input_path);
    macho_params params;
    params._mode = macho_reader_mode::derive_dylibs;
    params._executable_path = executable_path;
    params._register_dependencies = [&](std::vector<std::filesystem::path>&& p) {
        ZoneScopedN("register_dependencies");
        if (p.empty()) return;
        std::lock_guard m(dylib_result_mutex);
        move_append(result, std::move(p));
    };

    parse_file(input_path.string(), object_ancestry(), input, input.size(), std::move(params));

    orc::block_on_work();

    return make_sorted_unique(std::move(result));
}

/**************************************************************************************************/

std::vector<std::filesystem::path> derive_all_dylibs(const std::filesystem::path& binary) {
    ZoneScoped;

    const auto executable_path = binary.parent_path();
    std::vector<std::filesystem::path> scanned;
    std::vector<std::filesystem::path> pass(1, binary);

    if (log_level_at_least(settings::log_level::info)) {
        cout_safe([&](auto& s) {
            s << "info: scanning for dependencies of " << binary.filename() << "\n";
        });
    }

    while (true) {
        std::vector<std::filesystem::path> pass_dependencies;

        for (const auto& dependency : pass) {
            move_append(pass_dependencies, derive_immediate_dylibs(executable_path, dependency));
        }

        // The set of binaries scanned in this pass get appended to `scanned`
        move_append(scanned, std::move(pass));
        scanned = make_sorted_unique(std::move(scanned));

        // clean up the set of dependencies found in this pass.
        pass_dependencies = make_sorted_unique(std::move(pass_dependencies));

        // for the _next_ pass, we only want to scan files that are new,
        // those that are in `pass_dependencies` and _not_ in `scanned`.
        // If that set of files is empty, then we have found all our
        // dependencies, and can stop.
        pass = std::vector<std::filesystem::path>(); // ensure `pass` is valid and empty.
        std::set_difference(pass_dependencies.begin(), pass_dependencies.end(),
                            scanned.begin(), scanned.end(), std::back_inserter(pass));

        if (pass.empty()) {
            break;
        }

        if (log_level_at_least(settings::log_level::info)) {
            cout_safe([&](auto& s) {
                s << "info: scanning " << pass.size() << " more dependencies...\n";
            });
        }
    }

    if (log_level_at_least(settings::log_level::info)) {
        cout_safe(
            [&](auto& s) { s << "info: found " << scanned.size() << " total dependencies\n"; });
    }

    return scanned;
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

std::vector<std::filesystem::path> macho_derive_dylibs(
    const std::vector<std::filesystem::path>& binaries) {
    std::vector<std::filesystem::path> result;

    // For the purpose of the executable_path/loader_path relationships, we treat each binary
    // as independent of the others. That is, each root binary will be the `executable_path` for
    // its tree of dependencies.
    //
    // Then, yes, we smash them all together and treat them as one large binary with all its
    // dependencies. Otherwise we'd have to add a way to conduct multiple ORC scans per session,
    // which the app is not set up to do. We did warn the user we would do this, though.
    for (const auto& binary : binaries) {
        move_append(result, derive_all_dylibs(binary));
    }

    return make_sorted_unique(std::move(result));
}

/**************************************************************************************************/
