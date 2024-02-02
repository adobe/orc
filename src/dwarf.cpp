// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/dwarf.hpp"

// stdc++
#include <list>
#include <unordered_map>
#include <vector>

// application
#include "orc/dwarf_structs.hpp"
#include "orc/features.hpp"
#include "orc/object_file_registry.hpp"
#include "orc/settings.hpp"
#include "orc/tracy.hpp"

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

std::uint32_t form_length(dw::form f, freader& s) {
    static constexpr std::uint32_t length_size_k{4}; // REVISIT: (fosterbrereton) 8 on 64bit DWARF

    auto leb_block = [&] {
        return temp_seek(s, [&] {
            auto start = s.tellg();
            auto length = uleb128(s);
            auto leb_length = s.tellg() - start;
            return length - leb_length;
        });
    };

    switch (f) {
        case dw::form::addr:
            return 8;
        case dw::form::data2:
            return 2;
        case dw::form::data4:
            return 4;
        case dw::form::data8:
            return 8;
        case dw::form::string:
            assert(false);
            return 0; // TODO: read NTSB from s
        case dw::form::block:
            return static_cast<std::uint32_t>(leb_block());
        case dw::form::block1:
            // for block1, block2, and block4, we read the N-byte prefix, and then send back
            // its value as the length of the form. This causes the passover to skip over the
            // value, putting the read head at the correct place. So the returned length isn't
            // technically correct, but the net result is what we want.
            return read_pod<std::uint8_t>(s);
        case dw::form::block2:
            return read_pod<std::uint16_t>(s);
        case dw::form::block4:
            return read_pod<std::uint32_t>(s);
        case dw::form::data1:
            return 1;
        case dw::form::flag:
            return 1;
        case dw::form::sdata:
            return sleb128(s); // length of LEB _not_ included
        case dw::form::strp:
            return length_size_k;
        case dw::form::udata:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::ref_addr:
            return length_size_k;
        case dw::form::ref1:
            return 1;
        case dw::form::ref2:
            return 2;
        case dw::form::ref4:
            return 4;
        case dw::form::ref8:
            return 8;
        case dw::form::ref_udata:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::indirect:
            // For attributes with this form, the attribute value itself in the .debug_info
            // section begins with an unsigned LEB128 number that represents its form.
            assert(false);
            return 0; // TODO: (fbrereto)
        case dw::form::sec_offset:
            return length_size_k;
        case dw::form::exprloc:
            return static_cast<std::uint32_t>(leb_block());
        case dw::form::flag_present:
            return 0;
        case dw::form::strx:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::addrx:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::ref_sup4:
            return 4;
        case dw::form::strp_sup:
            return length_size_k;
        case dw::form::data16:
            return 16;
        case dw::form::line_strp:
            return length_size_k;
        case dw::form::ref_sig8:
            return 8;
        case dw::form::implicit_const:
            return 0;
        case dw::form::loclistx:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::rnglistx:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::ref_sup8:
            return 8;
        case dw::form::strx1:
            return 1;
        case dw::form::strx2:
            return 2;
        case dw::form::strx3:
            return 4;
        case dw::form::strx4:
            return 4;
        case dw::form::addrx1:
            return 1;
        case dw::form::addrx2:
            return 2;
        case dw::form::addrx3:
            return 4;
        case dw::form::addrx4:
            return 4;
        case dw::form::gnu_addr_index:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::gnu_str_index:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::gnu_ref_alt:
            return length_size_k;
        case dw::form::gnu_strp_alt:
            return length_size_k;
    }
}

/**************************************************************************************************/

struct section {
    std::size_t _offset{0};
    std::size_t _size{0};

    bool valid() const { return _offset != 0 && _size != 0; }
};

/**************************************************************************************************/
// An abbreviation (abbreviated 'abbrev' througout a lot of the DWARF spec) is a template of sorts.
// Think of it like a cookie cutter that needs to get stamped on some dough to make an actual
// cookie. Only in this case instead of a cookie, it'll make a DIE (DWARF Information Entry.)
struct abbrev {
    std::size_t _g{0};
    std::uint32_t _code{0};
    dw::tag _tag{0};
    bool _has_children{false};
    std::vector<attribute> _attributes;

    void read(freader& s);
};

void abbrev::read(freader& s) {
    _g = s.tellg();
    _code = uleb128(s);
    _tag = static_cast<dw::tag>(uleb128(s));
    _has_children = read_pod<bool>(s);
    while (true) {
        attribute entry;
        entry.read(s);
        if (entry._name == dw::at::none) break;
        _attributes.push_back(std::move(entry));
    }
}

/**************************************************************************************************/

struct file_name {
    std::string_view _name;
    std::uint32_t _directory_index{0};
    std::uint32_t _mod_time{0};
    std::uint32_t _file_length{0};
};

/**************************************************************************************************/

bool has_flag_attribute(const attribute_sequence& attributes, dw::at name) {
    return attributes.has_uint(name) && attributes.uint(name) == 1;
}

/**************************************************************************************************/

std::size_t die_hash(const die& d, const attribute_sequence& attributes) {
    ZoneScoped;

    // Ideally, tag would not be part of this hash and all symbols, regardless of tag, would be
    // unique. However, that fails in at least one case:
    //
    //     typedef struct S {} S;
    //
    // This results in both a `typedef` element and a `struct` element, with the same symbol path,
    // but which is not an ODRV.
    //
    // On the other hand, including tag in the hash results in missed ODRVs in cases like:
    //
    //    struct S1 {}
    //    ...
    //    class S1 { int i; }
    //
    // which results in a `struct` element and a `class` element with the same symbol path, but
    // differing definitions, which _is_ an ODRV.
    //
    // So, we will include the tag in the hash, but combine the tag values for `struct` and `class`
    // into a single value.
    auto tag = d._tag == dw::tag::structure_type ? dw::tag::class_type : d._tag;

    // clang-tidy off
    return orc::hash_combine(0, static_cast<std::size_t>(d._arch), static_cast<std::size_t>(tag),
                             has_flag_attribute(attributes, dw::at::declaration), d._path.hash());
    // clang-tidy on
};

/**************************************************************************************************/

struct cu_header {
    std::uint64_t _length{0}; // 4 bytes (or 12 if extended length is used.)
    bool _is_64_bit{false};
    std::uint16_t _version{0};
    std::uint64_t _debug_abbrev_offset{0}; // 4 (!_is_64_bit) or 8 (_is_64_bit) bytes
    std::uint32_t _address_size{0};

    void read(freader& s, bool needs_byteswap);
};

void cu_header::read(freader& s, bool needs_byteswap) {
    _length = read_pod<std::uint32_t>(s, needs_byteswap);

    if (_length >= 0xfffffff0) {
        // REVISIT: (fbrereto) handle extended length / DWARF64
        // For DWARF64 `_length` will be 0xffffffff.
        // See section 7.5.1.1 on how to handle this.
        throw std::runtime_error("unsupported length / DWARF64");
    }

    _version = read_pod<std::uint16_t>(s, needs_byteswap);

    // note the read_pod types differ.
    if (_is_64_bit) {
        _debug_abbrev_offset = read_pod<std::uint64_t>(s, needs_byteswap);
    } else {
        _debug_abbrev_offset = read_pod<std::uint32_t>(s, needs_byteswap);
    }

    _address_size = read_pod<std::uint8_t>(s);
}

