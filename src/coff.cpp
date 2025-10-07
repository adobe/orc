// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/coff.hpp"

// stdc++
#include <vector>

// adobe contract checks
#include "adobe/contract_checks.hpp"

// application
#include "orc/dwarf.hpp"
#include "orc/object_file_registry.hpp"
#include "orc/settings.hpp" // for globals

//--------------------------------------------------------------------------------------------------

namespace {

//--------------------------------------------------------------------------------------------------
//
// Relevant documentation:
//     - Portable Executable (PE) format:
//     https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
//     - image_file_header:
//     https://docs.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_file_header
//     - image_section_header:
//     https://docs.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_section_header
//

struct image_file_header {
    std::int16_t machine{0};
    std::int16_t section_count{0};
    std::int32_t datetimestamp{0};
    std::int32_t symbol_table_pointer{0};
    std::int32_t symbol_count{0};
    std::int16_t optional_header_size{0};
    std::int16_t characteristics{0};
};

static_assert(sizeof(image_file_header) == 20);

struct image_section_header {
    std::int8_t name[8]{0};
    union {
        std::int32_t physical_address{0};
        std::int32_t virtual_size;
    } misc;
    std::int32_t virtual_address{0};
    std::int32_t raw_data_size{0};
    std::int32_t raw_data_pointer{0};
    std::int32_t relocations_pointer{0};
    std::int32_t line_numbers_pointer{0};
    std::int16_t relocations_count{0};
    std::int16_t line_numbers_count{0};
    std::int32_t characteristics{0};
};

static_assert(sizeof(image_section_header) == 40);

struct section {
    image_section_header header;
    std::string actual_name;
};

//--------------------------------------------------------------------------------------------------
#if 0
/// Similar to strlen, except with an upper limit as to the size of the string.
/// APPARENTLY this is already available in some POSIX extensions \ macOS.
/// Keeping this around just in case.
std::size_t strnlen(const char* s, std::size_t n) {
    std::size_t result{0};
    for (; *s; ++s) {
        if (++result == n) {
            break;
        }
    }
    return result;
}
#endif
//--------------------------------------------------------------------------------------------------

} // namespace

//--------------------------------------------------------------------------------------------------

void read_coff(object_ancestry&& ancestry,
               freader& s,
               std::istream::pos_type end_pos,
               file_details details,
               reader_params params) {
    std::uint32_t ofd_index =
        static_cast<std::uint32_t>(object_file_register(std::move(ancestry), copy(details)));

    dwarf_from_coff(ofd_index, std::move(params)).process_all_dies();
}

//--------------------------------------------------------------------------------------------------

dwarf dwarf_from_coff(std::uint32_t ofd_index, reader_params params) {
    const auto& entry = object_file_fetch(ofd_index);
    freader s(entry._ancestry.begin()->allocate_path());
    dwarf dwarf(ofd_index, copy(s), copy(entry._details));

    s.seekg(entry._details._offset);

    // If you hit this, you're running ORC in a mode not supported by COFF.
    ADOBE_INVARIANT(params._mode == reader_mode::register_dies ||
                    params._mode == reader_mode::odrv_reporting);

    // The general format of COFF is:
    //     header
    //     section headers
    //     section data
    //     symbols
    //     strings
    // In our case, we're just looking for the DWARF data, which is housed
    // in one of the "raw data" blocks in COFF sections whose names are
    // the DWARF segments we are interested in (debug_info, debug_abbrev,
    // etc.) So we don't need to read anything beyond the section headers.

    const auto header = read_pod<image_file_header>(s);

    // According to the PE format docs there should be no optional header for object files.
    ADOBE_INVARIANT(header.optional_header_size == 0);

    // Grab the string table offset and size, which we'll need when deriving
    // the name of some of the sections we read below.
    const auto string_table_offset = header.symbol_table_pointer + header.symbol_count * 18;
    const auto string_table_size =
        temp_seek(s, string_table_offset, [&] { return read_pod<std::uint32_t>(s); });

    // Read the section headers. As we go, derive the actual section header
    // name, which may be in the string table. If the name is a DWARF segment,
    // add it to the DWARF processor.
    std::vector<section> sections(header.section_count);
    for (auto& section : sections) {
        s.read(section.header);
        const char* name = reinterpret_cast<char*>(&section.header.name[0]);
        if (*name != '/') {
            // strnlen is the same as strlen but with a string length upper limit.
            // Apparently its available via POSIX extension? Who knew. Not this guy.
            std::size_t len = strnlen(name, 8);
            section.actual_name = std::string(name, len);
        } else {
            ++name;
            int section_name_offset = std::atoi(name);
            ADOBE_INVARIANT(section_name_offset < string_table_size);
            section.actual_name = temp_seek(s, string_table_offset + section_name_offset,
                                            [&] { return s.read_c_string_view(); });
        }

        if (section.actual_name.starts_with(".debug")) {
            // std::cout << section.actual_name << '\n';
            dwarf.register_section(section.actual_name, section.header.raw_data_pointer,
                                   section.header.raw_data_size);
        }
    }

    if (params._mode == reader_mode::register_dies) {
        ++globals::instance()._object_file_count;
    }

    return dwarf;
}

//--------------------------------------------------------------------------------------------------
