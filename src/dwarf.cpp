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

std::size_t die_hash(const die& d, const attribute_sequence& attributes) {
    bool is_declaration =
        attributes.has_uint(dw::at::declaration) && attributes.uint(dw::at::declaration) == 1;
    return orc::hash_combine(0,
                             static_cast<std::size_t>(d._arch),
                             static_cast<std::size_t>(d._tag),
                             d._path.hash(),
                             is_declaration);
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
    _length = read_pod<std::uint32_t>(s);
    if (needs_byteswap) {
        endian_swap(_length);
    }
    if (_length >= 0xfffffff0) {
        // REVISIT: (fbrereto) handle extended length / DWARF64
        throw std::runtime_error("unsupported length");
    }

    _version = read_pod<std::uint16_t>(s);

    // note the read_pod types differ.
    if (_is_64_bit) {
        _debug_abbrev_offset = read_pod<std::uint64_t>(s);
    } else {
        _debug_abbrev_offset = read_pod<std::uint32_t>(s);
    }

    if (needs_byteswap) {
        endian_swap(_version);
        endian_swap(_debug_abbrev_offset);
    }

    _address_size = read_pod<std::uint8_t>(s);
}

/**************************************************************************************************/

struct line_header {
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
    _length = read_pod<std::uint32_t>(s);
    if (needs_byteswap) {
        endian_swap(_length);
    }
    if (_length >= 0xfffffff0) {
        // REVISIT: (fbrereto) handle extended length / DWARF64
        throw std::runtime_error("unsupported length");
    }
    _version = read_pod<std::uint16_t>(s);
    _header_length = read_pod<std::uint32_t>(s);
    _min_instruction_length = read_pod<std::uint8_t>(s);
    if (_version >= 4) {
        _max_ops_per_instruction = read_pod<std::uint8_t>(s);
    }
    _default_is_statement = read_pod<std::uint8_t>(s);
    _line_base = read_pod<std::int8_t>(s);
    _line_range = read_pod<std::uint8_t>(s);
    _opcode_base = read_pod<std::uint8_t>(s);
    if (needs_byteswap) {
        endian_swap(_version);
        endian_swap(_header_length);
        // endian_swap(_min_instruction_length);
        // endian_swap(_max_ops_per_instruction);
        // endian_swap(_default_is_statement);
        // endian_swap(_line_base);
        // endian_swap(_line_range);
        // endian_swap(_opcode_base);
    }
    for (std::size_t i{0}; i < (_opcode_base - 1); ++i) {
        _standard_opcode_lengths.push_back(read_pod<std::int8_t>(s));
    }

    while (true) {
        auto cur_directory = s.read_c_string_view();
        if (cur_directory.empty()) break;
        _include_directories.push_back(cur_directory);
    }

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
    // We only hash the attributes that could contribute to an ODRV. We also sort that set of
    // attributes by name to make sure the hashing is consistent regardless of attribute order.
    std::vector<dw::at> names;
    for (const auto& attr : attributes) {
        if (nonfatal_attribute(attr._name)) continue;
        names.push_back(attr._name);
    }
    std::sort(names.begin(), names.end());

    std::size_t h{0};
    for (const auto& name : names) {
        h = orc::hash_combine(h, attributes.hash(name));
    }
    return h;
}

/**************************************************************************************************/