/**************************************************************************************************/

struct line_header {
    // DWARF spec section 6.2.4 The Line Number Program Header.
    // Note this will change for DWARF5, so we need to look out
    // for DWARF data that uses the new version number and
    // account for it differently.
    std::uint64_t _length{0}; // 4 (DWARF) or 8 (DWARF64) bytes
    std::uint16_t _version{0};
    std::uint32_t _header_length{0}; // 4 (DWARF) or 8 (DWARF64) bytes
    std::uint32_t _min_instruction_length{0};
    std::uint32_t _max_ops_per_instruction{0}; // DWARF4 or greater
    std::uint32_t _default_is_statement{0};
    std::int32_t _line_base{0};
    std::uint32_t _line_range{0};
    std::uint32_t _opcode_base{0};
    std::vector<std::uint32_t> _standard_opcode_lengths;
    std::vector<std::string_view> _include_directories;
    std::vector<file_name> _file_names;

    void read(freader& s, bool needs_byteswap);
};

void line_header::read(freader& s, bool needs_byteswap) {
    _length = read_pod<std::uint32_t>(s, needs_byteswap);
    if (_length >= 0xfffffff0) {
        // REVISIT: (fbrereto) handle extended length / DWARF64
        throw std::runtime_error("unsupported length");
    }
    _version = read_pod<std::uint16_t>(s, needs_byteswap);
    if (_version > 4) {
        // REVISIT: (fbrereto) handle DWARF5 and later.
        throw std::runtime_error("unhandled DWARF version (" + std::to_string(_version) + ")");
    }
    _header_length = read_pod<std::uint32_t>(s, needs_byteswap);
    _min_instruction_length = read_pod<std::uint8_t>(s);
    if (_version >= 4) {
        _max_ops_per_instruction = read_pod<std::uint8_t>(s);
    }
    _default_is_statement = read_pod<std::uint8_t>(s);
    _line_base = read_pod<std::int8_t>(s);
    _line_range = read_pod<std::uint8_t>(s);
    _opcode_base = read_pod<std::uint8_t>(s);

    for (std::size_t i{0}; i < (_opcode_base - 1); ++i) {
        _standard_opcode_lengths.push_back(read_pod<std::int8_t>(s));
    }

    while (true) {
        auto cur_directory = s.read_c_string_view();
        if (cur_directory.empty()) break;
        _include_directories.push_back(cur_directory);
    }

    // REVIST (fosterbrereton): The reading here isn't entirely accurate. The current code stops the
    // first time an empty name is found, and interprets that as the end of the file names (and thus
    // the `line_header`). However, the spec (as the end of section 6.2.4) states "A compiler may
    // generate a single null byte for the file names field and define file names using the
    // extended opcode DW_LNE_define_file." This loop, then, should iterate through the end of the
    // defined size of `_header_length` instead of using an empty name as a sentry. Any additional
    // null bytes should be interpreted as a placeholder file name description. (Admittedly, I
    // haven't seen one of these in the wild yet.)
    while (true) {
        file_name cur_file_name;
        cur_file_name._name = s.read_c_string_view();
        if (cur_file_name._name.empty()) break;
        cur_file_name._directory_index = uleb128(s);
        cur_file_name._mod_time = uleb128(s);
        cur_file_name._file_length = uleb128(s);
        _file_names.push_back(std::move(cur_file_name));
    }
}

/**************************************************************************************************/

std::size_t fatal_attribute_hash(const attribute_sequence& attributes) {
    ZoneScoped;

    // We only hash the attributes that could contribute to an ODRV. We also sort that set of
    // attributes by name to make sure the hashing is consistent regardless of attribute order.
    constexpr const std::size_t max_names_k{32};
    std::array<dw::at, max_names_k> names;
    std::size_t count{0};

    for (const auto& attr : attributes) {
        if (nonfatal_attribute(attr._name)) continue;
        if (count >= max_names_k) {
            throw std::runtime_error("fatal_attribute_hash names overflow");
        }
        names[count++] = attr._name;
    }
    std::sort(&names[0], &names[count]);

    std::size_t h{0};

    std::for_each_n(&names[0], count, [&](const auto& name) {
        // If this assert fires, it means an attribute's value was passed over during evaluation,
        // but it was necessary for ODRV evaluation after all. The fix is to improve the attribute
        // form evaluation engine such that this attribute's value is no longer passed over.
        const auto& attribute = attributes.get(name);
        assert(!attributes.has(name, attribute_value::type::passover));
        h = orc::hash_combine(h, attribute._value.hash());
    });

    return h;
}

/**************************************************************************************************/

bool skip_tagged_die(const die& d) {
    static const dw::tag skip_tags[] = {
        dw::tag::compile_unit,
        dw::tag::partial_unit,
        dw::tag::variable,
        dw::tag::formal_parameter,
        dw::tag::template_type_parameter,
    };
    static const auto first = std::begin(skip_tags);
    static const auto last = std::end(skip_tags);

    return std::find(first, last, d._tag) != last;
}

/**************************************************************************************************/

enum class process_mode {
    complete,
    single,
};

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

struct dwarf::implementation {
    implementation(std::uint32_t ofd_index,
                   freader&& s,
                   file_details&& details,
                   register_dies_callback&& callback)
        : _s(std::move(s)), _details(std::move(details)), _register_dies(std::move(callback)),
          _ofd_index(ofd_index) {}

    void register_section(const std::string& name, std::size_t offset, std::size_t size);

    bool register_sections_done();

    void process_all_dies();

    bool skip_die(die& d, const attribute_sequence& attributes);

    die_pair fetch_one_die(std::size_t debug_info_offset);

    template <class T>
    T read();

    std::uint64_t read64();
    std::uint32_t read32();
    std::uint32_t read16();
    std::uint32_t read8();
    std::uint32_t read_uleb();
    std::int32_t read_sleb();

    void read_abbreviations();
    void read_lines(std::size_t header_offset);
    const abbrev& find_abbreviation(std::uint32_t code) const;

    pool_string read_debug_str(std::size_t offset);

    void path_identifier_push();
    void path_identifier_set(pool_string name);
    void path_identifier_pop();
    std::string qualified_symbol_name(const die& d, const attribute_sequence& attributes) const;

    attribute process_attribute(const attribute& attr, std::size_t cur_die_offset);
    attribute_value process_form(const attribute& attr, std::size_t cur_die_offset);
    attribute_value evaluate_exprloc(std::uint32_t expression_size);

    pool_string die_identifier(const die& a, const attribute_sequence& attributes) const;

    die_pair offset_to_die_pair(std::size_t offset);
    attribute_sequence offset_to_attribute_sequence(std::size_t offset);

    pool_string resolve_type(attribute type);

    die_pair abbreviation_to_die(std::size_t die_address, process_mode mode);

    freader _s;
    file_details _details;
    register_dies_callback _register_dies;
    std::vector<abbrev> _abbreviations;
    std::vector<pool_string> _path;
    std::vector<pool_string> _decl_files;
    std::unordered_map<std::size_t, pool_string> _type_cache;
    std::unordered_map<std::size_t, pool_string> _debug_str_cache;
    cu_header _cu_header;
    std::size_t _cu_address{0};
    std::uint32_t _ofd_index{0}; // index to the obj_registry in macho.cpp
    section _debug_abbrev;
    section _debug_info;
    section _debug_line;
    section _debug_str;
    bool _ready{false};
};

/**************************************************************************************************/

template <class T>
T dwarf::implementation::read() {
    return read_pod<T>(_s, _details._needs_byteswap);
}

std::uint64_t dwarf::implementation::read64() { return read<std::uint64_t>(); }

std::uint32_t dwarf::implementation::read32() { return read<std::uint32_t>(); }

std::uint32_t dwarf::implementation::read16() { return read<std::uint16_t>(); }

std::uint32_t dwarf::implementation::read8() { return read<std::uint8_t>(); }

std::uint32_t dwarf::implementation::read_uleb() { return uleb128(_s); }

std::int32_t dwarf::implementation::read_sleb() { return sleb128(_s); }

/**************************************************************************************************/

void dwarf::implementation::register_section(const std::string& name,
                                             std::size_t offset,
                                             std::size_t size) {
    // You've called process or fetch once already, and are trying to register more sections.
    // Instead, the section registration must be complete and cannot be revisited.
    assert(!_ready);

    if (name == "__debug_str") {
        _debug_str = section{offset, size};
    } else if (name == "__debug_info") {
        _debug_info = section{offset, size};
    } else if (name == "__debug_abbrev") {
        _debug_abbrev = section{offset, size};
    } else if (name == "__debug_line") {
        _debug_line = section{offset, size};
    }
}

/**************************************************************************************************/

void dwarf::implementation::read_abbreviations() {
    ZoneScoped;

    auto section_begin = _debug_abbrev._offset;
    auto section_end = section_begin + _debug_abbrev._size;
    temp_seek(_s, section_begin, [&] {
        while (_s.tellg() < section_end) {
            abbrev a;
            a.read(_s);
            if (a._code == 0 || a._tag == dw::tag::none) break;
            _abbreviations.push_back(std::move(a));
        }
    });
}

/**************************************************************************************************/

void dwarf::implementation::read_lines(std::size_t header_offset) {
    ZoneScoped;

    temp_seek(_s, _debug_line._offset + header_offset, [&] {
        line_header header;
        header.read(_s, _details._needs_byteswap);

        for (const auto& name : header._file_names) {
            if (name._directory_index > 0) {
                assert(name._directory_index - 1 < header._include_directories.size());
                std::string path(header._include_directories[name._directory_index - 1]);
                path += '/';
                path += name._name;
                _decl_files.push_back(empool(path));
            } else {
                _decl_files.push_back(empool(name._name));
            }
        }

        // We don't need to process the rest of this __debug__line subsection.
        // We're only here for the file table.
    });
}

/**************************************************************************************************/

const abbrev& dwarf::implementation::find_abbreviation(std::uint32_t code) const {
    auto found = std::lower_bound(_abbreviations.begin(), _abbreviations.end(), code,
                                  [](const auto& x, const auto& code) { return x._code < code; });
    if (found == _abbreviations.end() || found->_code != code) {
        throw std::runtime_error("abbrev not found: " + std::to_string(code));
    }
    return *found;
}

/**************************************************************************************************/

#define ORC_PRIVATE_FEATURE_DEBUG_STR_CACHE() (ORC_PRIVATE_FEATURE_TRACY() && 0)

pool_string dwarf::implementation::read_debug_str(std::size_t offset) {
    ZoneScoped;

#if ORC_FEATURE(DEBUG_STR_CACHE)
    thread_local float hit_s(0);
    thread_local float total_s(0);

    ++total_s;

    thread_local const char* plot_name_k = [] {
        const char* result =
            orc::tracy::format_unique("debug_str cache %s", orc::tracy::unique_thread_name());
        TracyPlotConfig(result, tracy::PlotFormatType::Percentage, true, true, 0);
        return result;
    }();

    const auto update_zone = [&] { tracy::Profiler::PlotData(plot_name_k, hit_s / total_s * 100); };
#endif // ORC_FEATURE(DEBUG_STR_CACHE)

    // I tried an implementation that loaded the whole debug_str section into the string pool on
    // the first debug string read. The big problem with that technique is that the single die
    // processing mode becomes very expensive, as it only needs a handful of debug strings but
    // ends up loading all of them. Perhaps we could pivot the technique based on the process mode?
    if (const auto found = _debug_str_cache.find(offset); found != _debug_str_cache.end()) {
#if ORC_FEATURE(DEBUG_STR_CACHE)
        ++hit_s;
        update_zone();
#endif // ORC_FEATURE(DEBUG_STR_CACHE)

        return found->second;
    }

#if ORC_FEATURE(DEBUG_STR_CACHE)
    update_zone();
#endif // ORC_FEATURE(DEBUG_STR_CACHE)

    return _debug_str_cache[offset] = temp_seek(_s, _debug_str._offset + offset,
                                                [&] { return empool(_s.read_c_string_view()); });
}

/**************************************************************************************************/

void dwarf::implementation::path_identifier_push() { _path.push_back(pool_string()); }

/**************************************************************************************************/

void dwarf::implementation::path_identifier_set(pool_string name) {
    assert(!_path.empty());
    _path.back() = name;
}

/**************************************************************************************************/

void dwarf::implementation::path_identifier_pop() { _path.pop_back(); }

/**************************************************************************************************/

std::string dwarf::implementation::qualified_symbol_name(
    const die& d, const attribute_sequence& attributes) const {
    ZoneScoped;

    // There are some attributes that contain the mangled name of the symbol.
    // This is a much better representation of the symbol than the derived path
    // we are using, so let's use that instead here.
    // (We may want to store the path off in the die elsewhere anyhow, as it
    // could aid in ODRV analysis by the user.)

    const dw::at qualified_attributes[] = {
        dw::at::linkage_name,
        dw::at::specification,
    };

    for (const auto& at : qualified_attributes) {
        if (attributes.has_string(at)) {
            return "::[u]::" + attributes.string(at).allocate_string();
        }
    }

    // preprocess the path. If any identifier in the path is the empty string,
    // then it's talking about an anonymous/unnamed symbol, which at this time
    // we do not register. In such a case, if we find one, return an empty string
    // for the whole path so we can skip over this die at registration time.
    for (const auto& identifier : _path) {
        if (identifier.empty()) {
            return std::string();
        }
    }

    std::string result;
    for (const auto& identifier : _path) {
        result += "::" + identifier.allocate_string();
    }
    return result;
}