bool skip_tagged_die(const die& d) {
    static const dw::tag skip_tags[] = {
        dw::tag::compile_unit,
        dw::tag::partial_unit,
        dw::tag::variable,
        dw::tag::formal_parameter,
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
    void read_lines();
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
    T result = read_pod<T>(_s);
    if (_details._needs_byteswap) endian_swap(result);
    return result;
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

void dwarf::implementation::read_lines() {
    temp_seek(_s, _debug_line._offset, [&] {
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

        // We don't need to process the rest of __debug__line. We're only here for the file table.
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

pool_string dwarf::implementation::read_debug_str(std::size_t offset) {
    // I tried an implementation that loaded the whole debug_str section into the string pool on
    // the first debug string read. The big problem with that technique is that the single die
    // processing mode becomes very expensive, as it only needs a handful of debug strings but
    // ends up loading all of them. Perhaps we could pivot the technique based on the process mode?
    auto found = _debug_str_cache.find(offset);
    if (found != _debug_str_cache.end()) {
        return found->second;
    }

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
        assert(decl_file_index < _decl_files.size());
        result._value.string(_decl_files[decl_file_index]);
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
    const dw::at string_attributes[] = {
        dw::at::linkage_name,    dw::at::name,          dw::at::type, dw::at::import,
        dw::at::abstract_origin, dw::at::specification,
    };
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
    // information in our state machine as a debugger does when they're doing these evaluations.
    // In the event that happens, we set this passover flag, and avoid the evaluation entirely.
    bool passover = false;

    // The use of the range-based switch statements below are an extension to the C++ standard,
    // but is supported by both GCC and clang. It makes the below *far* more readable, but if
    // we need to pull it out, we can.

    auto stack_pop = [&_stack = stack]{
        assert(!_stack.empty());
        auto result = _stack.back();
        _stack.pop_back();
        return result;
    };

    auto stack_push = [&_stack = stack](auto value){
        _stack.push_back(value);
    };

    // REVISIT (fosterbrereton) : The DWARF specification describes a
    // multi-register stack machine. This implementation is anything but.
    // Instead, it aims to be the minimum amount of work required to
    // evaluate the DWARF attributes we care about. To date, this has not
    // required that we fully implement the stack machine.

    // clang-format off
    while (_s.tellg() < end && !passover) {
        auto op = read_pod<dw::op>(_s);
        switch (op) {
            case dw::op::lit0 ... dw::op::lit31: { // gcc/clang extension
                stack_push(static_cast<int>(op) - static_cast<int>(dw::op::reg0));
            } break;
            case dw::op::reg0 ... dw::op::reg31: { // gcc/clang extension
                stack_push(0);
            } break;
            case dw::op::breg0 ... dw::op::breg31: { // gcc/clang extension
                // The single operand of the DW_OP_bregn operations provides a signed LEB128
                // offset from the specified register.
                stack_push(read_sleb());
            } break;
            case dw::op::fbreg: {
                // Provides a signed LEB128 offset from the address specified by the
                // location description in the DW_AT_frame_base attribute of the current
                // function.
                stack_push(read_sleb());
            }   break;
            case dw::op::addr: {
                // a single operand that encodes a machine address and whose size is the size of
                // an address on the target machine.
                if (_details._is_64_bit) {
                    stack_push(read64());
                } else {
                    stack_push(read32()); // safe to assume?
                }
            } break;
            case dw::op::plus_uconst: {
                // Pops the top stack entry, adds it to the unsigned LEB128 constant operand and
                // pushes the result. I have seen cases where the stack is empty, so we do a check
                // first.
                auto operand = read_uleb();
                if (!stack.empty()) {
                    operand += stack_pop();
                }
                stack_push(operand);
            } break;
            case dw::op::and_: {
                // Pops the top two stack values, performs a bitwise and operation on the two,
                // and pushes the result.
                assert(stack.size() > 1);
                auto arg0 = stack_pop();
                auto arg1 = stack_pop();
                stack_push(arg0 | arg1);
            } break;
            case dw::op::stack_value: {
                // The DWARF expression represents the actual value of the object, rather than
                // its location. The DW_OP_stack_value operation terminates the expression.
                // This is the "return" operator of the expression system. We assume it is at
                // the end of the evaluation stream and so do nothing. This may need to be
                // revisited if the assumption is bad.
            } break;
            case dw::op::const1u: {
                stack_push(read8());
            } break;
            case dw::op::const2u: {
                stack_push(read16());
            } break;
            case dw::op::const4u: {
                stack_push(read32());
            } break;
            case dw::op::const8u: {
                stack_push(read64());
            } break;
            case dw::op::const1s: {
                stack_push(read_pod<std::int8_t>(_s));
            } break;
            case dw::op::const2s: {
                stack_push(read_pod<std::int16_t>(_s));
            } break;
            case dw::op::const4s: {
                stack_push(read_pod<std::int32_t>(_s));
            } break;
            case dw::op::const8s: {
                stack_push(read_pod<std::int64_t>(_s));
            } break;
            case dw::op::constu: {
                stack_push(read_uleb());
            } break;
            case dw::op::consts: {
                stack_push(read_sleb());
            } break;
            case dw::op::regx: {
                stack_push(read_uleb());
            } break;
            case dw::op::dup: {
                if (stack.empty()) {
                    passover = true;
                } else {
                    stack_push(stack.back());
                }
            } break;
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
    auto form = attr._form;
    const auto debug_info_offset = _debug_info._offset;
    const auto cu_offset = _cu_address - debug_info_offset;
    attribute_value result;

    auto set_passover_result = [&]{
        result.passover();
        auto size = form_length(form, _s);
        _s.seekg(size, std::ios::cur);
    };

    auto evaluate_expression = [&, _impl = this](auto length_fn) {
        // If the attribute is nonfatal, don't waste time evaluating it.
        if (nonfatal_attribute(attr._name)) {
            set_passover_result();
            return;
        }
        auto length = (_impl->*length_fn)();
        read_exactly(_s, length,
                     [&](auto length) { result = evaluate_exprloc(length); });
    };

    /*
        Notes worth remembering:

        The values for ref1, ref2, ref4, and ref8 are offsets from the first byte of the current
        compilation unit header, not the top of __debug_info.

        ref_addr could be 4 (DWARF) or 8 (DWARF64) bytes. We assume the former at present. We
        should save the cu_header somewhere so we can do the right thing here.
     */

    switch (form) {
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
            read_exactly(_s, read_uleb(),
                         [&](auto expr_size) { result = evaluate_exprloc(static_cast<std::uint32_t>(expr_size)); });
        } break;
        case dw::form::addr: {
            result.uint(read64());
        } break;
        case dw::form::ref_addr: {
            result.reference(read32());
        } break;
        case dw::form::ref1: {
            result.reference(static_cast<std::uint32_t>(cu_offset + read8()));
        } break;
        case dw::form::ref2: {
            result.reference(static_cast<std::uint32_t>(cu_offset + read16()));
        } break;
        case dw::form::ref4: {
            result.reference(static_cast<std::uint32_t>(cu_offset + read32()));
        } break;
        case dw::form::ref8: {
            result.reference(static_cast<std::uint32_t>(cu_offset + read64()));
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
            evaluate_expression(&dwarf::implementation::read8);
        } break;
        case dw::form::block2: {
            evaluate_expression(&dwarf::implementation::read16);
        } break;
        case dw::form::block4: {
            evaluate_expression(&dwarf::implementation::read32);
        } break;
        case dw::form::block: {
            evaluate_expression(&dwarf::implementation::read_uleb);
        } break;
        default: {
            set_passover_result();
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
    _decl_files.push_back(object_file_ancestry(_ofd_index)._ancestors[0]);

    // Once we've loaded all the necessary DWARF sections, now we start piecing the details
    // together.

    read_abbreviations();

    read_lines();

    _ready = true;

    return true;
}

/**************************************************************************************************/

bool dwarf::implementation::skip_die(die& d, const attribute_sequence& attributes) {
    // These are a handful of "filters" we use to elide false positives.

    // These are the tags we don't deal with (yet, if ever.)
    if (skip_tagged_die(d)) return true;

    // According to DWARF 3.3.1, a subprogram tag that is missing the external
    // flag means the function is invisible outside its compilation unit. As
    // such, it cannot contribute to an ODRV.
    if (d._tag == dw::tag::subprogram && !attributes.has_uint(dw::at::external)) return true;

    // Empty path means the die (or an ancestor) is anonymous/unnamed. No need to register
    // them.
    if (d._path.empty()) return true;

    // Symbols with __ in them are reserved, so are not user-defined. No need to register
    // them.
    if (d._path.view().find("::__") != std::string::npos) return true;

    // lambdas are ephemeral and can't cause (hopefully) an ODRV
    if (d._path.view().find("lambda") != std::string::npos) return true;

    // we don't handle any die that's ObjC-based.
    if (attributes.has(dw::at::apple_runtime_class)) return true;

    // If the symbol is listed in the symbol_ignore list, we're done here.
    std::string_view symbol = d._path.view();
    if (symbol.size() > 7 && sorted_has(settings::instance()._symbol_ignore, symbol.substr(7)))
        return true;

    // Unfortunately we have to do this work to see if we're dealing with
    // a self-referential type.
    if (attributes.has_reference(dw::at::type)) {
        // if this is a self-referential type, it's die will have no attributes.
        // To determine that, we'll jump to the reference, grab the abbreviation code,
        // and see how many attributes it should have.
        auto reference = attributes.reference(dw::at::type);
        bool empty = temp_seek(_s, _debug_info._offset + reference, std::ios::seekdir::beg, [&] {
            auto abbrev_code = read_uleb();
            const auto& abbrev = find_abbreviation(abbrev_code);
            return abbrev._attributes.empty();
        });

        if (empty) return true;
    }

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
        cu_header header;

        _cu_address = _s.tellg();

        header.read(_s, _details._needs_byteswap);

        // process dies one at a time, recording things like addresses along the way.
        while (true) {
            die die;
            attribute_sequence attributes;
            std::tie(die, attributes) = abbreviation_to_die(_s.tellg(), process_mode::complete);

            // code 0 is reserved; it's a null entry, and signifies the end of siblings.
            if (die._tag == dw::tag::none) {
                path_identifier_pop();

                if (_path.size() == 1) {
                    break; // end of the compilation unit
                }

                continue;
            } else if (die._tag == dw::tag::compile_unit || die._tag == dw::tag::partial_unit) {
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

            dies.emplace_back(std::move(die));
        }
    }

    dies.shrink_to_fit();

    _register_dies(std::move(dies));
}

/**************************************************************************************************/

die_pair dwarf::implementation::fetch_one_die(std::size_t debug_info_offset) {
    if (!_ready && !register_sections_done()) throw std::runtime_error("dwarf setup failed");
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