/**************************************************************************************************/
// This "flattens" the template into an evaluated value, based on both the attribute and the
// current read position in debug_info.
attribute dwarf::implementation::process_attribute(const attribute& attr,
                                                   std::size_t cur_die_offset) {
    // clang-format off
    attribute result = attr;

    result._value = process_form(attr, cur_die_offset);

    if (result._value.has_passover()) {
        return result;
    }

    // some attributes need further processing. Once we have the initial value from process_form,
    // we can continue the work here. (Some forms, like ref_addr, have already been processed
    // completely- these are the cases when a value needs contextual interpretation. For example,
    // decl_file comes back as a uint, but that's a debug_str offset that needs to be resolved.
    if (result._name == dw::at::decl_file) {
        auto decl_file_index = result._value.uint();
        // We currently only process the `file_names` part of the `debug_line` section header to
        // determine the decl_files list. However, this is only a partial list as the line number
        // program can also contain DW_LNE_define_file ops, which we don't currently process.
        // See https://github.com/adobe/orc/issues/67
        // For now, we will ignore file indexes too large for our list.
        //assert(decl_file_index < _decl_files.size());
        if (decl_file_index < _decl_files.size()) {
            result._value.string(_decl_files[decl_file_index]);
        } else {
            result._value.string(empool("<unsupported file index>"));
        }
    } else if (result._name == dw::at::calling_convention) {
        auto convention = result._value.uint();
        assert(convention > 0 && convention <= 0xff);
        switch (convention) {
            case 0x01: result._value.string(empool("normal")); break;
            case 0x02: result._value.string(empool("program")); break;
            case 0x03: result._value.string(empool("nocall")); break;
            case 0x04: result._value.string(empool("pass by reference")); break;
            case 0x05: result._value.string(empool("pass by value")); break;
            case 0x40: result._value.string(empool("lo user")); break;
            case 0xff: result._value.string(empool("hi user")); break;
            // otherwise, leave the value unchanged.
        }
    } else if (result._name == dw::at::accessibility) {
        auto accessibility = result._value.uint();
        assert(accessibility >= 1 && accessibility <= 3);
        switch (accessibility) {
            case 1: result._value.string(empool("public")); break;
            case 2: result._value.string(empool("protected")); break;
            case 3: result._value.string(empool("private")); break;
            // otherwise, leave the value unchanged.
        }
    } else if (result._name == dw::at::virtuality) {
        auto virtuality = result._value.uint();
        assert(virtuality >= 0 && virtuality <= 2);
        switch (virtuality) {
            case 0: result._value.string(empool("none")); break;
            case 1: result._value.string(empool("virtual")); break;
            case 2: result._value.string(empool("pure virtual")); break;
            // otherwise, leave the value unchanged.
        }
    } else if (result._name == dw::at::visibility) {
        auto visibility = result._value.uint();
        assert(visibility > 0 && visibility <= 3);
        switch (visibility) {
            case 1: result._value.string(empool("local")); break;
            case 2: result._value.string(empool("exported")); break;
            case 3: result._value.string(empool("qualified")); break;
            // otherwise, leave the value unchanged.
        }
    } else if (result._name == dw::at::apple_property) {
        auto property = result._value.uint();
        // this looks like a bitfield; a switch may not suffice.
        switch (property) {
            case 0x01: result._value.string(empool("readonly")); break;
            case 0x02: result._value.string(empool("getter")); break;
            case 0x04: result._value.string(empool("assign")); break;
            case 0x08: result._value.string(empool("readwrite")); break;
            case 0x10: result._value.string(empool("retain")); break;
            case 0x20: result._value.string(empool("copy")); break;
            case 0x40: result._value.string(empool("nonatomic")); break;
            case 0x80: result._value.string(empool("setter")); break;
            case 0x100: result._value.string(empool("atomic")); break;
            case 0x200: result._value.string(empool("weak")); break;
            case 0x400: result._value.string(empool("strong")); break;
            case 0x800: result._value.string(empool("unsafe_unretained")); break;
            case 0x1000: result._value.string(empool("nullability")); break;
            case 0x2000: result._value.string(empool("null_resettable")); break;
            case 0x4000: result._value.string(empool("class")); break;
            // otherwise, leave the value unchanged.
        }
    } else if (result._form == dw::form::flag || result._form == dw::form::flag_present) {
        static const auto true_ = empool("true");
        static const auto false_ = empool("false");
        result._value.string(result._value.uint() ? true_ : false_);
    }

    return result;
    // clang-format on
}

/**************************************************************************************************/

pool_string dwarf::implementation::die_identifier(const die& d,
                                                  const attribute_sequence& attributes) const {
    // To get a good name for this die, first we look at the tag types
    // to see if there's something worth calling it. These tags may not
    // be the best high-level names to give to some of these types, but
    // I don't have all the attributes fully fleshed out yet. For example,
    // inlined_subroutine or subprogram might have an attribute with a
    // name that would be better than the tag name itself. When the
    // attributes are all fleshed out, we should revisit this one.
    switch (d._tag) {
        case dw::tag::compile_unit:
        case dw::tag::partial_unit:
            static pool_string unit_string_s = empool("[u]"); // u for "unit"
            return unit_string_s;
        default:
            // do nothing; we need the name not from the tag, but from one of its attributes
            break;
    }

    if (attributes.empty()) {
        return pool_string();
    }

    // If the tag type doesn't give us a good name, then we scan various
    // attributes.
    // clang-format off
    const dw::at string_attributes[] = {
        dw::at::linkage_name,
        dw::at::name,
        dw::at::type,
        dw::at::import_,
        dw::at::abstract_origin,
        dw::at::specification,
    };
    // clang-format on
    for (const auto& at : string_attributes)
        if (attributes.has_string(at)) return attributes.string(at);

    // If it's not named, it's an anonymous/unnamed die. So far we're skipping over these (and
    // all their descendants, if they have any) for registration purposes.
    return pool_string();
}

/**************************************************************************************************/

attribute_value dwarf::implementation::evaluate_exprloc(std::uint32_t expression_size) {
    std::vector<std::int64_t> stack;
    const auto end = _s.tellg() + expression_size;

    // There are some exprlocs that cannot be deciphered, probably because we don't have as much
    // information in our state machine as a debugger does when they're doing these evaluations. In
    // the event that happens, we set this passover flag, and avoid the evaluation entirely.
    bool passover = false;

    // The use of the range-based switch statements below are an extension to the C++ standard, but
    // is supported by both GCC and clang. It makes the below *far* more readable, but if we need
    // to pull it out, we can.

    auto stack_pop = [&_stack = stack] {
        assert(!_stack.empty());
        auto result = _stack.back();
        _stack.pop_back();
        return result;
    };

    auto stack_push = [&_stack = stack](auto value) { _stack.push_back(value); };

    // REVISIT (fosterbrereton) : The DWARF specification describes a multi-register stack machine
    // whose registers imitate (or are?!) the registers present on architecture for which this code
    // has been written. This implementation is anything but. Instead, it aims to be the minimum
    // implementation required to evaluate the DWARF attributes we care about.
    //
    // The DWARF spec mentions that each stack entry is a type/value tuple. As of now we store all
    // values as 64-bit unsigned integers, the rebels we are.
    //
    // Implementations marked OP_BROKEN are known to be incorrect, but whose implementations suffice
    // for the purposes of the tool. They could still be broken for unencountered cases. It's
    // likely there are other parts of this routine that are also broken, but are not known.
    //
    // When evaluating DW_AT_data_member_location for a DW_TAG_inheritance, we have to abide
    // by 5.7.3 of the spec, which says (in part):
    //     An inheritance entry for a class that derives from or extends another class or struct
    //     also has a DW_AT_data_member_location attribute, whose value describes the location of
    //     the beginning of the inherited type relative to the beginning address of the instance of
    //     the derived class. If that value is a constant, it is the offset in bytes from the
    //     beginning of the class to the beginning of the instance of the inherited type.
    //     Otherwise, the value must be a location description. In this latter case, the beginning
    //     address of the instance of the derived class is pushed on the expression stack before
    //     the location description is evaluated and the result of the evaluation is the location
    //     of the instance of the inherited type.
    // In other words, this is a _runtime derived_ value, where the object instance's address is
    // pushed onto the stack and then the machine is evaluated to derive the data member location
    // of the subclass. For our purposes, we can assume a base object address of 0.
    //
    // For a handful of expression location evaluations (like the one above), an initial value is
    // assumed to be on the stack. It's probably easiest to just push a default value of 0x10000
    // onto the stack to start with for all cases. (This isn't 0 because in some cases it's an
    // address and may be negatively offset, so we want a reasonable value that won't underflow).
    // Since the topmost item on the stack is the expression's value, an extra value at the bottom
    // shouldn't be fatal. That's what we'll do for now, and if a case pops where that's a problem,
    // we'll revisit.
    //
    // Aaaand we've hit a problem. I still think the above scenario works, but I am getting
    // some runtime errors. I have found cases where two libraries that appear to be compiled
    // with identical settings produce differing DWARF entries for DW_AT_data_member_location,
    // causing the values to differ when the base address is not 0. Specifically, the two
    // expressions I'm seeing are:
    //     DW_AT_data_member_location    (0x00)
    // and
    //     DW_AT_data_member_location    (DW_OP_plus_uconst 0x0)
    // which we know should be the same symbol as observed in different object files. I'm not
    // sure why one object file chooses an address-relative expression, while the other opts
    // for the absolute value. I _think_ in the absolute case, I need to add that value to
    // the base object's offset, which in our simulated case would be 0x10000. (See details
    // about DW_AT_data_member_location in Section 5.7.6 (Data Member Entries)).
    //
    // TL;DR: This is set to zero for now and we can address (ha!) it later if required.
    stack_push(0); // Ideally should be a nonzero value to better emulate addresses.

    // Useful to see what the whole expression is that this routine is about to evaluate.
#if ORC_FEATURE(DEBUG) && 0
    std::vector<char> expression(expression_size, 0);
    temp_seek(_s, [&] { _s.read(&expression[0], expression_size); });
#endif // ORC_FEATURE(DEBUG)

    // clang-format off
    while (_s.tellg() < end && !passover) {
        // The switch statements are ordered according to their enumeration in the DWARF 5
        // specification (starting in section 2.5).
        switch (auto op = read_pod<dw::op>(_s); op) {
            //
            // 2.5.1.1 Literal Encodings
            //
            case dw::op::lit0 ... dw::op::lit31: { // gcc/clang extension
                // These opcodes encode the unsigned literal values from 0 through 31, inclusive.
                stack_push(static_cast<int>(op) - static_cast<int>(dw::op::lit0));
            } break;
            case dw::op::addr: {
                // A single operand that encodes a machine address and whose size is the size of an
                // address on the target machine.
                //
                // REVISIT (fosterbrereton) : I think this "is 64 bit" question should be answered
                // by the compilation unit header (`cu_header`), not the file details. Or, better
                // yet, I think is the address size in that same header.
                if (_details._is_64_bit) {
                    stack_push(read64());
                } else {
                    stack_push(read32()); // safe to assume?
                }
            } break;
            case dw::op::const1u: {
                // The single operand provides a 1-byte unsigned integer constant
                stack_push(read8());
            } break;
            case dw::op::const2u: {
                // The single operand provides a 2-byte unsigned integer constant
                stack_push(read16());
            } break;
            case dw::op::const4u: {
                // The single operand provides a 4-byte unsigned integer constant
                stack_push(read32());
            } break;
            case dw::op::const8u: {
                // The single operand provides an 8-byte unsigned integer constant
                stack_push(read64());
            } break;
            case dw::op::const1s: {
                // The single operand provides a 1-byte signed integer constant
                stack_push(read_pod<std::int8_t>(_s));
            } break;
            case dw::op::const2s: {
                // The single operand provides a 2-byte signed integer constant
                stack_push(read_pod<std::int16_t>(_s));
            } break;
            case dw::op::const4s: {
                // The single operand provides a 4-byte signed integer constant
                stack_push(read_pod<std::int32_t>(_s));
            } break;
            case dw::op::const8s: {
                // The single operand provides an 8-byte signed integer constant
                stack_push(read_pod<std::int64_t>(_s));
            } break;
            case dw::op::constu: {
                // The single operand provides an unsigned LEB128 integer constant
                stack_push(read_uleb());
            } break;
            case dw::op::consts: {
                // The single operand provides a signed LEB128 integer constant
                stack_push(read_sleb());
            } break;

            //
            // 2.5.1.2 Register Values
            //
            case dw::op::fbreg: {
                // OP_BROKEN
                // Provides a signed LEB128 offset from the address specified by the location
                // description in the DW_AT_frame_base attribute of the current function.
                stack_push(read_sleb());
            }   break;
            case dw::op::breg0 ... dw::op::breg31: { // gcc/clang extension
                // OP_BROKEN
                // The single operand provides a signed LEB128 offset from the specified register.
                stack_push(read_sleb());
            } break;

            //
            // 2.5.1.3 Stack Operations
            //
            case dw::op::dup: {
                // Duplicates the value (including its type identifier) at the top of the stack
                assert(!stack.empty());
                stack_push(stack.back());
            } break;
            case dw::op::drop: {
                // Pops the value (including its type identifier) at the top of the stack
                assert(!stack.empty());
                (void)stack_pop();
            } break;
            case dw::op::deref: {
                // Pops the top stack entry and treats it as an address. The popped value must
                // have an integral type. The value retrieved from that address is pushed, and
                // has the generic type. The size of the data retrieved from the dereferenced
                // address is the size of an address on the target machine.
                //
                // The net effect of this operation is a pop, dereference, and push. In our
                // case, then, we'll do nothing.
            } break;

            //
            // 2.5.1.4 Arithmetic and Logical Operations
            //
            case dw::op::and_: {
                // Pops the top two stack values, performs a bitwise and operation on the two, and
                // pushes the result.
                assert(stack.size() > 1);
                auto arg0 = stack_pop();
                auto arg1 = stack_pop();
                stack_push(arg0 & arg1);
            } break;
            case dw::op::plus_uconst: {
                // Pops the top stack entry, adds it to the unsigned LEB128 constant operand and
                // pushes the result.
                assert(!stack.empty());
                stack_push(read_uleb() + stack_pop());
            } break;
            case dw::op::minus: {
                // Pops the top two stack values, subtracts the former top of the stack from the
                // former second entry, and pushes the result.
                auto arg0 = stack_pop();
                auto arg1 = stack_pop();
                stack_push(arg1 - arg0);
            } break;
            case dw::op::plus: {
                // Pops the top two stack entries, adds them together, and pushes the result.
                stack_push(stack_pop() + stack_pop());
            } break;

            //
            // This is the end of Section 2.5. What follows in Section 2.6 are descriptions
            // of "objects" in the machine. Unfortunately the spec isn't entirely clear what to do
            // with these objects once they've been located, so we push something to the stack.
            //

            //
            // 2.6.1.1.3 Register Location Descriptions
            //
            case dw::op::reg0 ... dw::op::reg31: { // gcc/clang extension
                // These opcodes encode the names of up to 32 registers, numbered from 0 through 31,
                // inclusive. The object addressed is in register n.
                stack_push(0);
            } break;
            case dw::op::regx: {
                // A single unsigned LEB128 literal operand that encodes the name of a register.
                stack_push(read_uleb()); } break;

            //
            // 2.6.1.1.4 Implicit Location Descriptions
            //
            case dw::op::stack_value: {
                // The DWARF expression represents the actual value of the object, rather than its
                // location. The DW_OP_stack_value operation terminates the expression. This is
                // the "return" operator of the expression system. We assume it is at the end of
                // the evaluation stream and so do nothing. This may need to be revisited if the
                // assumption is bad.
            } break;

            //
            // As soon as we find an opcode we don't interpret, we pass over the entire expression
            // and mark the result as unhandled.
            //

            default: {
                passover = true;
            } break;
        }
    }
    // clang-format on

    attribute_value result;

    if (passover) {
        _s.seekg(end);
        result.passover();
    } else {
        assert(!stack.empty());
        result.sint(static_cast<std::int32_t>(stack.back()));
    }

    return result;
}

/**************************************************************************************************/

attribute_value dwarf::implementation::process_form(const attribute& attr,
                                                    std::size_t cur_die_offset) {
    ZoneScoped;

    /*
        The values for `ref1`, `ref2`, `ref4`, and `ref8` are offsets from the first byte of
        the current compilation unit header, not the top of __debug_info.

        `ref_addr` could be 4 (DWARF) or 8 (DWARF64) bytes. We assume the former at present.
        We should save the cu_header somewhere so we can do the right thing here.

        Section 7.5.5 of the spec says very little about the data contained within `block` types:

            In all [block] forms, the length is the number of information bytes that follow. The
            information bytes may contain any mixture of relocated (or relocatable)
            addresses, references to other debugging information entries or data bytes.

        Given the ambiguity of the form, I am not convinced the associated attribute will be a
        necessary one for computing an ODRV. The previous rendition of the switch statement below
        treated them as an `exprloc`, which is _definitely_ not right (the spec doesn't say
        anything about that being the case) so we'll treat them as a passover value and emit
        a warning.
     */

    attribute_value result;

    const auto handle_reference = [&](std::uint64_t offset) {
        const auto debug_info_offset = _debug_info._offset;
        const auto cu_offset = _cu_address - debug_info_offset;
        // REVISIT (fosterbrereton): Possible overflow
        result.reference(static_cast<std::uint32_t>(cu_offset + offset));
    };

    const auto handle_passover = [&]() {
        // We have a problem if we are passing over an attribute that is needed to determine
        // ODRVs.
        assert(nonfatal_attribute(attr._name));
        result.passover();
        auto size = form_length(attr._form, _s);
        _s.seekg(size, std::ios::cur);
    };

    enum class block_type {
        one,
        two,
        four,
        uleb,
    };

    // Where the handling of an essential block takes place. We get a size amount from
    // `maybe_handle_block` telling us how many bytes are in this block that we need to process. We
    // read them one at a time, accumulating them in an unsigned 64-bit value. This assumes the
    // value is both an integer, and will fit in 64 bits. If either of this is found to be false,
    // we'll need to revisit this.
    const auto handle_block = [&](std::size_t size) {
        if (size > 8) {
            throw std::runtime_error("Unexpected block size read of essential data.");
        }

        std::uint64_t value(0);
        while (size--) {
            value <<= 8;
            value |= read8();
        }

        result.uint(value);
    };

    // The first level of `blockN` handling - if the attribute is nonessential, we pass over it like
    // we were doing before. If it is essential, depending on the `blockN` form, we read some
    // number of bytes to discover how much data this block holds. We then forward that size on to
    // `handle_block`, above.
    const auto maybe_handle_block = [&](block_type type) {
        if (nonfatal_attribute(attr._name)) {
            handle_passover();
        } else {
            switch (type) {
                case block_type::one: {
                    handle_block(read8());
                } break;
                case block_type::two: {
                    handle_block(read16());
                } break;
                case block_type::four: {
                    handle_block(read32());
                } break;
                case block_type::uleb: {
                    handle_block(read_uleb());
                } break;
            }
        }
    };

    switch (attr._form) {
        case dw::form::udata:
        case dw::form::implicit_const: {
            result.uint(read_uleb());
        } break;
        case dw::form::sdata: {
            result.uint(read_uleb()); // sdata is expecting unsigned values?
        } break;
        case dw::form::strp: {
            result.string(read_debug_str(read32()));
        } break;
        case dw::form::exprloc: {
            read_exactly(_s, read_uleb(), [&](auto expr_size) {
                result = evaluate_exprloc(static_cast<std::uint32_t>(expr_size));
            });
        } break;
        case dw::form::addr: {
            result.uint(read64());
        } break;
        case dw::form::ref_addr: {
            if (_cu_header._version == 2) {
                result.reference(read64());
            } else {
                result.reference(read32());
            }
        } break;
        case dw::form::ref1: {
            handle_reference(read8());
        } break;
        case dw::form::ref2: {
            handle_reference(read16());
        } break;
        case dw::form::ref4: {
            handle_reference(read32());
        } break;
        case dw::form::ref8: {
            handle_reference(read64());
        } break;
        case dw::form::data1: {
            result.uint(read8());
        } break;
        case dw::form::data2: {
            result.uint(read16());
        } break;
        case dw::form::data4: {
            result.uint(read32());
        } break;
        case dw::form::data8: {
            result.uint(read64());
        } break;
        case dw::form::string: {
            result.string(empool(_s.read_c_string_view()));
        } break;
        case dw::form::flag: {
            result.uint(read8());
        } break;
        case dw::form::flag_present: {
            result.uint(1);
        } break;
        case dw::form::sec_offset: {
            result.uint(read32());
        } break;
        case dw::form::block1: {
            maybe_handle_block(block_type::one);
        } break;
        case dw::form::block2: {
            maybe_handle_block(block_type::two);
        } break;
        case dw::form::block4: {
            maybe_handle_block(block_type::four);
        } break;
        case dw::form::block: {
            maybe_handle_block(block_type::uleb);
        } break;
        default: {
            handle_passover();
        } break;
    }

    return result;
}

/**************************************************************************************************/

die_pair dwarf::implementation::offset_to_die_pair(std::size_t offset) {
    return temp_seek(_s, offset + _debug_info._offset,
                     [&]() { return abbreviation_to_die(_s.tellg(), process_mode::single); });
}

/**************************************************************************************************/

attribute_sequence dwarf::implementation::offset_to_attribute_sequence(std::size_t offset) {
    return std::get<1>(offset_to_die_pair(offset));
}

/**************************************************************************************************/

pool_string dwarf::implementation::resolve_type(attribute type) {
    ZoneScoped;

    std::size_t reference = type.reference();
    auto found = _type_cache.find(reference);
    if (found != _type_cache.end()) {
        // std::cout << "memoized " << hex_print(reference) << ": " << found->second << '\n';
        return found->second;
    }

    auto recurse = [&](auto& attributes) {
        if (!attributes.has(dw::at::type)) return pool_string();
        return resolve_type(attributes.get(dw::at::type));
    };

    pool_string result;
    die die;
    attribute_sequence attributes;
    std::tie(die, attributes) = offset_to_die_pair(reference);

    if (die._tag == dw::tag::const_type) {
        result = empool("const " + recurse(attributes).allocate_string());
    } else if (die._tag == dw::tag::pointer_type) {
        result = empool(recurse(attributes).allocate_string() + "*");
    } else if (attributes.has_string(dw::at::type)) {
        result = type.string();
    } else if (attributes.has_reference(dw::at::type)) {
        result = recurse(attributes);
    } else if (attributes.has_string(dw::at::name)) {
        result = attributes.string(dw::at::name);
    }

    // std::cout << "cached " << hex_print(reference) << ": " << result << '\n';
    return _type_cache[reference] = result;
}

/**************************************************************************************************/

die_pair dwarf::implementation::abbreviation_to_die(std::size_t die_address, process_mode mode) {
    ZoneScoped;

    die die;
    attribute_sequence attributes;

    die._debug_info_offset = static_cast<std::uint32_t>(die_address - _debug_info._offset);
    die._arch = _details._arch;

    std::size_t abbrev_code = read_uleb();

    if (abbrev_code == 0) return std::make_tuple(std::move(die), std::move(attributes));

    auto& a = find_abbreviation(static_cast<std::uint32_t>(abbrev_code));

    die._tag = a._tag;
    die._has_children = a._has_children;

    attributes.reserve(a._attributes.size());

    std::transform(a._attributes.begin(), a._attributes.end(), std::back_inserter(attributes),
                   [&](const auto& x) {
                       // If the attribute is nonfatal, we'll pass over it in `process_attribute`.
                       return process_attribute(x, die._debug_info_offset);
                   });

    if (mode == process_mode::complete) {
        path_identifier_set(die_identifier(die, attributes));

        die._path = empool(std::string_view(qualified_symbol_name(die, attributes)));
    }

    return std::make_tuple(std::move(die), std::move(attributes));
}

/**************************************************************************************************/

bool dwarf::implementation::register_sections_done() {
    assert(!_ready);

    // Houston, we have a problem.
    if (!(_debug_info.valid() && _debug_abbrev.valid() && _debug_line.valid())) return false;

    // the declaration files are 1-indexed. The 0th index is reserved for the compilation unit /
    // partial unit name. We need to prime this here because in single process mode we don't get
    // the name of the compilation unit unless we explicitly ask for it.
    //
    // DWARF Spec 6.2.4 talking about `directories` (sequence of directory names):
    //     The first entry in the sequence is the primary source file whose file name exactly
    //     matches that given in the DW_AT_name attribute in the compilation unit debugging
    //     information entry.
    _decl_files.push_back(object_file_ancestry(_ofd_index)._ancestors[0]);

    // Once we've loaded all the necessary DWARF sections, now we start piecing the details
    // together.

    read_abbreviations();

    _ready = true;

    return true;
}

/**************************************************************************************************/

bool dwarf::implementation::skip_die(die& d, const attribute_sequence& attributes) {
    ZoneScoped;
    ZoneColor(tracy::Color::ColorType::Red);

    // These are a handful of "filters" we use to elide false positives.

    // These are the tags we don't deal with (yet, if ever.)
    if (skip_tagged_die(d)) {
        ZoneTextL("skipping: tagged die");
        return true;
    }

    // According to DWARF 3.3.1, a subprogram tag that is missing the external
    // flag means the function is invisible outside its compilation unit. As
    // such, it cannot contribute to an ODRV.
    if (d._tag == dw::tag::subprogram && !has_flag_attribute(attributes, dw::at::external)) {
        ZoneTextL("skipping: non-external subprogram");
        return true;
    }

    // Empty path means the die (or an ancestor) is anonymous/unnamed. No need to register
    // them.
    if (d._path.empty()) {
        ZoneTextL("skipping: empty path");
        return true;
    }

    // Symbols with __ in them are reserved, so are not user-defined. No need to register
    // them.
    if (d._path.view().find("::__") != std::string::npos) {
        ZoneTextL("skipping: non-user-defined (reserved) symbol");
        return true;
    }

    // lambdas are ephemeral and can't cause (hopefully) an ODRV
    if (d._path.view().find("lambda") != std::string::npos) {
        ZoneTextL("skipping: lambda");
        return true;
    }

    // we don't handle any die that's ObjC-based.
    if (attributes.has(dw::at::apple_runtime_class)) {
        ZoneTextL("skipping: apple runtime class");
        return true;
    }

    // If the symbol is listed in the symbol_ignore list, we're done here.
    std::string_view symbol = d._path.view();
    if (symbol.size() > 7 && sorted_has(settings::instance()._symbol_ignore, symbol.substr(7))) {
        ZoneTextL("skipping: on symbol_ignore list");
        return true;
    }

    // Unfortunately we have to do this work to see if we're dealing with
    // a self-referential type.
    if (attributes.has_reference(dw::at::type)) {
        // if this is a self-referential type, it's die will have no attributes.
        // To determine that, we'll jump to the reference, grab the abbreviation code,
        // and see how many attributes it should have.
        auto reference = attributes.reference(dw::at::type);
        bool empty = temp_seek(_s, _debug_info._offset + reference, std::ios::beg, [&] {
            auto abbrev_code = read_uleb();
            const auto& abbrev = find_abbreviation(abbrev_code);
            return abbrev._attributes.empty();
        });

        if (empty) {
            ZoneTextL("skipping: self-referential type");
            return true;
        }
    }

    // This makes the kept dies visually identifiable in the Tracy analyzer.
    TracyMessageL("odr_used");
    ZoneColor(tracy::Color::ColorType::Green);
    return false;
}

/**************************************************************************************************/

void dwarf::implementation::process_all_dies() {
    if (!_ready && !register_sections_done()) return;
    assert(_ready);

    auto section_begin = _debug_info._offset;
    auto section_end = section_begin + _debug_info._size;

    _s.seekg(section_begin);

    // Have a nonempty stack in the path
    path_identifier_push();

    dies dies;

    while (_s.tellg() < section_end) {
        _cu_address = _s.tellg();

        _cu_header.read(_s, _details._needs_byteswap);

        // process dies one at a time, recording things like addresses along the way.
        while (true) {
            ZoneScopedN("process_one_die"); // name matters for stats tracking

            die die;
            attribute_sequence attributes;
            std::tie(die, attributes) = abbreviation_to_die(_s.tellg(), process_mode::complete);

#if ORC_FEATURE(TRACY)
            const char* tag_str = to_string(die._tag);
            ZoneNameL(tag_str);
#endif // ORC_FEATURE(TRACY)

            // Useful for looking up symbols in dwarfdump output.
#if ORC_FEATURE(DEBUG) && 0
            std::cerr << std::hex << "0x" << (die._debug_info_offset) << std::dec << ": "
                      << to_string(die._tag) << '\n';
#endif // ORC_FEATURE(DEBUG) && 0

            // code 0 is reserved; it's a null entry, and signifies the end of siblings.
            if (die._tag == dw::tag::none) {
                path_identifier_pop();

                if (_path.size() == 1) {
                    break; // end of the compilation unit
                }

                continue;
            } else if (die._tag == dw::tag::compile_unit || die._tag == dw::tag::partial_unit) {
                // Spec (section 3.1.1) says that compilation and partial units may specify which
                // __debug_line subsection they want to draw their decl_files list from. This also
                // means we need to clear our current decl_files list (from index 1 to the end)
                // whenever we do hit either of these two dies. (What's the right action to take
                // when a unit doesn't have a stmt_list attribute? Where do we get our file names
                // from? Or is the expectation that the DWARF information won't specify any in that
                // case?)

                assert(!_decl_files.empty());
                _decl_files.erase(std::next(_decl_files.begin()), _decl_files.end());

                if (attributes.has_uint(dw::at::stmt_list)) {
                    read_lines(attributes.uint(dw::at::stmt_list));
                }

                // REVISIT (fosterbrereton): If the name is a relative path, there may be a
                // DW_AT_comp_dir attribute that specifies the path it is relative from.
                // Is it worth making this path absolute?

                _decl_files[0] = attributes.string(dw::at::name);

                // We've seen cases in the wild where compilation units are empty, have no children,
                // but do not have a null abbreviation code signalling their "end". In this case,
                // then, if we find a unit with no children, we're done. (No need to pop from the
                // path, either, because we never pushed anything.)

                // REVISIT: (fosterbrereton) This code path does not register the unit's die.
                if (!die._has_children) {
                    break; // end of the compilation unit
                }
            }

            if (die._has_children) {
                path_identifier_push();
            }

            if (attributes.has(dw::at::type)) {
                attributes.get(dw::at::type)
                    ._value.string(resolve_type(attributes.get(dw::at::type)));
            }

            die._skippable = skip_die(die, attributes);
            die._ofd_index = _ofd_index;
            die._hash = die_hash(die, attributes);
            die._fatal_attribute_hash = fatal_attribute_hash(attributes);

#if ORC_FEATURE(TRACY)
            auto path_view = die._path.view();
            if (!path_view.empty()) {
                ZoneText(path_view.data(), path_view.length());
            } else {
                ZoneTextL("<empty path>");
            }
            constexpr auto msg_sz_k = 32;
            char msg[msg_sz_k] = {0};
            std::snprintf(msg, msg_sz_k, "offset 0x%dh", die._debug_info_offset);
            ZoneTextL(msg);
            std::snprintf(msg, msg_sz_k, "%zu attribute(s)", attributes.size());
            ZoneTextL(msg);
#endif // ORC_FEATURE(TRACY)

            dies.emplace_back(std::move(die));
        }
    }

    dies.shrink_to_fit();

    _register_dies(std::move(dies));
}

/**************************************************************************************************/

die_pair dwarf::implementation::fetch_one_die(std::size_t debug_info_offset) {
    ZoneScoped;

    if (!_ready && !register_sections_done()) throw std::runtime_error("dwarf setup failed");

    // This is a hack for https://github.com/adobe/orc/issues/72. The problem is the file
    // declaration list is contained in a specific `debug_lines` entry, which is driven by the
    // `stmt_list` attribute in the `compilation_unit` die that contains this die we are trying to
    // fetch. (We have observed there can be more than one compilation unit declaration, and thus
    // more than one `debug_lines` entry, per `debug_info`). However, the way ORC tracks die
    // information today, we do not cross reference from the compilation unit die to this one, or
    // give each die its own `debug_lines` offset (which would be the proper solution). The reasons
    // I am avoiding the latter are 1) it adds an extra 32 bits per die, and 2) in all observed
    // instances the `debug_lines` offset for user-defined symbols is 0. So, the hack here to save
    // 32 bits per die is to assume a `debug_lines` offset of 0, read the file list from the
    // `debug_lines` header, and assume it is the right one. If/when a real-world instance is found
    // that breaks this assumption, we can fall back on the more memory-expensive option.
    read_lines(0);

    auto die_address = _debug_info._offset + debug_info_offset;
    _s.seekg(die_address);
    _cu_address = _debug_info._offset; // not sure if this is correct in all cases
    auto result = abbreviation_to_die(die_address, process_mode::single);
    auto& attributes = std::get<1>(result);
    if (attributes.has(dw::at::type)) {
        attributes.get(dw::at::type)._value.string(resolve_type(attributes.get(dw::at::type)));
    }
    return result;
}

/**************************************************************************************************/

dwarf::dwarf(std::uint32_t ofd_index,
             freader&& s,
             file_details&& details,
             register_dies_callback&& callback)
    : _impl(new implementation(ofd_index, std::move(s), std::move(details), std::move(callback)),
            [](auto x) { delete x; }) {}

void dwarf::register_section(std::string name, std::size_t offset, std::size_t size) {
    _impl->register_section(std::move(name), offset, size);
}

void dwarf::process_all_dies() { _impl->process_all_dies(); }

die_pair dwarf::fetch_one_die(std::size_t debug_info_offset) {
    return _impl->fetch_one_die(debug_info_offset);
}

/**************************************************************************************************/
